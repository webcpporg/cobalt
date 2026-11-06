# Working on cobalt

Read the superproject's [AGENTS.md](../../AGENTS.md) and
[CODING-GUIDELINES.md](../../CODING-GUIDELINES.md) first. This library is the
independent wrapper and compiled integration of Boost.Cobalt 1.92.0, upstream
commit `8b1cecba91d0e868ee0947b40fb04e3594680297`.

- Keep Boost.Cobalt's namespaces, coroutine types and signatures. WebCPP owns
  configuration, the runner, checked corrections, build integration and evidence.
- Cobalt depends on Asio. Asio's core must never depend on Cobalt. The old Asio
  and root Cobalt target names are compatibility aliases to this library.
- Preserve the 12 imported header corrections, the additional run.hpp,
  detail/detached.hpp and with.hpp corrections, and three source corrections with
  before/after hashes. Never patch the installed Boost tree. Upgrade only after
  source identity and regression checks pass.
- Preserve Asio's configurable default of 100 handlers per browser turn. The
  runner and the checked Emscripten run(task) helper use webcpp::asio::io_context;
  both paths need separate evidence. Native/WASI run(task) stays upstream.
- Preserve the nonfatal no-exceptions warning for
  https://github.com/boostorg/cobalt/issues/287. Do not ban all no-exceptions code
  or claim every pending-task move is safe merely because a timer test passed.
- TLS reuses the shared source-built OpenSSL and CA provider. Cobalt must not
  acquire a second provider or link optional TLS into its plain core.
- Preserve original example sources and their hashes under test/applications.
  Classify every example and benchmark. Adaptations have separate identities.
- Matrix cells require exact source, target, runtime version, EH, threading and
  suspension evidence. Node is not browser evidence, cooperative threads are not
  CPU parallelism, and an unavailable measurement is never zero.
- Documentation follows the project's webcpp-documentation skill: Diataxis,
  AsciiDoc, MrDocs, compiled listings and generated Release measurements.
