#!/usr/bin/env python3
"""Compare top-down RGB readbacks from two backends on the same test state."""
import pathlib, sys

def read(path):
    with pathlib.Path(path).open('rb') as f:
        assert f.readline() == b'P6\n'
        size = f.readline()
        assert f.readline() == b'255\n'
        return size, f.read()

size, a = read(sys.argv[1])
other, b = read(sys.argv[2])
assert size == other and len(a) == len(b)
delta = sorted(abs(x-y) for x,y in zip(a,b))
mean = sum(delta)/len(delta)
p99 = delta[int(len(delta)*.99)]
large = sum(d > 8 for d in delta)/len(delta)
print(f'RGB parity: mean absolute difference={mean:.5f}/255, P99={p99}/255, fraction >8={large:.6%}')
# Small rasterization/precision differences are allowed, orientation/color errors are not.
assert mean < 1.5 and large < .005, 'Backend image mismatch'
