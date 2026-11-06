// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

// tag::program[]
#include <webcpp/test/exit_status.hpp>

#include <boost/asio/steady_timer.hpp>
#include <boost/cobalt/main.hpp>
#include <boost/cobalt/op.hpp>
#include <boost/cobalt/result.hpp>
#include <boost/cobalt/this_coro.hpp>

#include <chrono>
#include <cstdio>

boost::cobalt::main co_main([[maybe_unused]] int argc, [[maybe_unused]] char** argv) {
    // Cobalt's awaiter has static members called by the compiler's co_await expansion.
    // NOLINTNEXTLINE(readability-static-accessed-through-instance)
    const auto exec = co_await boost::cobalt::this_coro::executor;
    boost::asio::steady_timer timer(exec, std::chrono::milliseconds(50));
    auto [error] = co_await boost::cobalt::as_tuple(timer.async_wait(boost::cobalt::use_op));
    if (error) {
        std::printf("timer error: %d\n", error.value());
        co_return webcpp::test::exit_status(1);
    }
    std::puts("cobalt slept");
    co_return webcpp::test::exit_status(0);
}

// end::program[]
