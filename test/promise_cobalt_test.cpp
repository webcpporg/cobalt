// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

// Cobalt tokens await JavaScript promises (doc: #cobalt-promises).
#include <boost/core/lightweight_test.hpp>
#include <boost/system/system_error.hpp>
#include <webcpp/asio/browser/async_await.hpp>
#include <webcpp/cobalt/main.hpp>
#include <webcpp/cobalt/op.hpp>
#include <webcpp/cobalt/result.hpp>
#include <webcpp/cobalt/this_coro.hpp>
#include <webcpp/test/browser_values.hpp>
#include <webcpp/test/exit_status.hpp>

webcpp::cobalt::main co_main([[maybe_unused]] int argc, [[maybe_unused]] char** argv) {
    // NOLINTNEXTLINE(readability-static-accessed-through-instance)
    const auto exec = co_await webcpp::cobalt::this_coro::executor;
    const auto value = co_await webcpp::asio::browser::async_await(
        exec, emscripten::val::take_ownership(test_promise_after(20, 42, false)),
        webcpp::cobalt::use_op);
    BOOST_TEST_EQ(value.as<int>(), 42);
#ifndef WEBCPP_ASIO_NO_EXCEPTIONS
    bool caught = false;
    try {
        co_await webcpp::asio::browser::async_await(
            exec, emscripten::val::take_ownership(test_promise_after(20, 9, true)),
            webcpp::cobalt::use_op);
    } catch (const boost::system::system_error& error) {
        caught = error.code() == webcpp::asio::browser::error::promise_rejected;
    }
    BOOST_TEST(caught);
#endif
    auto [ec, reason] = co_await webcpp::cobalt::as_tuple(webcpp::asio::browser::async_await(
        exec, emscripten::val::take_ownership(test_promise_after(20, 9, true)),
        webcpp::cobalt::use_op));
    BOOST_TEST(ec == webcpp::asio::browser::error::promise_rejected);
    BOOST_TEST_EQ(reason.as<int>(), 9);
    co_return webcpp::test::exit_status(boost::report_errors());
}
