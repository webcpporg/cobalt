#!/usr/bin/env python3
# Copyright (c) 2026 WebCpp.org
#
# Distributed under the Boost Software License, Version 1.0. (See
# accompanying file LICENSE_1_0.txt or copy at
# https://www.boost.org/LICENSE_1_0.txt)

"""Check real sequence transfer and graceful cancellation through the CLI."""
import os
from pathlib import Path
import re
import subprocess
import sys

program = str(Path(sys.argv[-1]).resolve())
if program.endswith('.js'):
    command = [os.environ.get('NODE', 'node'), program]
elif program.endswith('.wasm'):
    command = [os.environ.get('WASMTIME', 'wasmtime'), 'run', '-S', 'p3',
               '-W', 'component-model-async=y', '-W', 'component-model-threading=y',
               '-W', 'timeout=20s', program]
else:
    command = [program]
for arguments in (('10000',), ('10000', '100'), ('1',), ('0',)):
    result = subprocess.run([*command, *arguments], capture_output=True, text=True, timeout=25)
    if arguments == ('0',):
        # WASI command exit exposes success/failure rather than the original code.
        assert result.returncode != 0 and 'usage: spsc' in result.stderr, result.stderr
        continue
    assert result.returncode == 0, result.stdout + result.stderr
    match = re.search(r'spsc: count=(\d+) checksum=(\d+) cancelled=([01])', result.stdout)
    assert match, result.stdout
    count, checksum, cancelled = map(int, match.groups())
    assert checksum == count * (count - 1) // 2, result.stdout
    if len(arguments) == 2:
        assert 100 <= count <= 229 and cancelled == 1, result.stdout
    else:
        assert count == int(arguments[0]) and cancelled == 0, result.stdout
    print('PASS ' + result.stdout.strip(), flush=True)
