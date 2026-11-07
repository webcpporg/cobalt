# Copyright (c) 2026 WebCpp.org
#
# Distributed under the Boost Software License, Version 1.0. (See
# accompanying file LICENSE_1_0.txt or copy at
# https://www.boost.org/LICENSE_1_0.txt)

"""Freeze calibration, prepare shuffled cases and measure owned processes locally."""
import argparse
import json
import math
from pathlib import Path
import platform
import random
import shutil
import subprocess
import time

import build
import process

ROOT = build.ROOT
WORKLOADS = ("post", "immediate", "channel", "parallel", "allocation", "timer", "http")
SEED = 20261106


def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n")


def receipts(directory):
    rows = []
    for profile in build.profiles():
        path = directory / (profile["id"] + "-O3-lto-off.json")
        row = json.loads(path.read_text())
        if row["status"] != "passed":
            raise ValueError("Build did not pass: " + str(path))
        for app in row["artifacts"].values():
            for item in app["files"]:
                if build.digest(ROOT / item["path"]) != item["sha256"]:
                    raise ValueError("Artifact changed: " + item["path"])
        rows.append(row)
    if len({r["source_manifest"] for r in rows}) != 1:
        raise ValueError("Mixed source identities in measured builds")
    return rows


def versions():
    commands = {
        "native_compiler": ["clang++", "--version"],
        "wasi_compiler": [str(ROOT / ".local/wasi-sdk/bin/clang++"), "--version"],
        "emscripten": [str(ROOT / ".local/emsdk/upstream/emscripten/em++"), "--version"],
        "node": ["node", "--version"],
        "wasmtime": [str(ROOT / ".local/wasmtime-50/wasmtime/wasmtime"), "--version"],
        "cpu": ["sysctl", "-n", "machdep.cpu.brand_string"],
        "hardware": ["sysctl", "hw.memsize", "hw.ncpu", "hw.physicalcpu", "hw.logicalcpu"],
        "power": ["pmset", "-g", "batt"],
    }
    data = {
        "platform": platform.platform(),
        "python": platform.python_version(),
        "clock": vars(time.get_clock_info("perf_counter")),
        "recorded_at": time.time(),
        "isolation": (
            "No other build/test from this session during runtime measurements. Other user/OS"
            " activity is not controlled."
        ),
    }
    for name, command in commands.items():
        try:
            result = subprocess.run(command, capture_output=True, text=True, timeout=15)
            data[name] = {
                "command": command,
                "exit_code": result.returncode,
                "stdout": result.stdout,
                "stderr": result.stderr,
            }
        except OSError as error:
            data[name] = {"command": command, "unavailable": str(error)}
    return data


