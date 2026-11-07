# Reproducible Release experiments

These manual experiments are not conformance tests and are not CI lanes. They
compare the shipped Asio and Cobalt interfaces on the same machine. The native
interfaces use Boost 1.92; Wasm uses the explicit WebCPP backend. The HTTP workload
parses an identical Beast response in memory. It does not measure network latency.

Build the empty runner, workload and instrumented workload separately. Every
translation unit has a distinct B2 object identity for library/instrumentation;
never share an object compiled with different macros. Build receipts verify the
actual compiler macros and the bitcode signature of LTO objects. All programs use
Release, NDEBUG, and one of O2/O3/Os/Oz, each with and without full LTO. Artifact
sizes include baseline and matched workload; memory instrumentation has its own
artifact and must not inflate the published uninstrumented payload.
Payloads exclude runtime installations and native system shared libraries. A
native executable and a Wasm module therefore have different runtime boundaries.
Profile audits also verify exception, threading, WASI sysroot and suspension
flags. With this toolchain, EH-on uses `-fwasm-exceptions` for JSPI/WASI and
`-fexceptions` for Asyncify. EH-on comparisons include that exception code
generation difference; they do not isolate suspension alone.

The workload is a separately identified finite counterpart, not an unchanged
upstream benchmark. It retains post, immediate channel, rendezvous channel,
parallel send/receive, arena allocation and timer operations, plus HTTP parsing.
The same operation counts and checksums apply to both libraries. Cobalt join and
Asio parallel_group express the same completed send/receive pair. Every result
must equal N*(N+1)/2, including Release builds. Each process/page warms the same
workload before measurement. Calibration chooses bounded counts once; the frozen
workload file must precede recorded samples. Compilation and other test suites
must finish before timing begins.

Every workload constructs a channel and timer before its loop. On macOS this
initializes Boost's kqueue reactor, which is polled between posted completions.
The native Cobalt runner also registers POSIX signals. The measurements compare
these initialized-reactor applications across environments, not isolated
coroutine costs. The recorded `native-reactor-control` diagnostic changes only
the presence of an idle timer in an unmodified Boost post loop and retains the
source, command and results. Do not generalize the macOS ratios to other native
reactors or contexts that never initialize I/O.

Record 30 independent uninstrumented executions per workload and profile, ten
fresh-process/page baseline starts and three descriptive allocation observations.
OS and engine caches are not cleared. The browser process is retained across
fresh pages/workers; these startup observations do not claim a cold machine or
an empty engine code cache. The buffered fetch interval covers the Wasm response,
while total startup also includes JavaScript loading and runtime setup.
Process wall time includes parent harness setup, child launch and waiting. The
inner workload clock and attributed child CPU counters have narrower boundaries.
Runtime comparisons select O3 without LTO across every feasible profile; the
size/build-time matrix covers all eight optimization/LTO settings. Shuffle with
a recorded seed. Report
median, p95, min/max, sample count and a bootstrap 95% confidence interval for
the median. A p95 with 30 observations is descriptive, not a tail guarantee.
Retain failures separately and never substitute a missing measurement with zero.

The instrumentation build intercepts C++ new/delete (including aligned and
nothrow forms), with live/peak requested payload bytes. An executed ordinary and
aligned allocation control verifies the hooks before sampling. It excludes
bookkeeping, direct C malloc, JS objects and runtime internals. Cobalt frame PMR
requests are counted separately through the current resource, retaining its
upstream pool. Do not add those requested bytes to backing new/delete bytes:
those are nested accounting boundaries. Instrumented timing is retained only
as a diagnostic; three observations do not support a performance comparison.

Wasm linear memory reports allocated pages before/after; it is not live heap.
Memory cannot shrink, so the final size is also the observed high-water size of
that execution. Native linear memory is unavailable. Fresh native, Node and
Wasmtime child-process RSS/CPU include their runtime and must name that process.
Browser module RSS/CPU are unavailable when the browser cannot attribute them.
Do not replace them with the whole shared browser's memory. Dedicated worker
results remain distinct from main-page results; P3 cooperative threads are not
parallel CPU execution.

Single/multi are B2 threading properties. Native single does not disable Boost
threads; Wasm multi selects the thread-capable SDK profile. Each workload runs
one context on one execution thread. These are configuration-cost comparisons,
not multicore throughput or worker-pool scaling measurements.

The existing Asio yield experiment measures suspension, host responsiveness,
clock controls and turn-limit sweeps independently. Preserve its historical
results and attach fresh source/compiler/runtime identities to the repeated run.

## Run and verify

Use the superproject's configured B2, all required SDKs and a current Python
accepted by Emscripten. Set `EMSDK_PYTHON` explicitly when the system Python is
older than the SDK requires. Run conformance before measurement and stop other
agent builds/tests during these steps:

```sh
python3 -B libs/cobalt/bench/build.py --b2 b2 --output measured/builds \
  --build-root bin/measured --compilation-context isolated
python3 -B libs/cobalt/bench/qualify.py --builds measured/builds
python3 -B libs/cobalt/bench/experiment.py calibrate \
  --builds measured/builds --output measured/runtime
node libs/cobalt/bench/browser.mjs measured/runtime/calibration-browser-cases.json \
  measured/runtime/calibration-browser.jsonl
python3 -B libs/cobalt/bench/experiment.py prepare \
  --builds measured/builds --output measured/runtime
python3 -B libs/cobalt/bench/experiment.py process \
  --builds measured/builds --output measured/runtime
node libs/cobalt/bench/browser.mjs measured/runtime/browser-cases.json \
  measured/runtime/browser-samples.jsonl
python3 -B libs/cobalt/bench/analyze.py --builds measured/builds \
  --experiment measured/runtime --output measured/results
python3 -B libs/cobalt/bench/plot.py measured/results/summary.json measured/figures
```

The analyzer requires all 136 builds, 2,176 short payload checks across every
optimization/LTO setting, and all 15,906 final runtime observations. Payload
checks run in native, Node or Wasmtime and are excluded from timing statistics.
It binds runtime cases to the O3/no-LTO receipts and rejects omitted profiles,
changed artifacts, mislabeled workloads and smoke samples. Source snapshots
include `.ipp` implementations and build configuration. The plotting tool needs
optional Matplotlib/NumPy; it only renders validated summary data. `archive.py`
preserves text evidence with a SHA-256 manifest and excludes executable binaries.

## Recorded evidence

`results/2026-11-07/` retains the current generated tables, environment, frozen
workload counts, figures and `raw-evidence.tar.gz` with its SHA-256 file. The
archive contains build receipts/logs, implementation snapshots, qualification,
calibration and independent observations. `doc/release-measurements.adoc` is
generated by `analyze.py --document` with `--data-path bench/results/2026-11-07`.
Do not edit the report or its observations by hand.

Application conformance is a separate record under
`test/applications/results/2026-11-07/`, with matrices for Asio guide examples,
Cobalt applications and Beast applications. Its raw archive retains the local
commands and outcomes. Asio's independent browser-handoff experiment is under
`libs/asio/bench/yield/results/2026-11-07/` in the superproject.

The dated `native-reactor-control.cpp.txt` is an immutable source snapshot, not a
maintained build target. Its SHA-256 matches the diagnostic JSON and archived
source. Copy it to the `.cpp` path recorded in that JSON before replaying its
compile command. Later lint/format changes do not rewrite any measured source
snapshot or relabel the dated measurements as a newer candidate run.
