#!/usr/bin/env python3
# Copyright (c) 2026 WebCpp.org
#
# Distributed under the Boost Software License, Version 1.0. (See
# accompanying file LICENSE_1_0.txt or copy at
# https://www.boost.org/LICENSE_1_0.txt)

"""Record a precise native Boost limitation, never count it as working cancellation."""
import os
from pathlib import Path
import signal
import subprocess
import sys

case, executable = sys.argv[1:]
expected = {
    'accept': (3 if os.name == 'nt' else -signal.SIGABRT,
               'pending accept did not receive cancellation'),
    'composition': (99, 'composition contract timed out'),
}
status, diagnostic = expected[case]
result = subprocess.run([str(Path(executable).resolve())], capture_output=True,
                        text=True, timeout=12)
print(result.stdout, end='')
print(result.stderr, end='', file=sys.stderr)
assert result.returncode == status, (case, result.returncode, result.stdout, result.stderr)
assert diagnostic in result.stderr, (case, result.stderr)
print(f'UPSTREAM-LIMITATION Boost 1.92 {case}: cancellation was not propagated; '
      'this is a negative probe, not supported behavior')
