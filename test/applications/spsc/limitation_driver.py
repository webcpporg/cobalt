#!/usr/bin/env python3
# Copyright (c) 2026 WebCpp.org
#
# Distributed under the Boost Software License, Version 1.0. (See
# accompanying file LICENSE_1_0.txt or copy at
# https://www.boost.org/LICENSE_1_0.txt)

"""Require the deterministic queue defect, with an external process deadline."""

import os
from pathlib import Path
import subprocess
import sys

kind, name = sys.argv[1:]
assert kind in ('original', 'counterpart')
program = str(Path(name).resolve())
if program.endswith('.js'):
    command = [os.environ.get('NODE', 'node'), program]
elif program.endswith('.wasm'):
    command = [os.environ.get('WASMTIME', 'wasmtime'), 'run', '-S', 'p3',
               '-W', 'component-model-async=y', '-W', 'component-model-threading=y',
               '-W', 'timeout=20s', program]
else:
    command = [program]
result = subprocess.run(command, capture_output=True, text=True, timeout=25)
marker = f'UPSTREAM-LIMITATION {kind}: delayed notification drops value 2'
assert result.returncode == 0, result.stdout + result.stderr
assert result.stdout.splitlines() == [marker], result.stdout
assert result.stderr.strip() == 'No errors detected.', result.stderr
print(marker, flush=True)

# The current support policy and preservation of dated reports are part of
# this recurring lane, so changing the matrix generator runs its regression.
matrix_test = Path(__file__).resolve().parent.parent / 'matrix_test.py'
matrix_result = subprocess.run([sys.executable, str(matrix_test)], capture_output=True,
                               text=True, timeout=10)
assert matrix_result.returncode == 0, matrix_result.stdout + matrix_result.stderr
print('PASS SPSC matrix classification and historical preservation', flush=True)