def cases(rows, counts=None, smoke=False):
    result = {"process": [], "browser": []}
    # Directory iteration and receipt discovery must not change the seeded order.
    for receipt in sorted(rows, key=lambda row: row["id"]):
        profile = receipt["profile"]
        target = profile["target"]
        hosts = [
            "native" if target == "native" else "wasmtime" if target.startswith("wasi") else "node"
        ]
        if target in ("jspi", "asyncify"):
            hosts += ["page", "worker"]
        for host in hosts:
            for library in ("asio", "cobalt"):
                modes = ("workload",) if counts is None else ("baseline", "workload", "memory")
                for mode in modes:
                    artifact = receipt["artifacts"][library + "_" + mode]
                    workloads = ("none",) if mode == "baseline" else WORKLOADS
                    for workload in workloads:
                        repetitions = (
                            1
                            if counts is None or smoke
                            else 10 if mode == "baseline" else 3 if mode == "memory" else 30
                        )
                        count = 512 if counts is None else 32 if smoke else counts.get(workload, 1)
                        for repeat in range(repetitions):
                            item = dict(
                                id="-".join(
                                    [profile["id"], library, host, mode, workload, str(repeat)]
                                ),
                                profile=profile,
                                library=library,
                                host=host,
                                mode=mode,
                                workload=workload,
                                repeat=repeat,
                                threading=profile["threading"],
                                program=artifact["program"],
                                artifact_files=artifact["files"],
                                build_id=receipt["id"],
                                source_manifest=receipt["source_manifest"],
                                args=(
                                    []
                                    if mode == "baseline"
                                    else [workload, str(count), str(max(32, count // 4))]
                                ),
                            )
                            result["browser" if host in ("page", "worker") else "process"].append(
                                item
                            )
    for group in result:
        random.Random(SEED).shuffle(result[group])
    return result


def launch(item):
    program = str(ROOT / item["program"])
    target = item["profile"]["target"]
    if target == "native":
        command = [program]
    elif target in ("jspi", "asyncify"):
        command = [shutil.which("node"), program]
    else:
        command = [str(ROOT / ".local/wasmtime-50/wasmtime/wasmtime"), "run", "-W", "timeout=30s"]
        if target == "wasip3":
            command += [
                "-S",
                "p3",
                "-W",
                "component-model-async=y",
                "-W",
                "component-model-threading=y",
            ]
        command += [program]
    return command + item["args"]


def run_process(items, destination):
    existing = (
        [json.loads(line) for line in destination.read_text().splitlines()]
        if destination.exists()
        else []
    )
    completed = {row["id"] for row in existing if row["status"] == "passed"}
    for index, item in enumerate(items):
        if item["id"] in completed:
            continue
        for artifact in item["artifact_files"]:
            if build.digest(ROOT / artifact["path"]) != artifact["sha256"]:
                raise ValueError("Artifact changed before execution")
        start = time.time()
        measured = process.normalize(
            process.run(launch(item), cwd=ROOT),
            item["library"],
            item["mode"],
            item["profile"]["target"],
        )
        passed = measured["exit_code"] == 0 and not measured["timed_out"]
        passed = passed and (item["mode"] == "baseline" or "measurement" in measured)
        if passed and item["mode"] != "baseline":
            passed = (
                measured["measurement"]["operations"] == int(item["args"][1])
                and measured["measurement"]["workload"] == item["args"][0]
            )
        row = dict(item, **measured, started=start, status="passed" if passed else "failed")
        with destination.open("a") as stream:
            stream.write(json.dumps(row) + "\n")
        print(f'{row["status"].upper()} {index+1}/{len(items)} {item["id"]}', flush=True)
        if not passed:
            raise RuntimeError(
                "Measurement failed; retain its evidence and diagnose: " + item["id"]
            )


def freeze(output):
    calibration = []
    for name in ("calibration-process.jsonl", "calibration-browser.jsonl"):
        path = output / name
        calibration += [json.loads(line) for line in path.read_text().splitlines()]
    counts = {}
    for workload in WORKLOADS:
        observations = [row for row in calibration if row["workload"] == workload]
        if len(observations) != 66 or any(r["status"] != "passed" for r in observations):
            raise ValueError("Incomplete calibration: " + workload)
        slowest = max(
            row["measurement"]["elapsed_ms"] * 1_000_000 / row["measurement"]["operations"]
            for row in observations
        )
        proposed = 20_000_000 / max(slowest, 1)
        counts[workload] = max(256, min(65536, 2 ** math.floor(math.log2(proposed))))
    record = dict(
        seed=SEED,
        counts=counts,
        frozen_at=time.time(),
        rule=(
            "One common count per workload: power of two targeting 20 ms at the slowest"
            " calibration, bounded 256..65536. Calibration is excluded from reported samples."
        ),
        evidence={
            name: build.digest(output / name)
            for name in ("calibration-process.jsonl", "calibration-browser.jsonl")
        },
    )
    path = output / "workloads.json"
    if path.exists() and json.loads(path.read_text())["counts"] != counts:
        raise ValueError("Refusing to replace frozen workload counts")
    write(path, record)
    return counts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=("calibrate", "prepare", "process"))
    parser.add_argument("--builds", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--smoke", action="store_true")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    rows = receipts(args.builds.resolve())
    if args.phase == "process":
        data = json.loads((output / "process-cases.json").read_text())
        run_process(data["cases"], output / "process-samples.jsonl")
        return
    if args.phase == "calibrate":
        write(output / "environment.json", versions())
        prepared = cases(rows)
        write(
            output / "calibration-browser-cases.json",
            dict(root=str(ROOT), seed=SEED, cases=prepared["browser"]),
        )
        run_process(prepared["process"], output / "calibration-process.jsonl")
    else:
        counts = {kind: 32 for kind in WORKLOADS} if args.smoke else freeze(output)
        prepared = cases(rows, counts, args.smoke)
        for group, items in prepared.items():
            write(
                output / (group + "-cases.json"),
                dict(root=str(ROOT), seed=SEED, smoke=args.smoke, cases=items),
            )
            print(f"{group}: {len(items)} cases")


if __name__ == "__main__":
    main()
