# webcpp::cobalt

Boost.Cobalt coroutine applications on native systems, browsers through Emscripten,
and WASIp2/P3, using WebCPP's Asio integration. The upstream Boost.Cobalt namespace,
headers and coroutine types remain available.

This library is being extracted from the existing Asio integration under the
[accepted proposal](https://github.com/webcpporg/proposals/tree/main/libraries/cobalt/openspec/changes/genesis).
The independent target's application matrix and documentation are in development;
existing Asio results do not certify all Cobalt applications.

Develop this repository inside `libs/cobalt` of the WebCPP superproject. Its
`Jamroot` supplies Boost 1.92 and the configured native, Emscripten or WASI toolchain.
Canonical targets are `/webcpp/cobalt//core` for custom main,
`/webcpp/cobalt//cobalt` for co_main, and optional `io_ssl` and `cobalt_ssl`.

See [AGENTS.md](AGENTS.md) for integration constraints and the shared documentation
workflow. Licensed under the Boost Software License 1.0. Upstream-derived examples,
patches and runner code preserve their original notices.
