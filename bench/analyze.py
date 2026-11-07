# Copyright (c) 2026 WebCpp.org
#
# Distributed under the Boost Software License, Version 1.0. (See
# accompanying file LICENSE_1_0.txt or copy at
# https://www.boost.org/LICENSE_1_0.txt)

"""Validate complete observations and generate Release size, latency and memory tables."""
import argparse
import csv
import gzip
import hashlib
import json
import math
from pathlib import Path
import random
import statistics

import build
import experiment
import process
import qualify


def summary(values):
    values = sorted(values)
    if not values or any(not math.isfinite(v) or v < 0 for v in values):
        raise ValueError("Invalid or absent observation")
    rng = random.Random(20261106)
    boot = sorted(statistics.median(rng.choices(values, k=len(values))) for _ in range(2000))
    return dict(
        n=len(values),
        median=statistics.median(values),
        p95=values[math.ceil(0.95 * len(values)) - 1],
        minimum=values[0],
        maximum=values[-1],
        median_ci95=[boot[49], boot[1949]],
    )


def bind_builds(cases, receipts):
    selected = {row["id"]: row for row in receipts}
    for case in cases:
        receipt = selected.get(case["build_id"])
        if receipt is None:
            raise ValueError("Missing build receipt: " + case["build_id"])
        artifact = receipt["artifacts"].get(case["library"] + "_" + case["mode"])
        if (
            receipt["optimization"] != "O3"
            or receipt["lto"] != "off"
            or artifact is None
            or receipt["profile"] != case["profile"]
            or receipt["source_manifest"] != case["source_manifest"]
            or artifact["program"] != case["program"]
            or artifact["files"] != case["artifact_files"]
        ):
            raise ValueError("Runtime case differs from selected build receipt: " + case["id"])


def canonical_cases(cases, receipts, counts, group):
    primary = [row for row in receipts if row["optimization"] == "O3" and row["lto"] == "off"]
    expected = experiment.cases(primary, counts)[group]
    if cases != expected:
        raise ValueError("Cases differ from the complete canonical profile/host/workload set")


def complete(cases, observations):
    expected = {row["id"]: row for row in cases}
    actual = {}
    for row in observations:
        if row["status"] != "passed":
            raise ValueError("Failed observation retained: " + row["id"])
        if row["id"] in actual:
            raise ValueError("Duplicate observation: " + row["id"])
        actual[row["id"]] = row
    if actual.keys() != expected.keys():
        raise ValueError("Missing or unexpected observations")
    for key, row in actual.items():
        for field in (
            "program",
            "profile",
            "library",
            "host",
            "build_id",
            "mode",
            "workload",
            "repeat",
            "args",
            "artifact_files",
            "source_manifest",
        ):
            if row[field] != expected[key][field]:
                raise ValueError("Changed case identity: " + key + "/" + field)
        process.normalize(row, row["library"], row["mode"], row["profile"]["target"])
        if row["mode"] != "baseline" and (
            row["measurement"]["operations"] != int(row["args"][1])
            or row["measurement"]["workload"] != row["args"][0]
        ):
            raise ValueError("Executed workload differs from the frozen case")
    return list(actual.values())


def rows(path):
    return [json.loads(line) for line in path.read_text().splitlines()]


