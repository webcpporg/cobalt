// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

/** The configuration and backend capabilities of the Cobalt integration. */
#ifndef WEBCPP_COBALT_CONFIG_HPP
#define WEBCPP_COBALT_CONFIG_HPP

#include <webcpp/config.hpp>

#include <webcpp/asio/config.hpp>

#include <boost/version.hpp>

// Native compiled sources and the Wasm import share this exact audited baseline.
// Check here so direct compiler builds receive the same diagnostic as B2/CMake.
#if BOOST_VERSION != 109200
#error "webcpp::cobalt requires Boost 1.92.0 headers matching its shipped sources"
#endif

// Preserve Boost's build-wide configuration vocabulary. Native source files
// read these controls directly; Wasm maps them before its private config loads.
// Every application and compiled source must receive the same definitions.
#if defined(BOOST_COBALT_USE_IO_CONTEXT) && defined(BOOST_COBALT_CUSTOM_EXECUTOR)
#error "webcpp::cobalt: select either the io_context executor or a custom executor"
#endif
#if (defined(BOOST_COBALT_USE_STD_PMR) + defined(BOOST_COBALT_USE_BOOST_CONTAINER_PMR) + \
     defined(BOOST_COBALT_USE_CUSTOM_PMR) + defined(BOOST_COBALT_NO_PMR)) > 1
#error "webcpp::cobalt: select exactly one PMR policy"
#endif
#ifndef WEBCPP_TARGET_NATIVE
#ifdef BOOST_COBALT_USE_IO_CONTEXT
#define WEBCPP_COBALT_BACKEND_USE_IO_CONTEXT BOOST_COBALT_USE_IO_CONTEXT
#endif
#ifdef BOOST_COBALT_CUSTOM_EXECUTOR
#define WEBCPP_COBALT_BACKEND_CUSTOM_EXECUTOR BOOST_COBALT_CUSTOM_EXECUTOR

namespace webcpp::cobalt {
using executor = ::boost::cobalt::executor;
}
#endif
#ifdef BOOST_COBALT_USE_STD_PMR
#define WEBCPP_COBALT_BACKEND_USE_STD_PMR BOOST_COBALT_USE_STD_PMR
#endif
#ifdef BOOST_COBALT_USE_BOOST_CONTAINER_PMR
#define WEBCPP_COBALT_BACKEND_USE_BOOST_CONTAINER_PMR BOOST_COBALT_USE_BOOST_CONTAINER_PMR
#endif
#ifdef BOOST_COBALT_USE_CUSTOM_PMR
#define WEBCPP_COBALT_BACKEND_USE_CUSTOM_PMR BOOST_COBALT_USE_CUSTOM_PMR

namespace webcpp::cobalt {
namespace pmr = ::boost::cobalt::pmr;
}
#endif
#ifdef BOOST_COBALT_NO_PMR
#define WEBCPP_COBALT_BACKEND_NO_PMR BOOST_COBALT_NO_PMR
#endif
#endif

#ifdef WEBCPP_TARGET_NATIVE
/**
 Iterates an asynchronous range with the selected coroutine backend.

 Include `webcpp/cobalt/async_for.hpp` before using this macro. The arguments
 follow Boost.Cobalt's `BOOST_COBALT_FOR` syntax on every target.

 @param Value The declaration receiving each yielded value.
 @param Expression The asynchronous range expression.
*/
#define WEBCPP_COBALT_FOR(Value, Expression) BOOST_COBALT_FOR(Value, Expression)
#else
/**
 Iterates an asynchronous range with the selected coroutine backend.

 Include `webcpp/cobalt/async_for.hpp` before using this macro.

 @param Value The declaration receiving each yielded value.
 @param Expression The asynchronous range expression.
*/
#define WEBCPP_COBALT_FOR(Value, Expression) WEBCPP_COBALT_BACKEND_FOR(Value, Expression)
#endif

#ifdef WEBCPP_TARGET_NATIVE
namespace boost::cobalt::io {}

/** Native coroutine types retain Boost identity; the SSL wrapper is corrected separately. */
namespace webcpp::cobalt {
using namespace ::boost::cobalt;

namespace io {
using namespace ::boost::cobalt::io;
}
}  // namespace webcpp::cobalt
#else
namespace boost {}

namespace webcpp::cobalt {
using namespace ::boost;
}

// The Wasm library is compiled explicitly; it never asks a platform linker to
// auto-select a separately built Boost.Cobalt archive.
#define WEBCPP_COBALT_BACKEND_NO_LIB 1
#endif

#ifdef __MRDOCS__
/**
 Defined when WebCPP's Cobalt adapters are configured without exceptions.

 The compiler and B2 exception-handling setting still determine the exception
 ABI of Boost.Cobalt and its compiled sources. This macro does not repair the
 pending-task move limitation in Boost.Cobalt issue 287 or change Boost APIs.
 Define WEBCPP_NO_EXCEPTIONS to select the policy for the whole collection, or
 this macro to select it for this integration alone.

 @see "Exceptions and errors", in the guide.
*/
#define WEBCPP_COBALT_NO_EXCEPTIONS
#undef WEBCPP_COBALT_NO_EXCEPTIONS
#endif

