#!/usr/bin/env python3
# Copyright (c) 2026 WebCpp.org
# Distributed under the Boost Software License, Version 1.0.

"""Check actual generator, result, error and bidirectional cancellation behavior."""
import json
import os
from pathlib import Path
import subprocess
import sys

here = Path(__file__).resolve().parent
directory = Path(sys.argv[1]).resolve()
environment = dict(os.environ, PYTHONPATH=str(directory))
failures = []
for mode in ('values', 'error', 'cancel-outer', 'cancel-inner', 'repeat', 'close-generator'):
    command = [sys.executable, str(here / 'probe.py'), mode]
    result = subprocess.run(command, cwd=directory, env=environment,
                            capture_output=True, text=True, timeout=5)
    (directory / f'{mode}.log').write_text(result.stdout + result.stderr)
    try:
        assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
        lines = result.stdout.splitlines()
        actual = json.loads(lines[0])
        if mode in ('values', 'repeat'):
            assert actual == {'generator': list(range(1, 11)), 'promise': 42}, actual
            if mode == 'repeat':
                assert len(lines) == 2 and json.loads(lines[1]) == actual, result.stdout
        elif mode == 'error':
            assert actual == {'caught': 'ValueError', 'message': 'python error witness', 'loop_errors': []}, actual
        elif mode == 'cancel-outer':
            assert actual == {'cancelled': True, 'loop_errors': []}, actual
        elif mode == 'close-generator':
            assert actual == {'closed': True}, actual
        else:
            assert actual == {'caught': 'CancelledError', 'loop_errors': []}, actual
    except (AssertionError, ValueError) as error:
        failures.append(mode)
        print(f'FAIL {mode}: {error}', flush=True)
    else:
        print(f'PASS {mode}: {actual}', flush=True)
raise SystemExit(bool(failures))
