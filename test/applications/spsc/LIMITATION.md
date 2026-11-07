# SPSC delayed-notification limitation

Observed on 2026-11-07. This is an inherited defect in the pinned Boost.Cobalt
SPSC example and its existing WebCPP counterpart. It is not a defect in
Boost.Lockfree storage or the Cobalt coroutine core. Neither queue nor the
application is changed by this classification.

## End-user failure

[CI run 42381776828](https://github.com/webcpporg/webcpp/actions/runs/42381776828),
superproject `861809f`, failed `native.cobalt.spsc-sequence` with Clang 18 on Linux.
The requested 10,000 values produced 9,999 values and checksum 49,989,969 rather
than 49,995,000. Value 5,031 was missing: the first mismatch was 5,032 versus
5,031, followed by 4,970 failed sequence/count/checksum checks. The application's
existing assertions exposed the loss and remain unchanged.

A fresh native macOS run passed. That observation does not disprove the race
or supersede the failed CI result.
The recurring probe therefore forces the interleaving rather than relying on
probability, reduced workload, retries or a nondeterministic `run-fail` result.

## Mechanism

For a queue with one slot:

1. The consumer pops value 0, freeing the slot, but has not notified the writer.
2. The producer writes value 1 into that slot.
3. Its next write, value 2, suspends because the queue is full.
4. The old consumer notification now resumes that waiting write.
5. `write_op::await_resume` attempts `push(2)` without rechecking capacity. The
   real `push` returns false, the return is ignored, and the producer completes.
6. The consumer receives value 1, but value 2 never entered the queue.

The prior wakeup adaptation closes the separate gap between `await_ready` and
publication in `await_suspend`. It does not prevent this older notification from
waking a newer wait. The `spsc-wakeup` lane retains only that narrower claim.

## Deterministic evidence

`sequence_limitation.cpp` supplies a test value specialization that delegates
storage and operation results to a real `boost::lockfree::spsc_queue<int>`.
Its only instrumentation invokes the producer after the real pop has freed the
slot and before the unchanged awaitable queue sends its notification. The
producer is a real coroutine and its continuation uses the real executor.
There are no fabricated readiness results, sleeps, blocking joins or timing
assumptions. A separate diagnostic using two actual native threads reproduced
the same loss in both the immutable original and the unchanged counterpart.

`spsc-sequence` now runs `original_sequence_limitation` on native and
`sequence_limitation` on native, Emscripten and WASIp3. Their distinct
`UPSTREAM-LIMITATION original` and `UPSTREAM-LIMITATION counterpart` output means
the known loss was reproduced, not that reliable transfer passed. The driver
requires successful assertions and the exact marker, with a 25-second child
deadline and 45-second outer bound. It also runs the matrix policy regressions.

The forced schedule passes on native with exceptions on/off, JSPI and Asyncify
with exceptions on/off, and cooperative WASIp3 with exceptions off. WASIp3's
forced schedule documents the inherited protocol; it does not establish that
its cooperative scheduler naturally preempts at this boundary. The untouched
original is exercised only natively, where its Boost types match the executor.

The browser workbench disables the unreliable example, rejects programmatic
submission, and creates no runtime. Real Chrome checks cover both suspension
mechanisms and exception choices, wide and narrow layouts, and confirm the
remaining portable examples still run. These checks establish honest UI
availability, not SPSC transfer support.

## Historical identity

These files remain byte-identical to the diagnosed revision. Paths are relative
to the Cobalt library:

| File | SHA-256 |
| --- | --- |
| `test/applications/upstream/example/spsc.cpp` | `a3d8c7f084ebcad150544bb318199bdafb259496c4e7498a0253d444e0317dab` |
| `test/applications/spsc/original_queue.hpp` | `6b68a18a326bd54cf7f60220c7aa03b4b906e2b0e0026ef803fa0ada81fd2af0` |
| `example/applications/spsc_queue.hpp` | `251e760d19d42f5f921e7a5dafcb3dc0a22cb4825a740e593504c52f9948d027` |
| `example/applications/spsc.cpp` | `ac55b234b51a795a0518edffeebc4cb5b1cac034558935649975cfed4fdf057f` |
| `test/applications/results/2026-11-07/cobalt-application-matrix.json` | `e8365c9277f11f96e5c87906eeee3abc9b5dc10286c88e02f122c023b6150821` |

`matrix.py --historical` applies the current limitation to a separate output,
retains each dated counterpart observation as `historical_counterpart`, and
records the input report's digest. Neither JSON output nor document output may
overwrite that historical input. The generated current page labels the SPSC
counterpart unavailable without relabeling the immutable historical report.
