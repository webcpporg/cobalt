#!/usr/bin/env python3
# Copyright (c) 2026 WebCpp.org
#
# Distributed under the Boost Software License, Version 1.0. (See
# accompanying file LICENSE_1_0.txt or copy at
# https://www.boost.org/LICENSE_1_0.txt)

"""The baseline archive must preserve each declared command and result byte."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


class ArchiveTests(unittest.TestCase):
    def test_roundtrip_and_changed_input(self):
        with tempfile.TemporaryDirectory(prefix='cobalt baseline ') as temporary:
            root = Path(temporary)
            log = root / 'result.log'
            log.write_bytes(b'PASS original target\n')
            manifest = root / 'manifest.json'
            manifest.write_text(json.dumps({'artifacts': [{
                'path': log.name, 'bytes': log.stat().st_size,
                'sha256': hashlib.sha256(log.read_bytes()).hexdigest()}]}))
            command = [sys.executable, str(Path(__file__).with_name('archive.py')),
                       '--manifest', str(manifest)]
            result = subprocess.run([*command, '--capture', str(root)],
                                    capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            log.unlink()
            result = subprocess.run(command, capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            log.write_bytes(b'FAIL changed\n')
            result = subprocess.run([*command, '--capture', str(root)],
                                    capture_output=True, text=True, timeout=10)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('artifact mismatch', result.stderr)


if __name__ == '__main__':
    unittest.main()
