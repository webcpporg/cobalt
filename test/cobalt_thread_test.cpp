// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

#include <webcpp/test/exit_status.hpp>

#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/cobalt/main.hpp>
#include <boost/cobalt/op.hpp>
#include <boost/cobalt/result.hpp>
#include <boost/cobalt/this_coro.hpp>
#include <boost/cobalt/thread.hpp>
#include <boost/core/lightweight_test.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace {

struct worker_result {
    std::atomic<int> started{0};
    std::atomic<int> completed{0};
    std::atomic<int> errors{0};
    int returned_to_creator = 0;
    std::thread::id creator = std::this_thread::get_id();
};

boost::cobalt::thread worker(worker_result& result, boost::cobalt::executor creator) {
    // NOLINTNEXTLINE(readability-static-accessed-through-instance)
    const auto exec = co_await boost::cobalt::this_coro::executor;
    if (std::this_thread::get_id() == result.creator) {
        ++result.errors;
    }
    boost::asio::steady_timer timer(exec, std::chrono::hours(1));
    // Posting the observation ensures async_wait has already been initiated.
    boost::asio::post(exec, [&result] { result.started.store(1); });
    const auto [error] = co_await boost::cobalt::as_tuple(timer.async_wait(boost::cobalt::use_op));
    if (error != boost::asio::error::operation_aborted ||
        std::this_thread::get_id() == result.creator) {
        ++result.errors;
    }
    boost::asio::post(creator, [&result] {
        if (std::this_thread::get_id() != result.creator) {
            ++result.errors;
        }
        ++result.returned_to_creator;
    });
    result.completed.store(42);
}

}  // namespace

boost::cobalt::main co_main([[maybe_unused]] int argc, [[maybe_unused]] char** argv) {
    // NOLINTNEXTLINE(readability-static-accessed-through-instance)
    const auto creator = co_await boost::cobalt::this_coro::executor;
    boost::asio::steady_timer deadline(creator, std::chrono::seconds(5));
    deadline.async_wait([](boost::system::error_code error) {
        if (!error) {
            std::fputs("cobalt worker lifecycle timed out\n", stderr);
            std::_Exit(99);
        }
    });
    worker_result result;
    auto operation = worker(result, creator);
    BOOST_TEST(operation.get_id() != result.creator);
    boost::asio::steady_timer observe(creator);
    while (!result.started.load()) {
        observe.expires_after(std::chrono::milliseconds(1));
        co_await observe.async_wait(boost::cobalt::use_op);
    }
    operation.cancel();
    co_await operation;
    while (result.returned_to_creator == 0) {
        co_await boost::asio::post(creator, boost::cobalt::use_op);
    }
    BOOST_TEST_EQ(result.completed.load(), 42);
    BOOST_TEST_EQ(result.errors.load(), 0);
    BOOST_TEST_EQ(result.returned_to_creator, 1);
    BOOST_TEST(std::this_thread::get_id() == result.creator);
    deadline.cancel();
    std::puts("cobalt: thread coroutine completed and joined");
    co_return webcpp::test::exit_status(boost::report_errors());
}
