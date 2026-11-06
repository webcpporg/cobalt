#!/usr/bin/env python3
# Copyright (c) 2026 WebCpp.org
#
# Distributed under the Boost Software License, Version 1.0. (See
# accompanying file LICENSE_1_0.txt or copy at
# https://www.boost.org/LICENSE_1_0.txt)

"""Capture or verify the exact pre-extraction command and result archive."""
import argparse
import hashlib
import json
from pathlib import Path
import tempfile
import zipfile


def checked(data, entry):
    if (len(data) != entry['bytes'] or
            hashlib.sha256(data).hexdigest() != entry['sha256']):
        raise ValueError('artifact mismatch: ' + entry['path'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path,
                        default=Path(__file__).with_name('manifest.json'))
    parser.add_argument('--capture', type=Path, help='root holding the original artifact paths')
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text())
    entries = manifest['artifacts']
    names = [entry['path'] for entry in entries]
    if len(set(names)) != len(names):
        raise ValueError('duplicate archive path')
    for name in names:
        if Path(name).is_absolute() or '..' in Path(name).parts:
            raise ValueError('unsafe archive path: ' + name)
    archive = args.manifest.with_name('evidence.zip')
    if args.capture:
        with tempfile.TemporaryDirectory(dir=args.manifest.parent) as temporary:
            pending = Path(temporary) / archive.name
            with zipfile.ZipFile(pending, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as target:
                for entry in entries:
                    data = (args.capture / entry['path']).read_bytes()
                    checked(data, entry)
                    target.writestr(entry['path'], data)
            pending.replace(archive)
        data = archive.read_bytes()
        manifest['archive'] = {'path': archive.name, 'bytes': len(data),
                               'sha256': hashlib.sha256(data).hexdigest(),
                               'generated_by': 'archive.py --capture <superproject-root>'}
        args.manifest.write_text(json.dumps(manifest, indent=2) + '\n')
    checked(archive.read_bytes(), manifest['archive'])
    with zipfile.ZipFile(archive) as source:
        if sorted(source.namelist()) != sorted(names):
            raise ValueError('archive inventory differs from manifest')
        for entry in entries:
            checked(source.read(entry['path']), entry)
    print(f'baseline: {len(entries)} exact artifacts verified in {archive.name}')


if __name__ == '__main__':
    main()
