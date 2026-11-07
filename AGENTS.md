# Working on cobalt

Read the superproject's [AGENTS.md](../../AGENTS.md) and
[CODING-GUIDELINES.md](../../CODING-GUIDELINES.md) first. This library is the
independent wrapper and compiled integration of Boost.Cobalt 1.92.0, upstream
commit `8b1cecba91d0e868ee0947b40fb04e3594680297`.

- Owner direction on 2026-11-06 supersedes the original namespace/overlay design:
  native public types must be the actual Boost types, while Wasm adaptations are
  shipped explicitly and selected by configuration. Read the superproject's
  `docs/superpowers/specs/2026-11-06-portable-network-libraries.md` and matching plan.
  Native identity, full compiled-core/runner selection and unpacked-source
  direct/CMake consumers have separate regression witnesses. Keep all of them:
  a header-only identity witness does not establish source-distribution support.
- Cobalt depends on Asio. Asio's core must never depend on Cobalt. The old Asio
  and root Cobalt target names are compatibility aliases to this library.
- Preserve correction provenance and regression witnesses while moving existing
  Wasm corrections into shipped sources. The former overlays are historical
  migration evidence, not a consumer build path. Native builds must not silently
  acquire those corrections. Never patch the installed Boost tree.
- Preserve Asio's configurable default of 100 handlers per browser turn. The
  runner and the checked Emscripten run(task) helper use webcpp::asio::io_context;
  both paths need separate evidence. Native/WASI run(task) stays upstream.
- Preserve the nonfatal no-exceptions warning for
  https://github.com/boostorg/cobalt/issues/287. Do not ban all no-exceptions code
  or claim every pending-task move is safe merely because a timer test passed.
- TLS reuses the shared source-built OpenSSL and CA provider. Cobalt must not
  acquire a second provider or link optional TLS into its plain core.
- `test/tls//tls-integration` owns the recurring native/WASIp3 SSL client,
  state and cancellation fixtures. Build their Cobalt sources directly with the
  shared TLS peer driver; registering their compatibility aliases in another
  library's own lane makes the report reject that lane. Preserve native S/M
  with EH on/off, WASIp3 S with EH on/off and cooperative M with EH off, plus
  MSVC's static CRT requirement for S. The separate `tls` lane retains P2.
- On 2026-11-06 the owner approved one native identity exception after the
  original SSL wrapper failed state, shutdown and post-cancellation write tests:
  `webcpp::cobalt::io::ssl` uses WebCPP's corrected wrapper on native and WASI.
  Keep native coroutine/plain I/O types and the compiled core/runner upstream.
  Ship the native SSL header/source explicitly, without changing Boost files.
- Preserve original example sources and their hashes under test/applications.
  Classify every example and benchmark. Adaptations have separate identities.
- Matrix cells require exact source, target, runtime version, EH, threading and
  suspension evidence. Node is not browser evidence, cooperative threads are not
  CPU parallelism, and an unavailable measurement is never zero.
- Documentation follows the project's webcpp-documentation skill: Diataxis,
  AsciiDoc, MrDocs, compiled listings and generated Release measurements.
