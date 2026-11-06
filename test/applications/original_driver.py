#!/usr/bin/env python3
# Copyright (c) 2026 WebCpp.org
#
# Distributed under the Boost Software License, Version 1.0. (See
# accompanying file LICENSE_1_0.txt or copy at
# https://www.boost.org/LICENSE_1_0.txt)

"""Run locked upstream sources with explicit normal-exit and output witnesses."""
import os
from pathlib import Path
import subprocess
import sys

name = sys.argv[1]
program = str(Path(sys.argv[-1]).resolve())
if program.endswith('.wasm'):
    command = [os.environ.get('WASMTIME', 'wasmtime'), 'run', '-W', 'timeout=10s']
    if '--wasip3' in sys.argv:
        command += ['-S', 'p3', '-W', 'component-model-async=y',
                    '-W', 'component-model-threading=y']
    command += [program]
elif program.endswith('.js'):
    command = [os.environ.get('NODE', 'node'), program]
else:
    command = [program]
if name in ('delay', 'delay_op'):
    command += ['2']
result = subprocess.run(command, capture_output=True, text=True, timeout=15)
assert result.returncode == 0, result.stdout + result.stderr
assert 'webcpp: upstream application reached normal exit' in result.stderr, result.stderr
if name == 'channel':
    assert result.stdout.splitlines() == ['0', '1', '2', '3'], result.stdout
print(f'PASS original {name}: normal exit; output={result.stdout!r}')
