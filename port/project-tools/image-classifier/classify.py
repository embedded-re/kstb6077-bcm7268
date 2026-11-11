#!/usr/bin/env python3
"""CPU MobileNetV2 image classification, benchmark and bounded reliability check."""
import argparse
import hashlib
import json
from pathlib import Path
import resource
import statistics
import sys
import time

import numpy as np
import onnxruntime as ort
from PIL import Image

ROOT = Path(__file__).resolve().parent

def temperature():
    return int(Path('/sys/class/thermal/thermal_zone0/temp').read_text()) / 1000

def available_kib():
    return int(next(line.split()[1] for line in Path('/proc/meminfo').read_text().splitlines()
                    if line.startswith('MemAvailable:')))

def preprocess(image):
    with Image.open(image) as original:
        im = original.convert('RGB')
        w, h = im.size
        im = im.resize((round(w * 256 / min(w, h)), round(h * 256 / min(w, h))), Image.Resampling.BILINEAR)
        w, h = im.size
        im = im.crop(((w-224)//2, (h-224)//2, (w-224)//2+224, (h-224)//2+224))
        a = np.asarray(im, dtype=np.float32) / np.float32(255)
    a = (a-np.array([.485,.456,.406], dtype=np.float32))/np.array([.229,.224,.225], dtype=np.float32)
    return np.ascontiguousarray(a.transpose(2,0,1)[None])

def session(model, threads):
    opts = ort.SessionOptions()
    opts.intra_op_num_threads = threads
    opts.inter_op_num_threads = 1
    opts.execution_mode = ort.ExecutionMode.ORT_SEQUENTIAL
    opts.add_session_config_entry('session.intra_op.allow_spinning', '0')
    return ort.InferenceSession(str(model), sess_options=opts, providers=['CPUExecutionProvider'])

def infer(s, x):
    out = s.run(None, {s.get_inputs()[0].name:x})[0]
    if out.shape != (1,1000) or not np.isfinite(out).all():
        raise RuntimeError('invalid classifier output')
    return out

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('image', nargs='?', type=Path, default=ROOT/'assets/dog.jpg')
    p.add_argument('--threads', type=int, choices=[1,2,4], default=2)
    p.add_argument('--benchmark', type=int, default=0, help='warm samples per thread setting, max100')
    p.add_argument('--soak-seconds', type=int, default=0, help='bounded CPU/RAM check, max600')
    p.add_argument('--self-test', action='store_true', help='require known sample class and repeatability')
    p.add_argument('--report', type=Path)
    args = p.parse_args()
    if not 0 <= args.benchmark <= 100 or not 0 <= args.soak_seconds <= 600:
        p.error('benchmark/soak outside bounded range')
    assets = ROOT/'assets'
    manifest = json.loads((assets/'assets.json').read_text())
    for name in ('mobilenetv2-12.onnx','synset.txt') + (('dog.jpg',) if args.self_test else ()):
        actual = hashlib.sha256((assets/name).read_bytes()).hexdigest()
        if actual != manifest[name]['sha256']:
            raise RuntimeError('asset hash mismatch: '+name)
    if args.self_test and args.image.resolve() != (assets/'dog.jpg').resolve():
        p.error('--self-test requires the bundled sample image')
    start = time.perf_counter()
    x = preprocess(args.image)
    prep_ms = (time.perf_counter()-start)*1000
    labels = (assets/'synset.txt').read_text().splitlines()
    assert len(labels) == 1000
    report = dict(runtime=ort.__version__, numpy=np.__version__, providers=ort.get_available_providers(),
                  image=str(args.image), preprocessing_ms=prep_ms, benchmarks=[])
    reference = None
    settings = [1,2,4] if args.benchmark else [args.threads]
    sessions = {}
    for threads in settings:
        t = time.perf_counter();s = session(assets/'mobilenetv2-12.onnx',threads)
        load_ms = (time.perf_counter()-t)*1000
        sessions[threads] = s
        assert s.get_providers() == ['CPUExecutionProvider']
        out = infer(s,x)
        if reference is None:
            reference = out.copy()
        else:
            np.testing.assert_allclose(out,reference,rtol=5e-4,atol=5e-4)
        if args.self_test:
            assert int(out.argmax()) == 258, 'expected Samoyed on known image'
        samples = []
        for i in range(3+max(args.benchmark,1)):
            t = time.perf_counter();current = infer(s,x);elapsed = (time.perf_counter()-t)*1000
            np.testing.assert_allclose(current,out,rtol=5e-4,atol=5e-4)
            if i >= 3:samples.append(elapsed)
        result = dict(threads=threads,load_ms=load_ms,samples_ms=samples,
                      median_ms=statistics.median(samples),p95_ms=float(np.percentile(samples,95)))
        report['benchmarks'].append(result)
        print(json.dumps(dict(benchmark=result)),flush=True)
    chosen = min(report['benchmarks'],key=lambda b:b['median_ms'])['threads']
    s = sessions[chosen]
    # Release unused pools/model allocations before measuring sustained use.
    sessions = {chosen:s}
    logits = reference[0].astype(np.float64);prob = np.exp(logits-logits.max());prob /= prob.sum()
    report['top5'] = [dict(index=int(i),label=labels[i],probability=float(prob[i])) for i in np.argsort(prob)[-5:][::-1]]
    report['chosen_threads'] = chosen
    if args.soak_seconds:
        assert available_kib() > 768*1024, 'not enough available RAM for bounded memory test'
        memory = np.empty(256*1024*1024//8,dtype=np.uint64)
        block = 2*1024*1024  # 16 MiB chunks, bounded comparison temporary.
        def verify_memory():
            for i in range(0,len(memory),block):
                assert np.all(memory[i:i+block] == np.uint64(0xa5a5f00d12345678 ^ i)), 'RAM pattern mismatch'
        for i in range(0,len(memory),block):
            memory[i:i+block] = np.uint64(0xa5a5f00d12345678 ^ i)
        verify_memory()
        t0 = time.monotonic();next_sample = t0;next_memory = t0+10
        count = 0;max_temp = temperature();min_available = available_kib();sensors = []
        while time.monotonic()-t0 < args.soak_seconds:
            current = infer(s,x)
            np.testing.assert_allclose(current,reference,rtol=5e-4,atol=5e-4)
            count += 1
            now = time.monotonic()
            if now >= next_sample:
                temp = temperature();free = available_kib()
                max_temp = max(max_temp,temp);min_available = min(min_available,free)
                assert temp < 80, 'stopped at conservative 80 C test threshold'
                assert free > 256*1024, 'available RAM below 256 MiB test guard'
                sample = dict(elapsed_s=now-t0,inferences=count,temp_c=temp,available_kib=free,
                              peak_rss_kib=resource.getrusage(resource.RUSAGE_SELF).ru_maxrss)
                sensors.append(sample);print(json.dumps(dict(sensor=sample)),flush=True)
                next_sample = now+5
            if now >= next_memory:
                verify_memory();next_memory = now+10
        verify_memory()
        report['soak'] = dict(seconds=time.monotonic()-t0,inferences=count,memory_pattern_mib=256,
                              max_temp_c=max_temp,min_available_kib=min_available,sensors=sensors,
                              memory_check='PASS',repeatability='PASS')
        del memory
    report['peak_rss_kib'] = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    report['result'] = 'PASS'
    if args.report:
        args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(result='PASS',chosen_threads=chosen,top5=report['top5'],peak_rss_kib=report['peak_rss_kib'])),flush=True)

if __name__ == '__main__':
    main()
