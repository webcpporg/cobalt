# Copyright (c) 2026 WebCpp.org
#
# Distributed under the Boost Software License, Version 1.0. (See
# accompanying file LICENSE_1_0.txt or copy at
# https://www.boost.org/LICENSE_1_0.txt)

"""Create a deterministic evidence archive with text inputs and a SHA-256 manifest."""
import argparse
import gzip
import hashlib
import io
import json
from pathlib import Path
import tarfile

SUFFIXES = {
    ".json",
    ".jsonl",
    ".csv",
    ".log",
    ".txt",
    ".py",
    ".cpp",
    ".hpp",
    ".h",
    ".ipp",
    ".inl",
    ".inc",
    ".c",
    ".cc",
    ".cxx",
    ".sh",
    ".mjs",
    ".js",
    ".html",
    ".css",
    ".md",
    ".adoc",
    ".jam",
    ".expected",
    ".rs",
    ".toml",
    ".lock",
    ".wit",
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--input", action="append", required=True, help="archive-label=directory-or-file"
    )
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    files = {}
    for value in args.input:
        label, source = value.split("=", 1)
        if "/" in label or label in ("", ".", ".."):
            raise ValueError("Invalid archive label")
        source = Path(source).resolve()
        candidates = [source] if source.is_file() else sorted(source.rglob("*"))
        for path in candidates:
            relative = Path(path.name) if source.is_file() else path.relative_to(source)
            if any(
                part in (".git", "node_modules", "__pycache__", "results")
                for part in relative.parts
            ):
                continue
            if path.is_file() and (
                path.suffix in SUFFIXES or path.name in ("Jamfile", "Jamroot", "review-b2")
            ):
                files[(Path(label) / relative).as_posix()] = path.read_bytes()
    if not files:
        raise ValueError("No evidence to archive")
    manifest = {name: hashlib.sha256(data).hexdigest() for name, data in sorted(files.items())}
    files["SHA256.json"] = (json.dumps(manifest, indent=2) + "\n").encode()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("wb") as raw, gzip.GzipFile(
        fileobj=raw, mode="wb", mtime=0, filename=""
    ) as compressed:
        with tarfile.open(fileobj=compressed, mode="w") as archive:
            for name, data in sorted(files.items()):
                info = tarfile.TarInfo(name)
                info.size = len(data)
                info.mode = 0o644
                archive.addfile(info, io.BytesIO(data))
    digest = hashlib.sha256(args.output.read_bytes()).hexdigest()
    args.output.with_suffix(args.output.suffix + ".sha256").write_text(
        digest + "  " + args.output.name + "\n"
    )
    print(f"{len(manifest)} evidence files, {args.output.stat().st_size} bytes, SHA-256 {digest}")


if __name__ == "__main__":
    main()
