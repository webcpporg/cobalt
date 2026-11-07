#!/usr/bin/env python3
# Copyright (c) 2026 WebCpp.org
#
# Distributed under the Boost Software License, Version 1.0. (See
# accompanying file LICENSE_1_0.txt or copy at
# https://www.boost.org/LICENSE_1_0.txt)

"""Current limitations must not rewrite or promote dated passing observations."""

import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from typing import Any


class MatrixTests(unittest.TestCase):
    def test_document_cannot_overwrite_historical_input(self):
        data = dict(applications=[], profiles=[], cells=[], benchmarks=[], counts={})
        with tempfile.TemporaryDirectory(prefix='cobalt matrix ') as temporary:
            root = Path(temporary)
            source = root / 'historical.json'
            source.write_text(json.dumps(data))
            before = source.read_bytes()
            result = subprocess.run(
                [sys.executable, str(Path(__file__).with_name('matrix.py')),
                 '--historical', str(source), '--output', str(root / 'current.json'),
                 '--document', str(source)], capture_output=True, text=True, timeout=10)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(source.read_bytes(), before)

    def test_unavailable_counterpart_is_labeled_as_such(self):
        data = dict(applications=[dict(id='spsc')],
                    profiles=[dict(id='native-multi-on-native', target='native')],
                    cells=[dict(application='spsc', profile='native-multi-on-native',
                                status='upstream-defect',
                                counterpart=dict(status='upstream-defect'))], benchmarks=[],
                    counts={'upstream-defect': 1})
        with tempfile.TemporaryDirectory(prefix='cobalt matrix ') as temporary:
            root = Path(temporary)
            source = root / 'historical.json'
            source.write_text(json.dumps(data))
            document = root / 'current.adoc'
            result = subprocess.run(
                [sys.executable, str(Path(__file__).with_name('matrix.py')),
                 '--historical', str(source), '--output', str(root / 'current.json'),
                 '--document', str(document)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('counterpart unavailable', document.read_text())
            self.assertNotIn(' + counterpart\n', document.read_text())

    def test_current_constraints_preserve_historical_input(self):
        historical: dict[str, Any] = dict(
            schema=1, profiles=[], applications=[], benchmarks=[],
            cells=[dict(application='spsc', status='upstream-defect',
                        reason='Observed lost wakeup', profile='native-multi-on-native',
                        evidence=['dated-original.log'], original_sha256='original identity',
                        implementation_snapshot='dated.sources.json',
                        counterpart=dict(status='passed', evidence=['dated-sequence.log'])),
                   dict(application='thread', status='passed', counterpart=None)],
            counts={'upstream-defect': 1, 'passed': 1})
        with tempfile.TemporaryDirectory(prefix='cobalt matrix ') as temporary:
            root = Path(temporary)
            source = root / 'historical.json'
            source.write_text(json.dumps(historical))
            before = source.read_bytes()
            output = root / 'current.json'
            result = subprocess.run(
                [sys.executable, str(Path(__file__).with_name('matrix.py')),
                 '--historical', str(source),
                 '--output', str(output)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            current = json.loads(output.read_text())
            self.assertEqual(source.read_bytes(), before)
            self.assertEqual(current['cells'][1], historical['cells'][1])
            expected = copy.deepcopy(historical['cells'][0]['counterpart'])
            self.assertEqual(current['cells'][0]['historical_counterpart'], expected)
            self.assertEqual(current['cells'][0]['counterpart']['status'], 'upstream-defect')
            self.assertEqual(current['cells'][0]['original_sha256'], 'original identity')
            self.assertEqual(current['cells'][0]['implementation_snapshot'], 'dated.sources.json')
            self.assertIn('delayed-notification', current['cells'][0]['reason'])
            self.assertEqual(current['historical_report']['sha256'],
                             hashlib.sha256(before).hexdigest())


if __name__ == '__main__':
    unittest.main()
