#!/usr/bin/env python3
# Copyright (c) 2026 WebCpp.org
#
# Distributed under the Boost Software License, Version 1.0. (See
# accompanying file LICENSE_1_0.txt or copy at
# https://www.boost.org/LICENSE_1_0.txt)

"""Exercise source inventory diagnostics through the public checker command."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import prepare


class InventoryTests(unittest.TestCase):
    """An omitted, modified or unclassified upstream source must fail validation."""

    def setUp(self) -> None:
        self.scratch = tempfile.TemporaryDirectory(prefix='cobalt inventory ')
        self.addCleanup(self.scratch.cleanup)
        self.root = Path(self.scratch.name)
        (self.root / 'example').mkdir()
        (self.root / 'bench').mkdir()
        self.source = self.root / 'example/timer.cpp'
        self.source.write_text('int main() { return 0; }\n')
        self.manifest = self.root / 'manifest.json'
        self.data = {
            'schema_version': 1,
            'applications': [{'id': 'timer', 'path': 'example/timer.cpp',
                              'sha256': hashlib.sha256(self.source.read_bytes()).hexdigest()}],
            'benchmarks': [], 'supporting_files': [],
        }

    def run_checker(self) -> subprocess.CompletedProcess[str]:
        self.manifest.write_text(json.dumps(self.data))
        return subprocess.run(
            [sys.executable, str(Path(__file__).with_name('inventory.py')),
             '--manifest', str(self.manifest), '--source', str(self.root)],
            capture_output=True, text=True, timeout=10)

    def test_python_migration_binds_cancellation_promise(self) -> None:
        source = Path(__file__).with_name('upstream') / 'example/python.cpp'
        result = prepare.migrate(source.read_text())
        self.assertNotIn('h.promise().get_cancellation_slot()', result)
        self.assertEqual(result.count('auto& promise = h.promise();'), 1)
        self.assertIn('promise.get_cancellation_slot()', result)

    def test_classified_source_passes(self) -> None:
        result = self.run_checker()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('1 application', result.stdout)

    def test_new_source_requires_classification(self) -> None:
        (self.root / 'example/new.cpp').write_text('int main() {}\n')
        result = self.run_checker()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('unclassified source: example/new.cpp', result.stderr)

    def test_modified_source_is_rejected(self) -> None:
        self.source.write_text('int main() { return 7; }\n')
        result = self.run_checker()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('source hash mismatch: example/timer.cpp', result.stderr)

    def test_missing_source_is_rejected(self) -> None:
        self.source.unlink()
        result = self.run_checker()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('missing source: example/timer.cpp', result.stderr)

    def test_duplicate_identity_is_rejected(self) -> None:
        entry = self.data['applications'][0]
        self.data['applications'] = [entry, dict(entry)]
        result = self.run_checker()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('duplicate application id: timer', result.stderr)

    def test_new_benchmark_requires_classification(self) -> None:
        (self.root / 'bench/new.cpp').write_text('int main() {}\n')
        result = self.run_checker()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('unclassified source: bench/new.cpp', result.stderr)


if __name__ == '__main__':
    unittest.main()
