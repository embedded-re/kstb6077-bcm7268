#!/usr/bin/env python3
"""Validate this audit's retained facts without executing board workloads."""
from pathlib import Path
import hashlib
import json

p = Path(__file__).resolve().parent
cpu = (p / 'cpu-math.txt').read_text()
assert 'RESULT=PASS' in cpu
for result in ('NEON_FP32_4lane_FMA=PASS', 'NEON_FP64_2lane_FMA=PASS',
               'software_binary128_1_plus_2pow_minus100=PASS',
               'integer128_arithmetic=PASS', 'CPU_atomic64=PASS lock_free:1',
               'online_cpus=4', 'LDBL_MANT_DIG=113'):
    assert result in cpu, result
metrics = dict(line.split('=', 1) for line in cpu.splitlines())
for key in ('FFT_spectrum_relative_max', 'FFT_roundtrip_max',
            'Cholesky_solution_max', 'Cholesky_residual_max'):
    assert 0 <= float(metrics[key]) < 1e-10, key
shader = (p / 'shader-check.txt').read_text()
assert 'RESULT=PASS checks=11 dispatches=0' in shader
expected = dict(fp32_int32='accepted', fp64='rejected', int64='rejected',
                fp16_explicit='rejected', float_atomic_add='rejected',
                subgroups='rejected', local_128='accepted', local_129='rejected',
                shared_16384='accepted', shared_16388='rejected', recursion='rejected')
for name, value in expected.items():
    assert f'{name}={value} expected={value}' in shader
caps = (p / 'gpu-optin-caps.txt').read_text()
for name, value in dict(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS=128,
                       GL_MAX_COMPUTE_SHARED_MEMORY_SIZE=16384,
                       GL_MAX_SHADER_STORAGE_BLOCK_SIZE=134217728,
                       GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS=16,
                       GL_MAX_COMPUTE_IMAGE_UNIFORMS=8).items():
    assert f'{name}={value}\n' in caps
assert 'OpenGL ES 3.0' in (p / 'gpu-default-caps.txt').read_text()
assert 'OpenGL ES 3.1' in caps and 'git-8b05308' in caps
assert 'QPUs:         8' in (p / 'gpu-ident.txt').read_text()
soft = json.loads((p / 'python-math.json').read_text())
assert soft['bigint_modular_power']['independent_square_multiply_match']
assert soft['decimal_sqrt2']['digits'] == 210
assert soft['complex_identity_error'] < 1e-14
state = json.loads((p / 'state.json').read_text())
assert state['new_gpu_dispatches'] == 0 and state['new_reset_tests'] == 0
for entry in (p / 'SHA256SUMS').read_text().splitlines():
    digest, name = entry.split('  ', 1)
    assert hashlib.sha256((p / name).read_bytes()).hexdigest() == digest, name
print('PASS: CPU execution, GPU limits, 11 shader checks, software precision, archive hashes')
