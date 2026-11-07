# Copyright (c) 2026 WebCpp.org
#
# Distributed under the Boost Software License, Version 1.0. (See
# accompanying file LICENSE_1_0.txt or copy at
# https://www.boost.org/LICENSE_1_0.txt)

"""Generate application support from exact local receipts and explicit source constraints."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
from typing import Iterator, Optional, TypedDict

HERE = Path(__file__).resolve().parent
PORTABLE = {"channel", "delay", "delay_op", "outcome", "signals"}
WORKERS = {"thread", "thread_pool", "spsc"}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def checked_log(path, expected):
    if digest(path) != expected:
        raise ValueError("Changed evidence: " + str(path))
    return path.read_text()


class Profile(TypedDict):
    id: str
    job: str
    target: str
    threads: str
    exceptions: str
    host: str
    blocker: Optional[str]


def profiles() -> Iterator[Profile]:
    for target in ("native", "wasip2", "wasip3", "jspi", "asyncify"):
        for threads in ("single", "multi"):
            for eh in ("on", "off"):
                blocked = (
                    "wasi-sdk 34 has no P2 pthread target"
                    if target == "wasip2" and threads == "multi"
                    else (
                        "wasi-sdk 34 has no cooperative P3 C++ exception runtime"
                        if target == "wasip3" and threads == "multi" and eh == "on"
                        else None
                    )
                )
                hosts = (
                    ("node", "page", "worker")
                    if target in ("jspi", "asyncify")
                    else ("native" if target == "native" else "wasmtime",)
                )
                for host in hosts:
                    yield Profile(
                        id=f"{target}-{threads}-{eh}-{host}",
                        job=f"cobalt-{target}-{threads}-{eh}",
                        target=target,
                        threads=threads,
                        exceptions=eh,
                        host=host,
                        blocker=blocked,
                    )


def benchmark_inventory(manifest, probes):
    rows = [
        row
        for row in json.loads((probes / "results.json").read_text())
        if row["kind"] == "benchmark"
    ]
    expected = {
        (p["job"], Path(b["path"]).stem)
        for p in profiles()
        if not p["blocker"]
        for b in manifest["benchmarks"]
    }
    if {(r["profile"], Path(r["source"]).stem) for r in rows} != expected or len(rows) != len(
        expected
    ):
        raise ValueError("Incomplete original benchmark compile inventory")
    result = []
    for row in rows:
        text = checked_log(probes / row["evidence"], row["log_sha256"])
        reasons = []
        if "format specifies type 'long'" in text:
            reasons.append(
                "The original printf format does not match chrono::duration::rep under strict"
                " format warnings."
            )
        if "no member named 'cobalt_compose'" in text:
            reasons.append(
                "The original names asio::cobalt_compose, absent in the pinned Asio interface."
            )
        if "cannot use 'throw' with exceptions disabled" in text:
            reasons.append("The original contains throw syntax in a no-EH profile.")
        if row["compile"] == "failed" and not reasons:
            raise ValueError("Unclassified original benchmark rejection: " + row["id"])
        result.append(
            dict(row, status="source-rejected" if reasons else "compile-only", reasons=reasons)
        )
    return result


def generate(evidence, probes):
    manifest = json.loads((HERE / "manifest.json").read_text())
    jobs = {}
    for path in evidence.glob("cobalt-*.json"):
        row = json.loads(path.read_text())
        if not isinstance(row, dict) or "command" not in row:
            continue
        if row["status"] != 0:
            raise ValueError("Conformance job failed: " + row["id"])
        text = checked_log(path.with_suffix(".log"), row["log_sha256"])
        source = evidence / row["source_manifest"]
        snapshot = json.loads(source.read_text())
        expected = (
            hashlib.sha256(json.dumps(snapshot, sort_keys=True).encode()).hexdigest()
            + ".sources.json"
        )
        if source.name != expected:
            raise ValueError("Changed implementation snapshot")
        jobs[row["id"]] = dict(receipt=row, text=text)
    if len(jobs) != 17:
        raise ValueError("Expected 17 feasible conformance jobs")
    compiled = {
        (row["profile"], Path(row["source"]).stem): row
        for row in json.loads((probes / "results.json").read_text())
        if row["kind"] == "application"
    }
    for row in compiled.values():
        checked_log(probes / row["evidence"], row["log_sha256"])
    browser = {
        (row["id"], row["host"]): row
        for row in json.loads((evidence / "browser-original-results.json").read_text())[
            "observations"
        ]
    }
    if len(browser) != 88:
        raise ValueError("Expected 44 namespace-only programs in both page and worker")
    for row in browser.values():
        if row["status"] != 0 or row["error"] is not None:
            raise ValueError("Browser application did not pass")
    noeh = {}
    python_noeh = json.loads((evidence / "python-original/noeh.json").read_text())
    python_diagnostics = checked_log(evidence / python_noeh["evidence"], python_noeh["log_sha256"])
    if (
        python_noeh["exit_code"] == 0
        or "cannot use 'throw' with exceptions disabled" not in python_diagnostics
    ):
        raise ValueError("Missing native Python no-EH rejection")
    for row in json.loads((evidence / "http-noeh.json").read_text()):
        text = checked_log(evidence / row["evidence"], row["log_sha256"])
        if row["status"] != "passed" or "PASS controlled http:" not in text:
            raise ValueError("No-EH HTTPS witness failed")
        noeh[row["profile"]] = row
    result = []
    for profile in profiles():
        for app in manifest["applications"]:
            name = app["id"]
            job = jobs.get(profile["job"], {})
            eh = profile["exceptions"]
            if not profile["blocker"] and not job:
                raise ValueError("Missing feasible profile receipt: " + profile["job"])
            native = profile["target"] == "native"
            browser_target = profile["target"] in ("jspi", "asyncify")
            runtime_browser = profile["host"] in ("page", "worker")
            row = dict(
                application=name,
                profile=profile["id"],
                original_sha256=app["sha256"],
                source_form="immutable-upstream" if native else "namespace-only-migration",
                status="not-measured",
                reason="",
                evidence=[],
                counterpart=None,
            )
            probe = compiled.get((profile["job"], name))
            if probe:
                row["compile_probe"] = {
                    k: probe[k] for k in ("source", "source_sha256", "compile", "evidence")
                }
            if profile["blocker"]:
                row.update(status="sdk-runtime-blocked", reason=profile["blocker"])
            elif name == "python":
                if native and eh == "on":
                    row.update(
                        status="upstream-defect",
                        reason=(
                            "Pinned Python script names the wrong module; the binding also has"
                            " reproduced generator/cancellation defects."
                        ),
                        evidence=[
                            "python-original/original-script.log",
                            "python-original/runtime.log",
                        ],
                        counterpart=dict(
                            status="passed",
                            scope=(
                                "Optional native corrected nanobind/CPython extension, six cases;"
                                " not a default library dependency."
                            ),
                            evidence=["python-adapted/runtime.log", "python-adapted/build.json"],
                        ),
                    )
                elif native:
                    row.update(
                        status="source-requires-exceptions",
                        reason=(
                            "The original binding and its nanobind dependency contain explicit"
                            " try/throw syntax."
                        ),
                        evidence=[python_noeh["evidence"]],
                    )
                else:
                    row.update(
                        status="dependency-unavailable",
                        reason="No packaged Wasm CPython/nanobind runtime.",
                    )
            elif browser_target and name in ("echo_server", "http", "ticker"):
                row.update(
                    status="platform-unavailable",
                    reason=(
                        "The original requires guest TCP/TLS. Browsers expose fetch/WebSocket, not"
                        " raw sockets or TCP listeners."
                    ),
                )
                if name in ("http", "ticker") and runtime_browser:
                    network = jobs[f'cobalt-jspi-{profile["threads"]}-{eh}']
                    variant = profile["target"]
                    found = False
                    for line in network["text"].splitlines():
                        try:
                            observation = json.loads(line)
                        except ValueError:
                            continue
                        if not isinstance(observation, dict):
                            continue
                        if (
                            observation.get("application") == f"cobalt-{name}-ssl"
                            and observation.get("backend") == variant
                            and observation.get("result") == "passed"
                            and observation.get("host") == profile["host"]
                            and observation.get("tls_fixture", {}).get(
                                "trust_and_hostname_verified"
                            )
                            is True
                        ):
                            found = True
                    if not found:
                        raise ValueError(
                            "Missing browser network observation: " + profile["id"] + "/" + name
                        )
                    row["counterpart"] = dict(
                        status="passed",
                        scope=(
                            "Browser HTTPS client"
                            if name == "http"
                            else "Browser WSS client and channel/JSON routing; no TCP listener"
                        ),
                        evidence=[network["receipt"]["id"] + ".log"],
                    )
            elif eh == "off" and app["explicit_exception_syntax"]:
                if not probe or probe["compile"] != "failed":
                    raise ValueError(
                        "Missing explicit-exception compile rejection: "
                        + profile["id"]
                        + "/"
                        + name
                    )
                row.update(
                    status="source-requires-exceptions",
                    reason=(
                        "The original contains try/catch/throw. This is an application source"
                        " constraint, not a Cobalt requirement."
                    ),
                    evidence=[probe["evidence"]],
                )
            elif name in WORKERS and not native and profile["threads"] == "single":
                row.update(
                    status="profile-unavailable",
                    reason="This source creates workers; this Wasm profile disables threads.",
                )
            elif name == "spsc":
                row.update(
                    status="upstream-defect",
                    reason=(
                        "Pinned queue can lose a wakeup between await_ready and await_suspend. The"
                        " deterministic original negative control is retained."
                    ),
                )
                if profile["threads"] == "multi" and profile["host"] != "worker" and job:
                    marker = "spsc-sequence"
                    if not any(marker in word for word in job["receipt"]["command"]):
                        raise ValueError("Missing SPSC counterpart command")
                    row["counterpart"] = dict(
                        status="passed",
                        scope=(
                            "Bounded queue with sequence verification and corrected wakeup; browser"
                            " UI additionally drives start/cancel."
                        ),
                        evidence=[profile["job"] + ".log"],
                    )
            elif name in PORTABLE or name in WORKERS:
                if runtime_browser:
                    observation = browser.get((profile["job"] + "-" + name, profile["host"]))
                    if observation is None:
                        raise ValueError("Missing browser original: " + profile["id"] + "/" + name)
                    row.update(
                        status="passed",
                        reason=(
                            "Namespace migration with normal exit and source/program hashes, actual"
                            " browser host."
                        ),
                        evidence=["browser-original-results.json"],
                    )
                else:
                    if (
                        f'PASS {"original" if native else "namespace-migration"} {name}:'
                        not in job["text"]
                    ):
                        raise ValueError(
                            "Missing source runtime witness: " + profile["id"] + "/" + name
                        )
                    row.update(
                        status="passed",
                        reason=(
                            "Original application algorithm and checked exit; Wasm changes"
                            " namespaces/includes only."
                        ),
                        evidence=[profile["job"] + ".log"],
                    )
            elif name in ("echo_server", "http", "ticker"):
                if name == "http" and eh == "off":
                    proof = noeh.get(profile["job"])
                    if proof is None:
                        raise ValueError("Missing no-EH HTTP proof")
                    witness = proof["evidence"]
                else:
                    marker = (
                        "PASS original echo: traffic"
                        if name == "echo_server"
                        else f"PASS controlled {name}:"
                    )
                    if marker not in job["text"]:
                        raise ValueError("Missing network witness: " + profile["id"] + "/" + name)
                    witness = profile["job"] + ".log"
                row.update(
                    status="adapted-passed",
                    source_form="controlled-endpoint-counterpart",
                    reason=(
                        "Loopback endpoint/port and verified CA/SNI fixture; exact protocol"
                        " payloads. Infinite listeners are stopped after traffic, not"
                        " graceful-shutdown evidence."
                    ),
                    evidence=[witness],
                )
            if row["status"] == "not-measured":
                raise ValueError("Unclassified application: " + str(row))
            if job:
                row["implementation_snapshot"] = job["receipt"]["source_manifest"]
            result.append(row)
    return dict(
        schema=1,
        upstream_commit=manifest["upstream_commit"],
        profiles=list(profiles()),
        applications=manifest["applications"],
        cells=result,
        benchmarks=benchmark_inventory(manifest, probes),
        counts=dict(Counter(r["status"] for r in result)),
        scope=(
            "Application compatibility. A counterpart pass does not relabel the original source or"
            " prove every error path."
        ),
    )


def document(data):
    lines = [
        (
            "// Copyright (c) 2026 WebCpp.org\n//\n// Distributed under the Boost Software License,"
            " Version 1.0. (See\n// accompanying file LICENSE_1_0.txt or copy at\n//"
            " https://www.boost.org/LICENSE_1_0.txt)\n"
        ),
        "// Generated by test/applications/matrix.py. Do not edit.\n",
        "[[cobalt-application-matrix]]\n== Original applications and environment counterparts\n\n",
        (
            "The inventory pins all twelve Cobalt 1.92 applications. Native core types are Boost"
            " types; Wasm uses the shipped WebCPP backend. Every cell below names the actual"
            " target, thread/exception setting and host. Browser page and dedicated worker"
            " observations are separate from Node.\n\n"
        ),
        (
            "A controlled network counterpart changes endpoints, ports and certificate setup. It"
            " retains the application protocol. Infinite original servers are stopped after"
            " observed traffic; that does not prove graceful shutdown. Browser HTTP/WSS"
            " counterparts use host transports and do not provide raw TCP listeners.\n\n"
        ),
        (
            "Original examples run in Release, so an original `assert` may be disabled by `NDEBUG`."
            " A normal exit is not evidence for an assertion that did not execute. Separate finite"
            " portable counterparts check values, cancellation and completion explicitly in the"
            " same feasible profiles.\n\n"
        ),
        (
            "`source-requires-exceptions` means the original application contains exception syntax."
            " It does not mean Cobalt itself requires exceptions. Native no-EH Clang builds retain"
            " the documented upstream promise warning; only that diagnostic is nonfatal in these"
            " test consumers. P3 multi means cooperative threads, not CPU parallelism.\n\n"
        ),
    ]
    names = [app["id"] for app in data["applications"]]
    for target in ("native", "wasip2", "wasip3", "jspi", "asyncify"):
        lines += [
            f"=== {target}\n\n",
            '[cols="3,12*1",options="header"]\n|===\n|Profile'
            + "".join(" |" + name for name in names)
            + "\n",
        ]
        for profile in data["profiles"]:
            if profile["target"] != target:
                continue
            cells = {r["application"]: r for r in data["cells"] if r["profile"] == profile["id"]}
            labels = {
                "passed": "pass",
                "adapted-passed": "fixture",
                "sdk-runtime-blocked": "SDK",
                "dependency-unavailable": "dependency",
                "platform-unavailable": "platform",
                "source-requires-exceptions": "source EH",
                "profile-unavailable": "threads",
                "upstream-defect": "upstream defect",
            }
            lines += [
                "|"
                + profile["id"]
                + "".join(
                    " |"
                    + labels[cells[name]["status"]]
                    + (" + counterpart" if cells[name]["counterpart"] else "")
                    for name in names
                )
                + "\n"
            ]
        lines += ["|===\n\n"]
    lines += [
        "=== Evidence boundaries\n\n",
        "* `pass`: the original or namespace-only migrated program ran and completed.\n",
        "* `fixture`: a controlled endpoint adaptation ran against real TCP/TLS peers.\n",
        (
            "* `SDK`, `platform`, `threads`, `dependency`: the exact prerequisite is absent in that"
            " profile.\n"
        ),
        (
            "* `source EH`: explicit exception syntax in the example is rejected with exceptions"
            " disabled.\n"
        ),
        (
            "* `upstream defect`: the original failure has its own evidence; a corrected"
            " counterpart stays separately identified.\n\n"
        ),
        (
            "Raw JSON cells retain source hashes, implementation snapshots, commands and evidence"
            " filenames. The native Python extension is an optional CPython 3.14/nanobind"
            " experiment, not a Wasm capability. The five upstream benchmark sources remain"
            " inventoried and compile-probed; current performance uses separately identified finite"
            " matched workloads.\n"
        ),
    ]
    lines += [
        "\n=== Pinned upstream benchmark sources\n\n",
        (
            "All five benchmark sources are compiled with the measured profiles and strict"
            " diagnostics. These are compile probes, not performance results. The original sources"
            " remain unchanged; Wasm variants change namespaces/includes and make the original"
            " concurrency-hint macro include explicit.\n\n"
        ),
        '[cols="1,3",options="header"]\n|===\n|Source |Observed source constraints\n',
    ]
    for name in sorted({Path(r["source"]).stem for r in data["benchmarks"]}):
        reasons = sorted(
            {
                reason
                for row in data["benchmarks"]
                if Path(row["source"]).stem == name
                for reason in row["reasons"]
            }
        )
        lines += ["|" + name + " |" + " ".join(reasons) + "\n"]
    lines += ["|===\n"]
    return "".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--probes", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--document", type=Path)
    args = parser.parse_args()
    data = generate(args.evidence, args.probes)
    args.output.write_text(json.dumps(data, indent=2) + "\n")
    if args.document:
        args.document.write_text(document(data))
    print(json.dumps(data["counts"], sort_keys=True))


if __name__ == "__main__":
    main()
