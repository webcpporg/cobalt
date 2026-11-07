// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

// No exit_status helper: the runner itself must preserve co_return after suspension.
#include <webcpp/asio/steady_timer.hpp>
#include <webcpp/cobalt/main.hpp>
#include <webcpp/cobalt/op.hpp>
#include <webcpp/cobalt/result.hpp>
#include <webcpp/cobalt/this_coro.hpp>

#include <chrono>
#include <cstdio>

webcpp::cobalt::main co_main(int argc, [[maybe_unused]] char** argv) {
    // Cobalt's awaiter has static members called by the compiler's co_await expansion.
    // NOLINTNEXTLINE(readability-static-accessed-through-instance)
    const auto exec = co_await webcpp::cobalt::this_coro::executor;
    webcpp::asio::steady_timer timer(exec, std::chrono::milliseconds(20));
    auto [error] = co_await webcpp::cobalt::as_tuple(timer.async_wait(webcpp::cobalt::use_op));
    if (error) {
        co_return 3;
    }
    const int result = argc > 1 ? 0 : 7;
    std::printf("cobalt: returning %d\n", result);
    co_return result;
}
