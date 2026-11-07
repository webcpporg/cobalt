# webcpp::cobalt

WebCPP Cobalt makes Boost-style coroutine applications usable in WebAssembly:
browsers through Emscripten, and WASIp2/P3 components through runtimes such as
Wasmtime. JSPI and Asyncify are Emscripten suspension variants. Exception mode,
thread mode and runtime capabilities are separate choices in the support matrix.

On native targets, the core coroutine and plain I/O types have the identity of
the original Boost.Cobalt types. The compiled core and runner use unmodified
Boost 1.92 sources. One explicit exception is `webcpp::cobalt::io::ssl`: its
corrected state machine is shipped by WebCPP on native and WASI targets because
the upstream wrapper failed the handshake, cancellation and shutdown regression
tests. It is a separate type from `boost::cobalt::io::ssl::stream`.

The Wasm backend is shipped under library-owned paths and namespaces. B2,
direct-compiler and CMake consumers compile those files; they never apply a patch
or place replacement Boost headers before an installed Boost. The dated application matrix and Release measurements describe the shipped-source
candidate. Local lint and review are complete; coordinated remote CI remains
the publication gate.

This library was extracted from the existing Asio integration under the
[accepted proposal](https://github.com/webcpporg/proposals/tree/main/libraries/cobalt/openspec/changes/genesis).
The independent target has its own [application matrix](doc/application-matrix.adoc),
[Release measurements](doc/release-measurements.adoc), and
[configuration guide](doc/configuration.adoc). Existing Asio results alone do not
certify Cobalt applications; each report names its source and runtime evidence.

Develop this repository inside `libs/cobalt` of the WebCPP superproject. Its
`Jamroot` supplies Boost 1.92 and the configured native, Emscripten or WASI toolchain.
Canonical targets are `/webcpp/cobalt//core` for custom main,
`/webcpp/cobalt//cobalt` for co_main, and optional `io_ssl` and `cobalt_ssl`.

See [AGENTS.md](AGENTS.md) for integration constraints and the shared documentation
workflow. Licensed under the Boost Software License 1.0. Upstream-derived examples,
patches and runner code preserve their original notices.
