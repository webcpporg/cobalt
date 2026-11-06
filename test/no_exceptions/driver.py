#!/usr/bin/env python3
# Copyright (c) 2026 WebCpp.org
#
# Distributed under the Boost Software License, Version 1.0. (See
# accompanying file LICENSE_1_0.txt or copy at
# https://www.boost.org/LICENSE_1_0.txt)

"""Distinguish a precise upstream abort from successful ownership transfers."""
from pathlib import Path
import os
import subprocess
import sys

program = str(Path(sys.argv[-1]).resolve())
if program.endswith('.wasm'):
    launcher = [os.environ.get('WASMTIME', 'wasmtime'), 'run', '-W', 'timeout=8s']
    if '--wasip3' in sys.argv[1:-1]:
        launcher += ['-S', 'p3', '-W', 'component-model-async=y',
                     '-W', 'component-model-threading=y']
    command = [*launcher, program]
elif program.endswith('.js'):
    command = [os.environ.get('NODE', 'node'), program]
else:
    command = [program]

cases = (('borrowed-race',) if '--race-only' in sys.argv[1:-1] else
         ('pending-task', 'pending-promise', 'pending-generator',
          'completed-task', 'ready-promise', 'ready-generator', 'exception-ptr'))
for case in cases:
    result = subprocess.run([*command, case], capture_output=True, text=True, timeout=12)
    assert 'standard_library=unknown' not in result.stdout, result.stdout
    assert 'exception_handling=' in result.stdout, result.stdout + result.stderr
    expected_abort = ((case.startswith('pending-') or
                       case in ('exception-ptr', 'borrowed-race')) and
                      'exception_handling=off standard_library=libc++ ' in result.stdout)
    assert 'before move\n' in result.stderr, result.stdout + result.stderr
    if expected_abort:
        assert result.returncode != 0, result.stdout + result.stderr
        assert 'after move\n' not in result.stderr, result.stderr
        # Emscripten's release libc++ omits the native verbose-abort text.
        # The direct exception-ptr control identifies the same runtime path.
        diagnostic = ('RuntimeError: Aborted(native code called abort())' if
                      program.endswith('.js') else
                      'make_exception_ptr was called in -fno-exceptions mode')
        assert diagnostic in result.stderr, result.stderr
        verdict = 'known-libc++-abort'
    else:
        assert result.returncode == 0, result.stdout + result.stderr
        assert 'after move\n' in result.stderr, result.stderr
        verdict = 'completed'
    print(case + ': ' + verdict + '; ' + result.stdout.strip(), flush=True)
