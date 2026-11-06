// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

// An unchanged Cobalt entry point must receive webcpp's turn limit and run both timer APIs.
#include <webcpp/test/page_timeout.hpp>

#include <webcpp/test/exit_status.hpp>

#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/cobalt/io/steady_timer.hpp>
#include <boost/cobalt/main.hpp>
#include <boost/cobalt/op.hpp>
#include <boost/cobalt/result.hpp>
#include <boost/cobalt/this_coro.hpp>
#include <boost/core/lightweight_test.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>

// The build supplies an independent expectation for configuration regressions.
#ifndef WEBCPP_TEST_TURN_LIMIT
#define WEBCPP_TEST_TURN_LIMIT 100
#endif

using namespace std::chrono_literals;
namespace cobalt = boost::cobalt;

cobalt::main co_main([[maybe_unused]] int argc, [[maybe_unused]] char** argv) {
    // Cobalt's awaiter has static members called by the compiler's co_await expansion.
    // NOLINTNEXTLINE(readability-static-accessed-through-instance)
    const auto exec = co_await cobalt::this_coro::executor;
    const webcpp::test::yield_counter yields;
    int seen = 0;
    std::size_t since = 0;
    std::size_t longest = 0;
    for (std::size_t i = 0; i < 5000; ++i) {
        co_await boost::asio::post(exec, cobalt::use_op);
        const int now = yields.count();
        if (now != seen) {
            seen = now;
            since = 0;
        }
        longest = std::max(longest, ++since);
    }
#ifdef __EMSCRIPTEN__
    BOOST_TEST_GE(yields.count(), 4999 / WEBCPP_TEST_TURN_LIMIT);
    BOOST_TEST_EQ(longest, std::min<std::size_t>(5000, WEBCPP_TEST_TURN_LIMIT));
    BOOST_TEST_GT(longest, 0U);
    // Observe the host timer separately: its scheduling delay does not define the turn limit.
    webcpp::test::page_timeout timeout;
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    const int before = yields.count();
    while (!timeout.ran() && std::chrono::steady_clock::now() < deadline) {
        co_await boost::asio::post(exec, cobalt::use_op);
    }
    BOOST_TEST(timeout.ran());
    BOOST_TEST_GT(yields.count(), before);
#else
    BOOST_TEST_EQ(yields.count(), 0);
    BOOST_TEST_EQ(longest, 5000U);
#endif
    cobalt::io::steady_timer cobalt_timer(50ms, exec);
    auto [cobalt_error] = co_await cobalt::as_tuple(cobalt_timer.wait());
    BOOST_TEST(!cobalt_error);
    boost::asio::steady_timer asio_timer(exec, 20ms);
    auto [asio_error] = co_await cobalt::as_tuple(asio_timer.async_wait(cobalt::use_op));
    BOOST_TEST(!asio_error);
    std::puts("cobalt: turns and timers passed");
    co_return webcpp::test::exit_status(boost::report_errors());
}
