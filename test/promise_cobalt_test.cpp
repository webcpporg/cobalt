// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

// Cobalt tokens await JavaScript promises (doc: #asio-promises).
#include <boost/cobalt/main.hpp>
#include <boost/cobalt/op.hpp>
#include <boost/cobalt/result.hpp>
#include <boost/cobalt/this_coro.hpp>
#include <boost/core/lightweight_test.hpp>
#include <boost/system/system_error.hpp>
#include <webcpp/asio/browser/async_await.hpp>
#include <webcpp/test/browser_values.hpp>
#include <webcpp/test/exit_status.hpp>

boost::cobalt::main co_main([[maybe_unused]] int argc, [[maybe_unused]] char** argv) {
    // NOLINTNEXTLINE(readability-static-accessed-through-instance)
    const auto exec = co_await boost::cobalt::this_coro::executor;
    const auto value = co_await webcpp::asio::browser::async_await(
        exec, emscripten::val::take_ownership(test_promise_after(20, 42, false)),
        boost::cobalt::use_op);
    BOOST_TEST_EQ(value.as<int>(), 42);
#ifndef WEBCPP_ASIO_NO_EXCEPTIONS
    bool caught = false;
    try {
        co_await webcpp::asio::browser::async_await(
            exec, emscripten::val::take_ownership(test_promise_after(20, 9, true)),
            boost::cobalt::use_op);
    } catch (const boost::system::system_error& error) {
        caught = error.code() == webcpp::asio::browser::error::promise_rejected;
    }
    BOOST_TEST(caught);
#endif
    auto [ec, reason] = co_await boost::cobalt::as_tuple(webcpp::asio::browser::async_await(
        exec, emscripten::val::take_ownership(test_promise_after(20, 9, true)),
        boost::cobalt::use_op));
    BOOST_TEST(ec == webcpp::asio::browser::error::promise_rejected);
    BOOST_TEST_EQ(reason.as<int>(), 9);
    co_return webcpp::test::exit_status(boost::report_errors());
}
