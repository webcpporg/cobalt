#!/usr/bin/env python3
# Copyright (c) 2026 WebCpp.org
#
# Distributed under the Boost Software License, Version 1.0. (See
# accompanying file LICENSE_1_0.txt or copy at
# https://www.boost.org/LICENSE_1_0.txt)
"""Use a fresh host-granted directory and verify bytes outside the guest."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

program = str(Path(sys.argv[-1]).resolve())
with tempfile.TemporaryDirectory(prefix='cobalt-files-') as directory:
    command = [program, str(Path(directory) / 'data')]
    if program.endswith('.wasm'):
        command = [os.environ.get('WASMTIME', 'wasmtime'), 'run', '--dir', f'{directory}::/fixture',
                   '-W', 'timeout=20s']
        if '--wasip3' in sys.argv:
            command += ['-S', 'p3', '-W', 'component-model-async=y']
        command += [program, '/fixture/data']
    result = subprocess.run(command, capture_output=True, text=True, timeout=30)
    print(result.stdout, end='')
    print(result.stderr, end='', file=sys.stderr)
    assert result.returncode == 0, (command, result.returncode)
    assert (Path(directory) / 'data').read_bytes() == b'ABCD4XYZ89'
    print('PASS host observed exact offset-write bytes')

    if os.name == 'nt':
        print('POSIX stdin cancellation fixture does not apply to Windows handles')
        raise SystemExit(0)
    # A live anonymous host pipe exercises the supplied-descriptor path even on
    # WASI, without claiming that the guest can create a pipe of its own.
    command[-1] = '--cancel-stdin'
    with tempfile.TemporaryFile() as stdout, tempfile.TemporaryFile() as stderr:
        process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=stdout, stderr=stderr)
        assert process.stdin is not None
        try:
            deadline = time.monotonic() + 15
            while time.monotonic() < deadline:
                stderr.seek(0)
                if b'cancel-issued' in stderr.read():
                    break
                assert process.poll() is None, 'guest exited before requesting cancellation'
                time.sleep(.01)
            else:
                raise AssertionError('guest did not request cancellation')
            try:
                process.stdin.write(b'x')
                process.stdin.flush()
            except BrokenPipeError:
                pass
            status = process.wait(timeout=15)
            stdout.seek(0)
            stderr.seek(0)
            output, errors = stdout.read().decode(), stderr.read().decode()
            assert status == 0, (command, status, output, errors)
            if program.endswith('.wasm'):
                assert 'SDK-BLOCKED: raw POSIX stdin' in output, output
                print('OBSERVED SDK limitation: raw POSIX stdin cannot become nonblocking')
            else:
                print('PASS supplied stdin descriptor cancellation')
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=2)
            try:
                process.stdin.close()
            except BrokenPipeError:
                pass
