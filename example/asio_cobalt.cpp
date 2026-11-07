// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

// tag::program[]
#include <webcpp/test/exit_status.hpp>

#include <webcpp/asio/steady_timer.hpp>
#include <webcpp/cobalt/main.hpp>
#include <webcpp/cobalt/op.hpp>
#include <webcpp/cobalt/result.hpp>
#include <webcpp/cobalt/this_coro.hpp>

#include <chrono>
#include <cstdio>

// Boost 1.92 intentionally omits this promise hook without exceptions. Clang
// 21 and 23 still warn at the coroutine definition even with -fno-exceptions.
// Keep the workaround on this native example, without changing Boost's types.
#if defined(__clang__) && defined(WEBCPP_TARGET_NATIVE) && !defined(__EXCEPTIONS)
#if __has_warning("-Wcoroutine-missing-unhandled-exception")
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcoroutine-missing-unhandled-exception"
#endif
#endif

webcpp::cobalt::main co_main([[maybe_unused]] int argc, [[maybe_unused]] char** argv) {
    // Cobalt's awaiter has static members called by the compiler's co_await expansion.
    // NOLINTNEXTLINE(readability-static-accessed-through-instance)
    const auto exec = co_await webcpp::cobalt::this_coro::executor;
    webcpp::asio::steady_timer timer(exec, std::chrono::milliseconds(50));
    auto [error] = co_await webcpp::cobalt::as_tuple(timer.async_wait(webcpp::cobalt::use_op));
    if (error) {
        std::printf("timer error: %d\n", error.value());
        co_return webcpp::test::exit_status(1);
    }
    std::puts("cobalt slept");
    co_return webcpp::test::exit_status(0);
}

#if defined(__clang__) && defined(WEBCPP_TARGET_NATIVE) && !defined(__EXCEPTIONS)
#if __has_warning("-Wcoroutine-missing-unhandled-exception")
#pragma clang diagnostic pop
#endif
#endif

// end::program[]
