#!/usr/bin/env python3
"""Stdlib software-precision checks; not a cryptographic benchmark."""
import cmath
import decimal
import json
import math
import time

decimal.getcontext().prec = 210
t = time.perf_counter()
x = decimal.Decimal(2).sqrt()
decimal_seconds = time.perf_counter() - t
n = (1 << 4096) - 159
base = 12345678901234567890
exponent = 65537
t = time.perf_counter()
result = pow(base, exponent, n)
bigint_seconds = time.perf_counter() - t
reference, factor, e = 1, base % n, exponent
while e:
    if e & 1:
        reference = (reference * factor) % n
    factor = factor * factor % n
    e >>= 1
assert reference == result
print(json.dumps({
    "decimal_sqrt2": {"digits": 210, "seconds": decimal_seconds,
                      "squared_residual": str(x*x - 2)},
    "bigint_modular_power": {
        "modulus_bits": n.bit_length(), "exponent": exponent,
        "seconds": bigint_seconds,
        "independent_square_multiply_match": reference == result,
        "note": "Deterministic arithmetic check; modulus not asserted prime."},
    "complex_identity_error": abs(cmath.exp(1j * math.pi) + 1)
}, indent=2))
