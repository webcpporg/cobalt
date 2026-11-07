#!/usr/bin/env python3
# Copyright (c) 2026 WebCpp.org
#
# Distributed under the Boost Software License, Version 1.0. (See
# accompanying file LICENSE_1_0.txt or copy at
# https://www.boost.org/LICENSE_1_0.txt)
"""Keep P2 stdin empty for cancellation, then check a coroutine-driven echo."""
import os
import subprocess
import sys
import time
import tempfile

command = [os.environ.get('WASMTIME', 'wasmtime'), 'run', '-W', 'timeout=15s', sys.argv[-1]]
with tempfile.TemporaryFile() as stdout, tempfile.TemporaryFile() as stderr:
    with subprocess.Popen(command, stdin=subprocess.PIPE, stdout=stdout, stderr=stderr) as process:
        try:
            deadline = time.monotonic() + 15
            while time.monotonic() < deadline:
                stderr.seek(0)
                if b'ready-for-input' in stderr.read():
                    break
                assert process.poll() is None, 'guest ended before cancelling its first read'
                time.sleep(.01)
            else:
                raise AssertionError('guest did not complete cancellation')
            process.communicate(b'cobalt-stdio\n', timeout=15)
            stdout.seek(0)
            stderr.seek(0)
            output, errors = stdout.read(), stderr.read()
            assert process.returncode == 0, (process.returncode, output, errors)
            assert output == b'cobalt-stdio\n', output
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=2)
print('PASS Cobalt use_op: P2 pollable stdin cancellation, reuse and stdout echo')
