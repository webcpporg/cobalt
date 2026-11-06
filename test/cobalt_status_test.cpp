// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

// No exit_status helper: the runner itself must preserve co_return after suspension.
#include <boost/asio/steady_timer.hpp>
#include <boost/cobalt/main.hpp>
#include <boost/cobalt/op.hpp>
#include <boost/cobalt/result.hpp>
#include <boost/cobalt/this_coro.hpp>

#include <chrono>
#include <cstdio>

boost::cobalt::main co_main(int argc, [[maybe_unused]] char** argv) {
    // Cobalt's awaiter has static members called by the compiler's co_await expansion.
    // NOLINTNEXTLINE(readability-static-accessed-through-instance)
    const auto exec = co_await boost::cobalt::this_coro::executor;
    boost::asio::steady_timer timer(exec, std::chrono::milliseconds(20));
    auto [error] = co_await boost::cobalt::as_tuple(timer.async_wait(boost::cobalt::use_op));
    if (error) {
        co_return 3;
    }
    const int result = argc > 1 ? 0 : 7;
    std::printf("cobalt: returning %d\n", result);
    co_return result;
}
