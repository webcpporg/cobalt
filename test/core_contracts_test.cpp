// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

#include <webcpp/cobalt.hpp>
#include <webcpp/test/exit_status.hpp>

#include "output.hpp"

#include <boost/core/lightweight_test.hpp>
#include <boost/variant2/variant.hpp>
#include <webcpp/asio/post.hpp>
#include <webcpp/asio/steady_timer.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <utility>

namespace cobalt = webcpp::cobalt;
namespace asio = webcpp::asio;

namespace {

cobalt::promise<int> deferred_value(int value) {
    // NOLINTNEXTLINE(readability-static-accessed-through-instance)
    const auto executor = co_await cobalt::this_coro::executor;
    BOOST_TEST(executor == cobalt::this_thread::get_executor());
    co_await asio::post(executor, cobalt::use_op);
    co_return value;
}

cobalt::task<int> lazy_value(bool& started) {
    started = true;
    co_return 42;
}

cobalt::generator<int> values() {
    co_yield 1;
    co_yield 2;
    co_return 3;
}

cobalt::promise<void> produce(cobalt::channel<int>& channel) {
    for (int value = 1; value <= 4; ++value) {
        co_await channel.write(value);
    }
    channel.close();
}

cobalt::promise<boost::system::error_code> cancellable_wait(asio::steady_timer& timer,
                                                            bool& initiated,
                                                            boost::system::error_code& observed) {
    // An eager promise initiates the operation before returning to its caller.
    initiated = true;
    const auto [error] = co_await cobalt::as_tuple(timer.async_wait(cobalt::use_op));
    observed = error;
    co_return error;
}

#ifndef WEBCPP_COBALT_NO_EXCEPTIONS
cobalt::promise<void> count_completion(int& count) {
    co_await deferred_value(0);
    ++count;
}
#endif

cobalt::detached detached_completion(bool& done) {
    co_await deferred_value(0);
    done = true;
}

}  // namespace

cobalt::main co_main([[maybe_unused]] int argc, [[maybe_unused]] char** argv) {
    // NOLINTNEXTLINE(readability-static-accessed-through-instance)
    const auto executor = co_await cobalt::this_coro::executor;
    asio::steady_timer deadline(executor, std::chrono::seconds(5));
    deadline.async_wait([](boost::system::error_code error) {
        if (!error) {
            webcpp::test::write_stderr("cobalt core contract timed out\n");
            std::_Exit(99);
        }
    });
    webcpp::test::write_stderr("core: task\n");
    bool started = false;
    auto lazy = lazy_value(started);
    BOOST_TEST(!started);
    BOOST_TEST_EQ(co_await lazy, 42);
    BOOST_TEST(started);

    webcpp::test::write_stderr("core: generator/noop\n");
    BOOST_TEST_EQ(co_await cobalt::noop(7), 7);
    int sum = 0;
    WEBCPP_COBALT_FOR(const int value, values()) {
        sum += value;
    }
    BOOST_TEST_EQ(sum, 6);

    webcpp::test::write_stderr("core: join\n");
    auto join_left = deferred_value(20);
    auto join_right = deferred_value(22);
    const auto [left, right] = co_await cobalt::join(join_left, join_right);
    BOOST_TEST_EQ(left + right, 42);
    webcpp::test::write_stderr("core: gather\n");
    auto gather_first = deferred_value(8);
    auto gather_second = deferred_value(9);
    const auto [first, second] = co_await cobalt::gather(gather_first, gather_second);
    BOOST_TEST(first && second);
    if (first && second) {
        BOOST_TEST_EQ(*first + *second, 17);
    }

    webcpp::test::write_stderr("core: channel\n");
    cobalt::channel<int> channel;
    auto producer = produce(channel);
    int received = 0;
    while (channel.is_open()) {
        received += co_await channel.read();
    }
    co_await producer;
    BOOST_TEST_EQ(received, 10);

    webcpp::test::write_stderr("core: race\n");
    asio::steady_timer timer(executor, std::chrono::hours(1));
    bool initiated = false;
    boost::system::error_code observed;
    auto pending = cancellable_wait(timer, initiated, observed);
    BOOST_TEST(initiated);
    BOOST_TEST(!pending.ready());
    pending.cancel();
    BOOST_TEST((co_await pending) == asio::error::operation_aborted);
    BOOST_TEST(observed == asio::error::operation_aborted);

    // Race operations directly. Interrupting a borrowed promise creates an
    // exception_ptr in the pinned upstream implementation, even with EH off.
    timer.expires_after(std::chrono::hours(1));
    const auto winner = co_await cobalt::race(asio::post(executor, cobalt::use_op),
                                              timer.async_wait(cobalt::use_op));
    BOOST_TEST_EQ(winner, 0U);
    BOOST_TEST_EQ(timer.cancel(), 0U);

#ifndef WEBCPP_COBALT_NO_EXCEPTIONS
    // wait_group takes ownership and moves pending promises (issue 287).
    webcpp::test::write_stderr("core: wait_group pending\n");
    int count = 0;
    cobalt::wait_group group;
    group.push_back(count_completion(count));
    group.push_back(count_completion(count));
    BOOST_TEST_EQ(group.size(), 2U);
    co_await group.wait();
    BOOST_TEST_EQ(count, 2);
    BOOST_TEST_EQ(group.reap(), 0U);
    BOOST_TEST_EQ(group.size(), 0U);
#else
    webcpp::test::write_stderr("core: wait_group ready\n");
    cobalt::wait_group group;
    group.push_back(cobalt::promise<void>{cobalt::noop{}});
    co_await group.wait();
    BOOST_TEST_EQ(group.size(), 0U);
#endif

    webcpp::test::write_stderr("core: detached\n");
    bool detached = false;
    detached_completion(detached);
    // The eager detached coroutine updates this flag after the posted continuation.
    // NOLINTNEXTLINE(bugprone-infinite-loop)
    while (!detached) {
        co_await asio::post(executor, cobalt::use_op);
    }

#ifndef WEBCPP_COBALT_NO_EXCEPTIONS
    // spawn moves a pending task, the exact libc++ EH-off issue-287 path.
    bool spawned = false;
    bool spawn_started = false;
    cobalt::spawn(executor, lazy_value(spawn_started),
                  [&](const std::exception_ptr& error, int value) {
                      BOOST_TEST(!error);
                      BOOST_TEST_EQ(value, 42);
                      spawned = true;
                  });
    // spawn may start immediately on this executor; completion is what the
    // caller awaits. Laziness before initiation was tested above.
    while (!spawned) {
        co_await asio::post(executor, cobalt::use_op);
    }
    BOOST_TEST(spawn_started);
#endif

    deadline.cancel();
    std::puts("cobalt: coroutine values, channels, composition, cancellation and cleanup checked");
    co_return webcpp::test::exit_status(boost::report_errors());
}
