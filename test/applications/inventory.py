#!/usr/bin/env python3
# Copyright (c) 2026 WebCpp.org
#
# Distributed under the Boost Software License, Version 1.0. (See
# accompanying file LICENSE_1_0.txt or copy at
# https://www.boost.org/LICENSE_1_0.txt)

"""Check classified upstream example and benchmark source identities without building them."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import sys
from typing import Any


def validate(manifest: dict[str, Any], source: Path) -> list[str]:
    """Return every missing, unclassified or changed source and duplicate identity."""
    problems: list[str] = []
    if manifest.get('schema_version') != 1:
        return ['unsupported inventory schema_version']
    entries = manifest['applications'] + manifest['benchmarks'] + manifest['supporting_files']
    ids: set[str] = set()
    for entry in manifest['applications']:
        name = entry['id']
        if name in ids:
            problems.append('duplicate application id: ' + name)
        ids.add(name)
    classified: set[str] = set()
    for entry in entries:
        name = entry['path']
        relative = PurePosixPath(name)
        if relative.is_absolute() or '..' in relative.parts or str(relative) != name:
            problems.append('invalid source path: ' + name)
            continue
        if name in classified:
            problems.append('duplicate source path: ' + name)
        classified.add(name)
        path = source / name
        if not path.is_file():
            problems.append('missing source: ' + name)
        elif hashlib.sha256(path.read_bytes()).hexdigest() != entry['sha256']:
            problems.append('source hash mismatch: ' + name)
    actual = {p.relative_to(source).as_posix()
              for pattern in ('example/*.cpp', 'bench/*.cpp') for p in source.glob(pattern)}
    problems.extend('unclassified source: ' + name for name in sorted(actual - classified))
    return problems


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    base = Path(__file__).resolve().parent
    parser.add_argument('--manifest', type=Path, default=base / 'manifest.json')
    parser.add_argument('--source', type=Path, default=base / 'upstream')
    arguments = parser.parse_args()
    try:
        manifest = json.loads(arguments.manifest.read_text())
        problems = validate(manifest, arguments.source)
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, 'inventory: ' + str(error) + '\n')
    if problems:
        parser.exit(1, '\n'.join('inventory: ' + message for message in problems) + '\n')
    print(f"inventory: {len(manifest['applications'])} applications, "
          f"{len(manifest['benchmarks'])} benchmarks and "
          f"{len(manifest['supporting_files'])} supporting files verified")
    return 0


if __name__ == '__main__':
    sys.exit(main())