#if !defined(WEBCPP_HAS_EXCEPTIONS) && !defined(WEBCPP_COBALT_NO_EXCEPTIONS)
#define WEBCPP_COBALT_NO_EXCEPTIONS
#endif

// Derived observations, never switches that enable missing backend facilities.
#if defined(WEBCPP_COBALT_HAS_SOCKET_BACKEND) || defined(WEBCPP_COBALT_HAS_OPENSSL_BACKEND) || \
    defined(WEBCPP_COBALT_HAS_BROWSER_BRIDGE) || defined(WEBCPP_COBALT_HAS_THREADS) ||         \
    defined(WEBCPP_COBALT_HAS_BROWSER_WORKERS) || defined(WEBCPP_COBALT_HAS_LOCAL_SOCKETS) ||  \
    defined(WEBCPP_COBALT_HAS_SIGNALS) || defined(WEBCPP_COBALT_HAS_SERIAL_PORT) ||            \
    defined(WEBCPP_COBALT_HAS_HOST_DNS) || defined(WEBCPP_COBALT_HAS_REVERSE_DNS) ||           \
    defined(WEBCPP_COBALT_HAS_PIPE_CREATION) || defined(WEBCPP_COBALT_HAS_POSIX_STDIO) ||      \
    defined(WEBCPP_COBALT_HAS_POLLABLE_STDIO)
#error \
    "webcpp::cobalt: capability macros are derived by config.hpp; do not define WEBCPP_COBALT_HAS_*"
#else

/** 1 when the configured Asio backend provides native or WASI TCP/UDP; host grants remain runtime
 * requirements. */
#define WEBCPP_COBALT_HAS_SOCKET_BACKEND WEBCPP_ASIO_HAS_SOCKET_BACKEND

/** 1 when the target can link guest OpenSSL; select io_ssl or cobalt_ssl separately. */
#define WEBCPP_COBALT_HAS_OPENSSL_BACKEND WEBCPP_ASIO_HAS_OPENSSL_BACKEND

/** 1 when JavaScript Promises and events can be awaited through the Asio browser bridge. */
#define WEBCPP_COBALT_HAS_BROWSER_BRIDGE WEBCPP_ASIO_HAS_BROWSER_BRIDGE

/** 1 when the configured executor supports threads; WASIp3 cooperative threads are not CPU
 * parallelism. */
#define WEBCPP_COBALT_HAS_THREADS WEBCPP_ASIO_HAS_THREADS

/** 1 for an Emscripten pthread configuration; browser isolation and worker hosting are runtime
 * requirements. */
#define WEBCPP_COBALT_HAS_BROWSER_WORKERS WEBCPP_ASIO_HAS_BROWSER_WORKERS

/** 1 when native Boost.Asio supplies local sockets; WASI and browser transports do not impersonate
 * Unix sockets. */
#define WEBCPP_COBALT_HAS_LOCAL_SOCKETS WEBCPP_ASIO_HAS_LOCAL_SOCKETS

/** 1 when native process-signal delivery is available; application cancellation remains available
 * elsewhere. */
#define WEBCPP_COBALT_HAS_SIGNALS WEBCPP_ASIO_HAS_SIGNALS

/** 1 when the native backend supplies serial ports; a browser or WASI host adapter is a separate
 * facility. */
#define WEBCPP_COBALT_HAS_SERIAL_PORT WEBCPP_ASIO_HAS_SERIAL_PORT

/** 1 when the backend provides forward lookup; the host can still deny network or DNS capabilities.
 */
#define WEBCPP_COBALT_HAS_HOST_DNS WEBCPP_ASIO_HAS_HOST_DNS

/** 1 when native reverse lookup is available; WASI address lookup is forward-only. */
#define WEBCPP_COBALT_HAS_REVERSE_DNS WEBCPP_ASIO_HAS_REVERSE_DNS

#if (defined(WEBCPP_TARGET_NATIVE) && defined(BOOST_ASIO_HAS_PIPE)) || defined(WEBCPP_TARGET_WASIP3)
/** 1 when guest-created asynchronous pipes support transfer, cancellation and EOF. */
#define WEBCPP_COBALT_HAS_PIPE_CREATION 1
#else
/** 1 when guest-created asynchronous pipes support transfer, cancellation and EOF. */
#define WEBCPP_COBALT_HAS_PIPE_CREATION 0
#endif

#if defined(WEBCPP_TARGET_NATIVE) && defined(BOOST_ASIO_HAS_POSIX_STREAM_DESCRIPTOR)
/** 1 when native POSIX stdio descriptors can participate in asynchronous stream operations. */
#define WEBCPP_COBALT_HAS_POSIX_STDIO 1
#else
/** 1 when native POSIX stdio descriptors can participate in asynchronous stream operations. */
#define WEBCPP_COBALT_HAS_POSIX_STDIO 0
#endif