def write_csv(path, data):
    with path.open("w") as stream:
        out = csv.DictWriter(stream, fieldnames=list(data[0]))
        out.writeheader()
        out.writerows(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--builds", type=Path, required=True)
    parser.add_argument("--experiment", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--document", type=Path)
    parser.add_argument("--data-path", help="Library-relative directory for published CSV/figures")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    build_rows = []
    for path in sorted(args.builds.glob("*.json")):
        if path.name.endswith(".sources.json"):
            continue
        record = json.loads(path.read_text())
        if record["status"] != "passed":
            raise ValueError("Failed build: " + path.name)
        if record["compilation_context"] != "isolated":
            raise ValueError("Preparation build time is not an isolated measurement")
        build_rows.append(record)
    if len(build_rows) != 136 or len({r["source_manifest"] for r in build_rows}) != 1:
        raise ValueError("Expected 136 builds from one source identity")
    expected_builds = {
        p["id"] + "-" + opt + "-lto-" + lto
        for p in build.profiles()
        for opt in ("O2", "O3", "Os", "Oz")
        for lto in ("off", "on")
    }
    if {r["id"] for r in build_rows} != expected_builds:
        raise ValueError("Incomplete optimization/profile build set")
    for record in build_rows:
        source = args.builds / record["source_manifest"]
        snapshot = json.loads(source.read_text())
        digest = hashlib.sha256(json.dumps(snapshot, sort_keys=True).encode()).hexdigest()
        if source.name != digest + ".sources.json":
            raise ValueError("Changed source snapshot")
        if build.digest(args.builds / (record["id"] + ".log")) != record["log_sha256"]:
            raise ValueError("Changed build log")
        build.audit_profile(record["compile_commands"], record["link_commands"], record["profile"])
        build.audit_warnings(
            (args.builds / (record["id"] + ".log")).read_text(),
            record["profile"],
            record["lto"] == "on",
        )
    qualified = complete(qualify.cases(build_rows), rows(args.builds / "qualification.jsonl"))
    sizes = []
    for record in build_rows:
        for name, artifact in record["artifacts"].items():
            encoded = 0
            for item in artifact["files"]:
                path = build.ROOT / item["path"]
                if build.digest(path) != item["sha256"]:
                    raise ValueError("Changed artifact: " + str(path))
                encoded += len(gzip.compress(path.read_bytes(), compresslevel=9, mtime=0))
            sizes.append(
                dict(
                    build=record["id"],
                    profile=record["profile"]["id"],
                    optimization=record["optimization"],
                    lto=record["lto"],
                    program=name,
                    payload_bytes=artifact["total_bytes"],
                    gzip_bytes=encoded,
                    wasm_bytes=sum(f["bytes"] for f in artifact["files"] if f["kind"] == "wasm")
                    or None,
                    javascript_bytes=sum(
                        f["bytes"] for f in artifact["files"] if f["kind"] == "javascript"
                    )
                    or None,
                    native_bytes=sum(
                        f["bytes"] for f in artifact["files"] if f["kind"] == "native-executable"
                    )
                    or None,
                    build_seconds_six_programs=record["build_seconds"],
                )
            )
    write_csv(args.output / "sizes.csv", sizes)
    data = []
    counts = json.loads((args.experiment / "workloads.json").read_text())["counts"]
    for group in ("process", "browser"):
        cases = json.loads((args.experiment / (group + "-cases.json")).read_text())
        if cases.get("smoke"):
            raise ValueError("Smoke runs cannot be published as measurements")
        bind_builds(cases["cases"], build_rows)
        canonical_cases(cases["cases"], build_rows, counts, group)
        data += complete(cases["cases"], rows(args.experiment / (group + "-samples.jsonl")))
    groups = {}
    for row in data:
        key = (row["profile"]["id"], row["host"], row["library"], row["mode"], row["workload"])
        groups.setdefault(key, []).append(row)
    metrics = []
    for (profile, host, library, mode, workload), observations in sorted(groups.items()):
        required = 10 if mode == "baseline" else 3 if mode == "memory" else 30
        if len(observations) != required:
            raise ValueError("Wrong observation count")
        fields = {
            "wall_ms": lambda r: r.get(
                "process_wall_ms", (r.get("result") or {}).get("complete_ms")
            ),
            "user_cpu_ms": lambda r: r.get("process_cpu_user_ms"),
            "system_cpu_ms": lambda r: r.get("process_cpu_system_ms"),
            "peak_process_rss_bytes": lambda r: r.get("process_peak_rss_bytes"),
        }
        if mode != "baseline":
            fields.update(
                {
                    "workload_ms": lambda r: r["measurement"]["elapsed_ms"],
                    "ns_per_operation": (
                        lambda r: r["measurement"]["elapsed_ms"]
                        * 1e6
                        / r["measurement"]["operations"]
                    ),
                }
            )
            for name in observations[0]["measurement"]:
                if name.startswith(("cpp_", "cobalt_frame_", "linear_")):
                    fields[name] = lambda r, name=name: r["measurement"].get(name)
        if host in ("page", "worker"):
            for name in ("fetch_ms", "compile_ms", "instantiate_ms", "runtime_ready_ms"):
                fields[name] = lambda r, name=name: r["result"].get(name)
        for metric, accessor in fields.items():
            values = [accessor(row) for row in observations]
            available = [v for v in values if v is not None]
            stats = summary(available) if available else None
            metrics.append(
                dict(
                    profile=profile,
                    host=host,
                    library=library,
                    mode=mode,
                    workload=workload,
                    metric=metric,
                    summary=stats,
                    scope=(
                        "descriptive instrumentation only"
                        if mode == "memory"
                        else "measured sample distribution"
                    ),
                )
            )
    (args.output / "summary.json").write_text(
        json.dumps(dict(sizes=sizes, metrics=metrics), indent=2) + "\n"
    )
    csv_metrics = [
        dict((k, v) for k, v in item.items() if k != "summary")
        | (
            item["summary"]
            or dict(n=0, median=None, p95=None, minimum=None, maximum=None, median_ci95=None)
        )
        for item in metrics
    ]
    write_csv(args.output / "metrics.csv", csv_metrics)
    if args.document:
        lines = [
            (
                "// Copyright (c) 2026 WebCpp.org\n//\n// Distributed under the Boost Software"
                " License, Version 1.0. (See\n// accompanying file LICENSE_1_0.txt or copy at\n//"
                " https://www.boost.org/LICENSE_1_0.txt)\n"
            ),
            "// Generated by bench/analyze.py. Do not edit.\n",
            (
                "[[cobalt-release-measurements]]\n== Release measurements of the shipped"
                " implementation\n\n"
            ),
            (
                "These local measurements compare Boost-backed native interfaces with the shipped"
                " Wasm implementation on one machine. They are not a cross-machine performance"
                " guarantee. See the raw environment, commands, hashes and samples accompanying the"
                " tables.\n\n"
            ),
            (
                "Sizes cover `-O2`, `-O3`, `-Os` and `-Oz`, each with full LTO off/on. Runtime"
                " comparisons use `-O3` without LTO across every feasible profile. All builds use"
                " Release and `NDEBUG`. EH-on uses `-fwasm-exceptions` for JSPI/WASI and"
                " `-fexceptions` for Asyncify in this toolchain. Those comparisons include the"
                " selected exception code generation, not suspension alone. The workload executable"
                " contains seven finite matched workloads, not an unchanged upstream application."
                " The baseline contains only an empty runner. Browser payload is JavaScript plus"
                " Wasm; native is a Mach-O executable. Runtime installations and native system"
                " shared libraries are outside these payload sizes.\n\n"
            ),
            (
                "All 136 optimization/profile builds also execute the empty runner and seven short"
                " checked workloads for both libraries: 2,176 payload qualification runs in native,"
                " Node or Wasmtime. They validate the size artifacts; their timings are excluded"
                " from the performance tables. Real-browser measurements and correctness"
                " observations below remain separate.\n\n"
            ),
            (
                "Single/multi are B2 threading properties. Native single does not disable Boost"
                " threads; Wasm multi selects the thread-capable SDK profile. Each workload runs"
                " one context on one execution thread. This is not a multicore throughput"
                " comparison. P3 threads are cooperative.\n\n"
            ),
            (
                "Build time is one forced rebuild of six programs per setting with three compiler"
                " jobs and a warmed SDK cache. No other build/test from this session ran"
                " concurrently; other user and OS activity is not controlled. A single build is an"
                " observation, not a timing distribution.\n\n"
            ),
        ]
        lines += [
            "=== Interpreting the native comparison\n\n",
            (
                "Every workload constructs a channel and timer before its loop, including the"
                " post-only loop. The initialized reactor remains part of that context. On this"
                " macOS/Boost combination, the native scheduler polls kqueue between posted"
                " completions. Therefore the ratios compare these initialized-reactor applications,"
                " not the intrinsic cost of an isolated coroutine or post. They must not be"
                " generalized to Linux, Windows, or a context that never initializes I/O. The"
                " native Cobalt runner also registers POSIX signals, unlike its WASI runner.\n\n"
            ),
        ]
        control_path = args.output / "native-reactor-control.json"
        if control_path.exists():
            control = json.loads(control_path.read_text())
            without = [r["ns_per_post"] for r in control["rows"] if not r["initialized_reactor"]]
            initialized = [
                r["ns_per_post"] / 1000 for r in control["rows"] if r["initialized_reactor"]
            ]
            lines += [
                "A separate diagnostic uses unmodified Boost and changes only whether it"
                " constructs an idle timer before 10,000 checked posts. Five pairs observed"
                f" {min(without):.1f}-{max(without):.1f} ns/post without that timer and"
                f" {min(initialized):.2f}-{max(initialized):.2f} us/post with it. This supports the"
                " initialized-reactor explanation for the large native post/channel ratios. It is"
                " a boundary check with fixed order and no discarded warmup, not a replacement"
                " statistical benchmark. Its source, command and output are retained as"
                " `native-reactor-control` files beside the results; the immutable source snapshot"
                " is `.cpp.txt`. Copy it to the recorded `.cpp` path before replaying that"
                " historical command. The main samples are unchanged.\n\n"
            ]
        sdk_diagnostics = [
            r
            for r in build_rows
            if any(
                w["kind"] == "sdk34-wasip3-lto-cabi-realloc-diagnostic"
                for w in r.get("warnings", [])
            )
        ]
        if sdk_diagnostics:
            lines += [
                "=== Versioned toolchain diagnostics\n\n",
                (
                    f"{len(sdk_diagnostics)} WASIp3 LTO build configurations report the SDK 34"
                    " linker diagnostic `function signature mismatch: cabi_realloc`. A minimal"
                    " malloc/printf program using only the SDK reproduces it and runs successfully"
                    " in Wasmtime. The warning is preserved in build receipts; all measured"
                    " payloads also pass their separate short execution checks. This does not"
                    " establish that every canonical-ABI path is safe with this SDK/LTO"
                    " combination. The primary runtime comparison uses no LTO. Native no-EH Cobalt"
                    " retains its upstream promise diagnostic and issue-287 warning; neither"
                    " warning is hidden or treated as proof that the affected move path is"
                    " safe.\n\n"
                ),
            ]
        for mode in ("baseline", "workload"):
            lines += [
                f"=== {mode.title()} payload (KiB)\n\n",
                '[cols="2,1,8*1",options="header"]\n|===\n|Profile |Library',
            ]
            options = [(opt, lto) for opt in ("O2", "O3", "Os", "Oz") for lto in ("off", "on")]
            lines += [
                "".join(" |" + opt + (" + LTO" if lto == "on" else "") for opt, lto in options)
                + "\n"
            ]
            for profile in build.profiles():
                for library in ("asio", "cobalt"):
                    selected = [
                        r
                        for r in sizes
                        if r["profile"] == profile["id"] and r["program"] == library + "_" + mode
                    ]
                    lines += [
                        "|"
                        + profile["id"]
                        + " |"
                        + library
                        + "".join(
                            " |"
                            + format(
                                next(
                                    r["payload_bytes"]
                                    for r in selected
                                    if (r["optimization"], r["lto"]) == (opt, lto)
                                )
                                / 1024,
                                ".2f",
                            )
                            for opt, lto in options
                        )
                        + "\n"
                    ]
            lines += ["|===\n\n"]
        lines += [
            "=== Runtime and memory scope\n\n",
            (
                "Each operation is one completed iteration: `post` resumes once; `immediate` sends"
                " then receives in a buffered Asio channel; `channel` transfers one value through"
                " an unbuffered channel; `parallel` joins one Asio-channel send/receive pair;"
                " `allocation` performs five posts and five arena allocations; `timer` awaits a"
                " zero-expiry timer; `http` parses one complete in-memory HTTP response. Asio uses"
                " `parallel_group` and Cobalt uses `join` for paired operations. HTTP does not"
                " measure network or TLS latency. Every iteration contributes to a checked"
                " checksum.\n\n"
            ),
            (
                "Thirty fresh executions per workload/profile use frozen common operation counts"
                " and an in-process warmup. The report records median, p95, min/max and a bootstrap"
                " 95% interval for the median. Ten baseline starts measure fresh process/page"
                " startup. The browser process is retained; each page/worker is new, with no-store"
                " HTTP. OS and engine caches are not cleared, so these are not cold-machine or"
                " empty-code-cache measurements. The explicit buffered loader measures fetch,"
                " compile and instantiate separately; it does not represent the default streaming"
                " loader.\n\n"
            ),
            (
                "Three instrumented executions describe {cpp} allocation counts/requested bytes and"
                " Cobalt frame PMR requests. Those nested counters must not be added. Direct C"
                " malloc and JavaScript allocations are outside their scope. Wasm linear memory is"
                " capacity, not live heap. Process RSS includes Node/Wasmtime; browser-module"
                " RSS/CPU are unavailable. Instrumentation timing is retained only as a diagnostic,"
                " not a comparative performance claim.\n\n"
            ),
        ]
        for library in ("asio", "cobalt"):
            lines += [
                f"=== {library.title()} median time per operation (ns)\n\n",
                (
                    '[cols="3,1,7*1",options="header"]\n|===\n|Profile |Host |Post |Immediate'
                    " |Channel |Parallel |Allocation |Timer |HTTP parse\n"
                ),
            ]
            keys = sorted({(m["profile"], m["host"]) for m in metrics if m["library"] == library})
            for profile, host in keys:
                selected = [
                    m
                    for m in metrics
                    if m["profile"] == profile
                    and m["host"] == host
                    and m["library"] == library
                    and m["mode"] == "workload"
                    and m["metric"] == "ns_per_operation"
                ]
                lines += [
                    "|"
                    + profile
                    + " |"
                    + host
                    + "".join(
                        " |"
                        + format(
                            next(
                                m["summary"]["median"]
                                for m in selected
                                if m["workload"] == workload
                            ),
                            ".1f",
                        )
                        for workload in (
                            "post",
                            "immediate",
                            "channel",
                            "parallel",
                            "allocation",
                            "timer",
                            "http",
                        )
                    )
                    + "\n"
                ]
            lines += ["|===\n\n"]
        lines += [
            "=== Baseline startup and instrumented allocation workload\n\n",
            (
                "Startup is the median of ten fresh starts. Process wall time includes harness"
                " setup, runtime launch and waiting; browser wall time begins inside a fresh"
                " page/worker, after its host page navigation, and includes buffered module"
                " loading. They are different observation boundaries. `n/a` means unavailable, not"
                " zero. Memory columns below use three instrumented allocation-workload runs."
                " Complete workload-specific memory distributions and separate JS/Wasm/native"
                " payload bytes are in the CSV files.\n\n"
            ),
            '[cols="3,1,1,1,1,1,1,1,1",options="header"]\n|===\n',
            (
                "|Profile |Host |Library |Start (ms) |{cpp} allocations |{cpp} peak (bytes) |Frame"
                " peak (bytes) |Linear capacity (KiB) |Process RSS (MiB)\n"
            ),
        ]

        def median_metric(profile, host, library, mode, workload, metric, divisor=1):
            found = [
                m
                for m in metrics
                if (m["profile"], m["host"], m["library"], m["mode"], m["workload"], m["metric"])
                == (profile, host, library, mode, workload, metric)
            ]
            if len(found) != 1 or found[0]["summary"] is None:
                return "n/a"
            return format(found[0]["summary"]["median"] / divisor, ".2f")

        contexts = sorted({(m["profile"], m["host"], m["library"]) for m in metrics})
        for profile, host, library in contexts:
            values = [median_metric(profile, host, library, "baseline", "none", "wall_ms")]
            values += [
                median_metric(profile, host, library, "memory", "allocation", metric, divisor)
                for metric, divisor in (
                    ("cpp_allocations", 1),
                    ("cpp_peak_bytes", 1),
                    ("cobalt_frame_peak_bytes", 1),
                    ("linear_memory_after", 1024),
                    ("peak_process_rss_bytes", 1024 * 1024),
                )
            ]
            lines += [
                "|"
                + profile
                + " |"
                + host
                + " |"
                + library
                + "".join(" |" + value for value in values)
                + "\n"
            ]
        lines += [
            "|===\n\n",
            "=== Buffered browser startup phases (ms)\n\n",
            (
                "Each column is an independent median of ten empty-runner observations. Fetch"
                " covers the Wasm response. Medians do not necessarily add; runtime-ready also"
                " includes JavaScript loading and setup.\n\n"
            ),
            '[cols="3,1,1,1,1,1,1",options="header"]\n|===\n',
            "|Profile |Host |Library |Fetch |Compile |Instantiate |Runtime ready\n",
        ]
        for profile, host, library in contexts:
            if host not in ("page", "worker"):
                continue
            values = [
                median_metric(profile, host, library, "baseline", "none", metric)
                for metric in ("fetch_ms", "compile_ms", "instantiate_ms", "runtime_ready_ms")
            ]
            lines += [
                "|"
                + profile
                + " |"
                + host
                + " |"
                + library
                + "".join(" |" + value for value in values)
                + "\n"
            ]
        lines += ["|===\n\n"]
        if args.data_path:
            lines += [
                "=== Complete evidence and plots\n\n",
                (
                    f"`{args.data_path}` contains `sizes.csv`, `metrics.csv`, `summary.json`,"
                    " environment metadata and the raw evidence archive with its SHA-256 file. The"
                    " archive retains commands, build logs, source/artifact hashes, calibration,"
                    " frozen counts and every independent sample. The local experiment is not"
                    " remote CI evidence.\n\n"
                ),
            ]
            for name, title in (
                ("asio-runtime-ratio", "Asio execution relative to native"),
                ("cobalt-runtime-ratio", "Cobalt execution relative to native"),
                ("asio-payload", "Asio Release payload"),
                ("cobalt-payload", "Cobalt Release payload"),
                ("compilation", "Observed compilation times"),
            ):
                lines += [
                    f".{title}\nimage::{{library-root}}/{args.data_path}/figures/{name}.svg"
                    f"[{title},opts=inline]\n\n"
                ]
        args.document.write_text("".join(lines))
    print(
        f"Validated {len(build_rows)} builds, {len(qualified)} payload checks and"
        f" {len(data)} runtime observations"
    )


if __name__ == "__main__":
    main()
