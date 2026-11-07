#!/usr/bin/env python3
# Copyright (c) 2026 WebCpp.org
#
# Distributed under the Boost Software License, Version 1.0. (See
# accompanying file LICENSE_1_0.txt or copy at
# https://www.boost.org/LICENSE_1_0.txt)

"""Build the optional native binding with an explicit matching Cobalt archive."""
import argparse
import hashlib
import importlib.metadata
import json
from pathlib import Path
import platform
import shlex
import subprocess
import sys
import sysconfig

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--root", type=Path, required=True)
parser.add_argument("--archive", type=Path, required=True)
parser.add_argument("--nanobind", type=Path, required=True)
parser.add_argument("--boost", type=Path, required=True)
parser.add_argument("--compiler", required=True)
parser.add_argument("--output", type=Path, required=True)
parser.add_argument(
    "--source",
    type=Path,
    default=Path(__file__).resolve().parents[1] / "upstream/example/python.cpp",
)
args = parser.parse_args()
lock = json.loads(Path(__file__).with_name("dependencies.json").read_text())
assert sys.implementation.name == lock["interpreter"]
assert (
    platform.python_version() == lock["python_version"]
), "Use the pinned optional Python fixture interpreter."
root, output, nanobind = args.root.resolve(), args.output.resolve(), args.nanobind.resolve()
output.mkdir(parents=True, exist_ok=True)
archive = args.archive.resolve()
assert archive.is_file(), f"Build /webcpp/cobalt//core first: {archive}"
assert platform.system() in (
    "Darwin",
    "Linux",
), "This optional fixture currently records native Unix hosts only."
distribution = next(
    item
    for item in importlib.metadata.distributions(path=[str(nanobind.parent)])
    if item.metadata["Name"] == "nanobind"
)
assert distribution.version == lock["nanobind_version"]
module = output / ("boost_cobalt_example_python" + sysconfig.get_config_var("EXT_SUFFIX"))
command = [
    args.compiler,
    "-std=c++20",
    "-O3",
    "-DNDEBUG",
    "-fPIC",
    "-fvisibility=hidden",
    "--system-header-prefix=boost/",
    "-DNB_COMPACT_ASSERTIONS",
    "-DBOOST_COBALT_NO_LIB=1",
    "-DBOOST_ALL_NO_LIB",
    "-shared",
]
if platform.system() == "Darwin":
    command += ["-undefined", "dynamic_lookup"]
for include in (
    root / "libs/cobalt/include",
    root / "libs/asio/include",
    root / "libs/config/include",
    args.boost.resolve(),
    Path(sysconfig.get_paths()["include"]),
    nanobind / "include",
    nanobind / "ext/robin_map/include",
):
    command += ["-I", str(include)]
command += [
    str(args.source.resolve()),
    str(nanobind / "src/nb_combined.cpp"),
    str(archive),
    "-o",
    str(module),
]
print(shlex.join(command), flush=True)
result = subprocess.run(command, capture_output=True, text=True, timeout=180)
(output / "build.log").write_text(result.stdout + result.stderr)
record = {
    "command": command,
    "status": result.returncode,
    "python": sys.version,
    "platform": platform.platform(),
    "nanobind": distribution.version,
    "source": str(args.source.resolve()),
    "source_sha256": hashlib.sha256(args.source.read_bytes()).hexdigest(),
    "archive_sha256": hashlib.sha256(archive.read_bytes()).hexdigest(),
    "compiler": subprocess.check_output([args.compiler, "--version"], text=True),
}
if result.returncode == 0:
    record["module_sha256"] = hashlib.sha256(module.read_bytes()).hexdigest()
(output / "build.json").write_text(json.dumps(record, indent=2) + "\n")
print(result.stdout + result.stderr)
raise SystemExit(result.returncode)
