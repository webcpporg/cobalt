// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

#include <webcpp/cobalt.hpp>
#include <webcpp/test/exit_status.hpp>

#include <boost/core/lightweight_test.hpp>
#include <webcpp/asio/steady_timer.hpp>

#include <chrono>
#include <cstdio>

webcpp::cobalt::main co_main([[maybe_unused]] int argc, [[maybe_unused]] char** argv) {
    // NOLINTNEXTLINE(readability-static-accessed-through-instance)
    const auto executor = co_await webcpp::cobalt::this_coro::executor;
    webcpp::asio::steady_timer timer(executor, std::chrono::milliseconds(1));
    const auto [error] =
        co_await webcpp::cobalt::as_tuple(timer.async_wait(webcpp::cobalt::use_op));
    BOOST_TEST(!error);
    std::puts("cobalt: the runner enters co_main");
    co_return webcpp::test::exit_status(boost::report_errors());
}
