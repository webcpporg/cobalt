# Copyright (c) 2026 WebCpp.org
#
# Distributed under the Boost Software License, Version 1.0. (See
# accompanying file LICENSE_1_0.txt or copy at
# https://www.boost.org/LICENSE_1_0.txt)

"""Check that patched headers and sources are verified before being published."""
from pathlib import Path
import hashlib
import importlib.machinery
import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('prepare', HERE / 'prepare.py')
assert spec is not None and isinstance(spec.loader, importlib.machinery.SourceFileLoader)
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)


class Preparation(unittest.TestCase):
    def test_checked_headers_and_sources(self):
        with tempfile.TemporaryDirectory(prefix='cobalt patch ') as directory:
            root = Path(directory)
            headers, sources = root / 'headers', root / 'sources'
            output, bundle = root / 'output', root / 'bundle'
            names = {'boost/cobalt/io/endpoint.hpp': headers,
                     'libs/cobalt/src/io/endpoint.cpp': sources}
            manifest = {'boost_version': 109200, 'files': {}}
            patch = ''
            for name, parent in names.items():
                original = parent / name
                original.parent.mkdir(parents=True, exist_ok=True)
                original.write_text('before\n')
                manifest['files'][name] = {
                    'before': hashlib.sha256(b'before\n').hexdigest(),
                    'after': hashlib.sha256(b'after\n').hexdigest()}
                patch += f'--- a/{name}\n+++ b/{name}\n@@ -1 +1 @@\n-before\n+after\n'
            (headers / 'boost/version.hpp').write_text('#define BOOST_VERSION 109200\n')
            bundle.mkdir()
            (bundle / 'manifest.json').write_text(json.dumps(manifest))
            (bundle / 'boost-1.92.patch').write_text(patch)
            prepare.prepare(headers, sources, output, bundle)
            timestamps = {}
            for name, parent in names.items():
                self.assertEqual((parent / name).read_text(), 'before\n')
                self.assertEqual((output / name).read_text(), 'after\n')
                timestamps[name] = (output / name).stat().st_mtime_ns
            prepare.prepare(headers, sources, output, bundle)
            for name in names:
                self.assertEqual((output / name).stat().st_mtime_ns, timestamps[name])
            # A caller may select another checked Cobalt patch without copying the engine.
            subprocess.run([sys.executable, str(HERE / 'prepare.py'), '--boost', str(headers),
                            '--sources', str(sources), '--output', str(output),
                            '--bundle', str(bundle)], check=True, capture_output=True)
            # Either input tree changing must invalidate the complete overlay.
            for name, parent in names.items():
                (parent / name).write_text('different\n')
                with self.assertRaisesRegex(ValueError, 'incompatible Boost'):
                    prepare.prepare(headers, sources, output, bundle)
                self.assertEqual((output / name).read_text(), 'after\n')
                (parent / name).write_text('before\n')
            stale = output / 'stale.hpp'
            stale.write_text('obsolete\n')
            with self.assertRaisesRegex(ValueError, 'unexpected overlay'):
                prepare.prepare(headers, sources, output, bundle)
            stale.unlink()
            stale.symlink_to(headers, target_is_directory=True)
            with self.assertRaisesRegex(ValueError, 'symlink'):
                prepare.prepare(headers, sources, output, bundle)
            stale.unlink()
            for original in (headers, sources):
                with self.assertRaisesRegex(ValueError, 'separate'):
                    prepare.prepare(headers, sources, original, bundle)
            (headers / 'boost/version.hpp').write_text('#define BOOST_VERSION 109300\n')
            with self.assertRaisesRegex(ValueError, 'requires Boost 1.92'):
                prepare.prepare(headers, sources, output, bundle)


if __name__ == '__main__':
    unittest.main()