#ifdef WEBCPP_TARGET_WASIP2
/** 1 when Asio's explicit WASIp2 pollable stdio adapter is available with Cobalt tokens. */
#define WEBCPP_COBALT_HAS_POLLABLE_STDIO 1
#else
/** 1 when Asio's explicit WASIp2 pollable stdio adapter is available with Cobalt tokens. */
#define WEBCPP_COBALT_HAS_POLLABLE_STDIO 0
#endif

// Set a requirement before including any Cobalt integration header. Zero
// leaves the choice to the application and does not require the capability.
#if defined(WEBCPP_COBALT_REQUIRE_SOCKET_BACKEND) && WEBCPP_COBALT_REQUIRE_SOCKET_BACKEND && \
    !WEBCPP_COBALT_HAS_SOCKET_BACKEND
#error \
    "webcpp::cobalt: raw TCP/UDP is unavailable on Emscripten; use a browser transport or a native/WASI target"
#endif
#if defined(WEBCPP_COBALT_REQUIRE_THREADS) && WEBCPP_COBALT_REQUIRE_THREADS && \
    !WEBCPP_COBALT_HAS_THREADS
#error \
    "webcpp::cobalt: threads are disabled; select a supported threading=multi profile (WASIp2 supports single only)"
#endif
#if defined(WEBCPP_COBALT_REQUIRE_LOCAL_SOCKETS) && WEBCPP_COBALT_REQUIRE_LOCAL_SOCKETS && \
    !WEBCPP_COBALT_HAS_LOCAL_SOCKETS
#error \
    "webcpp::cobalt: local sockets require a native backend; use TCP or a supplied byte stream on WASI/browser"
#endif
#if defined(WEBCPP_COBALT_REQUIRE_SIGNALS) && WEBCPP_COBALT_REQUIRE_SIGNALS && \
    !WEBCPP_COBALT_HAS_SIGNALS
#error \
    "webcpp::cobalt: process signals require a native target; use explicit application cancellation on WASI/browser"
#endif
#if defined(WEBCPP_COBALT_REQUIRE_SERIAL_PORT) && WEBCPP_COBALT_REQUIRE_SERIAL_PORT && \
    !WEBCPP_COBALT_HAS_SERIAL_PORT
#error \
    "webcpp::cobalt: serial ports require native support; WASI/browser need a separate host adapter"
#endif
#if defined(WEBCPP_COBALT_REQUIRE_REVERSE_DNS) && WEBCPP_COBALT_REQUIRE_REVERSE_DNS && \
    !WEBCPP_COBALT_HAS_REVERSE_DNS
#error \
    "webcpp::cobalt: reverse DNS is unavailable on WASI/browser; use numeric endpoints or forward lookup"
#endif
#if defined(WEBCPP_COBALT_REQUIRE_OPENSSL) && WEBCPP_COBALT_REQUIRE_OPENSSL
#if !WEBCPP_COBALT_HAS_OPENSSL_BACKEND
#error \
    "webcpp::cobalt: guest OpenSSL is unavailable on Emscripten; use browser-managed HTTPS/WSS or native/WASI"
#elif !defined(WEBCPP_COBALT_LINK_SSL) || !WEBCPP_COBALT_LINK_SSL
#error \
    "webcpp::cobalt: SSL requires /webcpp/cobalt//io_ssl or //cobalt_ssl; outside B2, link matching Cobalt SSL/OpenSSL archives and define WEBCPP_COBALT_LINK_SSL=1"
#endif
#endif

#if defined(WEBCPP_COBALT_REQUIRE_PIPE_CREATION) && WEBCPP_COBALT_REQUIRE_PIPE_CREATION && \
    !WEBCPP_COBALT_HAS_PIPE_CREATION
#ifdef WEBCPP_TARGET_WASIP2
#error "webcpp::cobalt: wasi-libc does not create pipes on WASIp2; use supplied pollable streams"
#else
#error \
    "webcpp::cobalt: this target has no validated asynchronous pipe lifecycle; use a host adapter"
#endif
#endif
#if defined(WEBCPP_COBALT_REQUIRE_POSIX_STDIO) && WEBCPP_COBALT_REQUIRE_POSIX_STDIO && \
    !WEBCPP_COBALT_HAS_POSIX_STDIO
#if defined(WEBCPP_TARGET_WASIP2) || defined(WEBCPP_TARGET_WASIP3)
#error \
    "webcpp::cobalt: wasi-libc stdio descriptors have no nonblocking mode; use an explicit host stream adapter"
#else
#error "webcpp::cobalt: asynchronous POSIX stdio requires a native POSIX descriptor backend"
#endif
#endif
#if defined(WEBCPP_COBALT_REQUIRE_POLLABLE_STDIO) && WEBCPP_COBALT_REQUIRE_POLLABLE_STDIO && \
    !WEBCPP_COBALT_HAS_POLLABLE_STDIO
#error \
    "webcpp::cobalt: the pollable stdio adapter requires WASIp2; other targets need their own host adapter"
#endif

#endif  // derived capability observations

#endif  // WEBCPP_COBALT_CONFIG_HPP
