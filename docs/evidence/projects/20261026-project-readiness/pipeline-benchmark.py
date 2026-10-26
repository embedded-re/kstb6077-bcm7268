"""Warm still-image pipeline timing, after the sustained test has exited."""
import json,runpy,statistics,time
from pathlib import Path
root=Path('/home/kstb/projects/image-classifier')
a=runpy.run_path(str(root/'classify.py'))
s=a['session'](root/'assets/mobilenetv2-12.onnx',4)
samples=[];prep=[];compute=[]
for i in range(23):
    t=time.perf_counter();x=a['preprocess'](root/'assets/dog.jpg');t1=time.perf_counter()
    out=a['infer'](s,x);done=time.perf_counter()
    assert int(out.argmax())==258
    if i>=3:
        samples.append((done-t)*1000);prep.append((t1-t)*1000);compute.append((done-t1)*1000)
result=dict(samples_ms=samples,preprocessing_ms=prep,inference_ms=compute,
            median_ms=statistics.median(samples),p95_ms=float(a['np'].percentile(samples,95)),
            preprocessing_median_ms=statistics.median(prep),inference_median_ms=statistics.median(compute),
            threads=4,expected_class=258,result='PASS',
            scope='warm file open/decode, RGB resize/crop/normalization and model inference; excludes process/session startup')
(root/'pipeline-benchmark.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result))
