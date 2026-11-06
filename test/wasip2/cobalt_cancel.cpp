// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

#include <webcpp/test/exit_status.hpp>

#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/cobalt/io/resolver.hpp>
#include <boost/cobalt/io/steady_timer.hpp>
#include <boost/cobalt/main.hpp>
#include <boost/cobalt/promise.hpp>
#include <boost/core/lightweight_test.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace cobalt = boost::cobalt;
namespace io = cobalt::io;
namespace asio = boost::asio;

namespace {
cobalt::promise<void> resolve_cancelled(io::resolver& resolver) {
    const auto [error, endpoints] = co_await cobalt::as_tuple(resolver.resolve("localhost", "443"));
    BOOST_TEST(error == asio::error::operation_aborted);
    BOOST_TEST(endpoints.empty());
}

cobalt::promise<void> timer_cancelled(io::steady_timer& timer) {
    const auto [error] = co_await cobalt::as_tuple(timer.wait());
    BOOST_TEST(error == asio::error::operation_aborted);
}
}  // namespace

cobalt::main co_main([[maybe_unused]] int argc, [[maybe_unused]] char** argv) {
    // NOLINTNEXTLINE(readability-static-accessed-through-instance)
    const auto exec = co_await cobalt::this_coro::executor;
    asio::steady_timer watchdog(exec, std::chrono::seconds(5));
    watchdog.async_wait([](boost::system::error_code error) {
        if (!error) {
            std::abort();
        }
    });
    io::resolver resolver(exec);
    auto pending = resolve_cancelled(resolver);
    BOOST_TEST(!pending.ready());
    resolver.cancel();
    co_await pending;
    auto signal_cancelled = resolve_cancelled(resolver);
    BOOST_TEST(!signal_cancelled.ready());
    signal_cancelled.cancel();
    co_await signal_cancelled;

    io::lookup lookup("localhost", "443", exec);
    const auto [error, endpoints] = co_await cobalt::as_tuple(lookup);
    BOOST_TEST(!error);
    BOOST_TEST(!endpoints.empty());

    io::steady_timer timer(std::chrono::hours(1), exec);
    auto waiting = timer_cancelled(timer);
    BOOST_TEST(!waiting.ready());
    waiting.cancel();
    co_await waiting;
    watchdog.cancel();
    std::puts("cobalt: forward lookup, resolver cancellation and timer cancellation checked");
    co_return webcpp::test::exit_status(boost::report_errors());
}
