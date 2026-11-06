# Copyright (c) 2026 WebCpp.org
#
# Distributed under the Boost Software License, Version 1.0. (See
# accompanying file LICENSE_1_0.txt or copy at
# https://www.boost.org/LICENSE_1_0.txt)

"""Require a resumed Cobalt main to return both success and failure without traps."""
import os
import subprocess
import sys

for status, arguments in ((7, []), (0, ['zero'])):
    result = subprocess.run([os.environ.get('WASMTIME', 'wasmtime'), 'run', '-W', 'timeout=8s',
                             sys.argv[-1], *arguments], capture_output=True, text=True, timeout=10)
    assert result.returncode == (1 if status else 0), result.stdout + result.stderr
    assert result.stdout.strip() == f'cobalt: returning {status}', result.stdout
    assert result.stderr == '', result.stderr
    print(f'cobalt: return {status}, exact host exit status {result.returncode}')
