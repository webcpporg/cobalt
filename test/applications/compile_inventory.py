# Copyright (c) 2026 WebCpp.org
#
# Distributed under the Boost Software License, Version 1.0. (See
# accompanying file LICENSE_1_0.txt or copy at
# https://www.boost.org/LICENSE_1_0.txt)

"""Compile each pinned source with the exact flags of a measured B2 profile."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]


def command_from(log, source_name="channel", source_directory="example"):
    for line in log.read_text().splitlines():
        if (
            f'/{source_directory}/{source_name}.cpp"' in line
            and " -c " in line
            and line.lstrip().startswith('"')
        ):
            words = shlex.split(line)
            result = []
            skip = False
            for word in words:
                if skip:
                    skip = False
                    continue
                if word in ("-o", "-c"):
                    skip = word == "-o"
                    continue
                if word.endswith(".cpp"):
                    continue
                result.append(word)
            return result + ["-fsyntax-only"]
    raise ValueError("No executed " + source_name + " compile command in " + str(log))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--profiles", nargs="*")
    parser.add_argument("--kind", choices=("all", "application", "benchmark"), default="all")
    parser.add_argument("--emsdk-python", default=os.environ.get("EMSDK_PYTHON", sys.executable))
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    manifest = json.loads((HERE / "manifest.json").read_text())
    previous = output / "results.json"
    observations = []
    if previous.exists() and (args.profiles or args.kind != "all"):
        observations = [
            row
            for row in json.loads(previous.read_text())
            if (args.profiles and row["profile"] not in args.profiles)
            or (args.kind != "all" and row["kind"] != args.kind)
        ]
    environment = dict(os.environ)
    environment["EMSDK_PYTHON"] = args.emsdk_python
    for variable in ("CPATH", "CPLUS_INCLUDE_PATH", "C_INCLUDE_PATH"):
        environment.pop(variable, None)
    for receipt in sorted(args.evidence.glob("cobalt-*.json")):
        record = json.loads(receipt.read_text())
        if not isinstance(record, dict) or "command" not in record:
            continue
        profile = record["id"]
        if args.profiles and profile not in args.profiles:
            continue
        log = receipt.with_suffix(".log")
        if record.get("status") != 0:
            continue
        command = command_from(log)
        native = profile.startswith("cobalt-native-")
        form = "upstream" if native else "migrated"
        names = [
            (item["path"], "application")
            for item in manifest["applications"]
            if item["id"] != "python"
        ]
        names += [(item["path"], "benchmark") for item in manifest["benchmarks"]]
        for relative, kind in names:
            if args.kind != "all" and kind != args.kind:
                continue
            source = HERE / form / relative
            exact = command
            if kind == "application":
                try:
                    exact = command_from(log, source.stem)
                except ValueError:
                    pass  # A deliberately absent profile still needs a compile probe.
                if (
                    source.stem in ("http", "ticker")
                    and profile.endswith("-off")
                    and not any(part in profile for part in ("-jspi-", "-asyncify-"))
                ):
                    # Use the actually executed no-EH TLS consumer command,
                    # including that target's source-built OpenSSL headers.
                    tls_log = args.evidence / "http-noeh" / (profile + ".log")
                    exact = command_from(tls_log, "http", "oracle" if native else "port")
            invoked = [*exact, str(source)]
            if kind == "application" and source.stem in ("http", "ticker"):
                # Supply protocol headers even for unsupported raw-socket browser
                # consumers so rejection names the transport contract.
                invoked[-1:-1] = ["-I" + str(ROOT / "libs/beast/include")]
            result = subprocess.run(
                invoked, cwd=ROOT, env=environment, capture_output=True, text=True, timeout=45
            )
            name = f"{profile}-{kind}-{source.stem}"
            (output / (name + ".log")).write_text(
                shlex.join(invoked) + "\n" + result.stdout + result.stderr
            )
            if result.returncode and "error:" not in result.stderr:
                raise RuntimeError(
                    "Compiler did not produce a source diagnostic; inspect " + name + ".log"
                )
            observations.append(
                dict(
                    id=name,
                    profile=profile,
                    source=str(source.relative_to(ROOT)),
                    source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                    form=form,
                    kind=kind,
                    compile="passed" if result.returncode == 0 else "failed",
                    command=invoked,
                    exit_code=result.returncode,
                    evidence=name + ".log",
                    log_sha256=hashlib.sha256((output / (name + ".log")).read_bytes()).hexdigest(),
                )
            )
            (output / "results.json").write_text(json.dumps(observations, indent=2) + "\n")
            print(name, observations[-1]["compile"], flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
