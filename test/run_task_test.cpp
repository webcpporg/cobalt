// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

#include <webcpp/cobalt.hpp>
#include <webcpp/test/exit_status.hpp>
#include <webcpp/test/page_timeout.hpp>

#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/core/lightweight_test.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>

#ifndef WEBCPP_TEST_TURN_LIMIT
#define WEBCPP_TEST_TURN_LIMIT 100
#endif

namespace {

boost::cobalt::task<int> calculate() {
    // NOLINTNEXTLINE(readability-static-accessed-through-instance)
    const auto executor = co_await boost::cobalt::this_coro::executor;
    const webcpp::test::yield_counter yields;
    int last = 0;
    std::size_t longest = 0;
    std::size_t consecutive = 0;
    for (int index = 0; index < 5000; ++index) {
        co_await boost::asio::post(executor, boost::cobalt::use_op);
        const auto now = yields.count();
        if (now != last) {
            last = now;
            consecutive = 0;
        }
        longest = std::max(longest, ++consecutive);
    }
#ifdef __EMSCRIPTEN__
    BOOST_TEST_GE(yields.count(), 4999 / WEBCPP_TEST_TURN_LIMIT);
    BOOST_TEST_EQ(longest, std::min<std::size_t>(5000, WEBCPP_TEST_TURN_LIMIT));
#else
    BOOST_TEST_EQ(yields.count(), 0);
    BOOST_TEST_EQ(longest, 5000U);
#endif
    boost::asio::steady_timer timer(executor, std::chrono::milliseconds(1));
    const auto [error] = co_await boost::cobalt::as_tuple(timer.async_wait(boost::cobalt::use_op));
    BOOST_TEST(!error);
    co_return 42;
}

}  // namespace

int main() {
    BOOST_TEST_EQ(boost::cobalt::run(calculate()), 42);
    std::puts("cobalt: run(task) checked independently of co_main");
    return webcpp::test::exit_status(boost::report_errors());
}
