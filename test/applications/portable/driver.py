#!/usr/bin/env python3
# Copyright (c) 2026 WebCpp.org
#
# Distributed under the Boost Software License, Version 1.0. (See
# accompanying file LICENSE_1_0.txt or copy at
# https://www.boost.org/LICENSE_1_0.txt)

"""Check finite counterparts through their public CLI, including pending cancel."""
import os
from pathlib import Path
import re
import subprocess
import sys

program = str(Path(sys.argv[-1]).resolve())
command = [program]
if program.endswith('.js'):
    command = [os.environ.get('NODE', 'node'), program]
elif program.endswith('.wasm'):
    command = [os.environ.get('WASMTIME', 'wasmtime'), 'run', '-W', 'timeout=10s']
    if '--wasip3' in sys.argv:
        command += ['-S', 'p3', '-W', 'component-model-async=y']
    command += [program]

for kind in ('timer', 'custom', 'channel', 'outcome', 'signals'):
    for delay in (0, 2):
        for cancel_after in (0, 2):
            args = [kind, '4', str(delay), str(cancel_after), '0']
            result = subprocess.run(command + args, capture_output=True, text=True, timeout=15)
            assert result.returncode == 0, (args, result.stdout, result.stderr)
            count = cancel_after or 4
            values = list(map(int, re.findall(r'progress: count=\d+ value=(-?\d+)', result.stdout)))
            expected = list(range(count)) if kind == 'channel' else [
                42 if kind == 'signals' else 11 if kind == 'outcome' else 1] * count
            assert values == expected, (args, values, expected)
            assert f'done: kind={kind} count={count} cancelled={int(bool(cancel_after))}' in result.stdout, result.stdout
            if kind == 'custom':
                match = re.search(r'custom: ready=(\d+) initiated=(\d+)', result.stdout)
                assert match, result.stdout
                ready, initiated = map(int, match.groups())
                assert ready + initiated == count, result.stdout
                if delay == 0:
                    assert (ready, initiated) == (count, 0), result.stdout
    # Cancellation reaches an actually pending timer, before the first value.
    result = subprocess.run(command + [kind, '4', '1000', '0', '2'],
                            capture_output=True, text=True, timeout=15)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert f'done: kind={kind} count=0 cancelled=1' in result.stdout, result.stdout
    if kind == 'custom':
        assert 'custom: ready=0 initiated=1' in result.stdout, result.stdout
    print(f'PASS {kind}: immediate, pending, bounded output, progress cancel, pending cancel')

for args in (['timer', '0'], ['unknown', '4'], ['timer', '4x'], ['timer', '4', '-1']):
    result = subprocess.run(command + args, capture_output=True, text=True, timeout=15)
    # wasi:cli/run exposes result<(), ()>, so the host maps nonzero to 1.
    expected_status = 1 if program.endswith('.wasm') else 2
    assert result.returncode == expected_status, (args, result.returncode, result.stdout, result.stderr)
    assert 'usage:' in result.stderr, result.stderr
print('PASS portable input validation')
