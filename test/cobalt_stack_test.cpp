// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

// Measure actual Asyncify unwinds through run_main, including forced reactor waits.
#include <webcpp/test/asyncify_watch.hpp>

#include <webcpp/test/exit_status.hpp>

#include <emscripten/eventloop.h>
#include <boost/asio/error.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/cobalt/io/steady_timer.hpp>
#include <boost/cobalt/main.hpp>
#include <boost/cobalt/op.hpp>
#include <boost/cobalt/result.hpp>
#include <boost/cobalt/this_coro.hpp>
#include <boost/core/lightweight_test.hpp>

#include <chrono>
#include <iostream>

namespace cobalt = boost::cobalt;
using watch = webcpp::test::unwinds;

namespace {

void check_stack(const char* path, bool waits) {
    std::cout << path << ": " << watch::deepest() << " bytes of " << watch::stack_size() << '\n';
    BOOST_TEST_GT(watch::count(), 0U);
    BOOST_TEST_GT(watch::deepest(), 0U);
    BOOST_TEST_LE(2 * watch::deepest(), watch::stack_size());
    if (waits) {
        BOOST_TEST_GT(watch::waits(), 0U);
    }
}

struct pending_cobalt {
    cobalt::executor exec;
    cobalt::io::steady_timer timer;
};

}  // namespace

cobalt::main co_main([[maybe_unused]] int argc, [[maybe_unused]] char** argv) {
    webcpp::test::bound(std::chrono::seconds(15));
    test_watch_unwinds();
    // Cobalt's awaiter has static members called by the compiler's co_await expansion.
    // NOLINTNEXTLINE(readability-static-accessed-through-instance)
    const auto exec = co_await cobalt::this_coro::executor;
    watch::reset();
    for (int i = 0; i < 2000; ++i) {
        co_await boost::asio::post(exec, cobalt::use_op);
    }
    check_stack("co_main, the yield", false);

    // Only a JavaScript macrotask posts cancellation. Until then the timer cannot expire,
    // so the event loop must suspend in a blocking poll, even on a heavily loaded host.
    pending_cobalt pending{
        .exec = exec,
        .timer = {std::chrono::steady_clock::time_point::max(), exec},
    };
    watch::reset();
    const auto cobalt_timeout = emscripten_set_timeout(
        [](void* raw) {
            auto* state = static_cast<pending_cobalt*>(raw);
            boost::asio::post(state->exec, [state] { state->timer.cancel(); });
        },
        0, &pending);
    auto [cobalt_error] = co_await cobalt::as_tuple(pending.timer.wait());
    emscripten_clear_timeout(cobalt_timeout);
    BOOST_TEST(cobalt_error == boost::asio::error::operation_aborted);
    check_stack("co_main, Cobalt timer wait", true);

    boost::asio::steady_timer timer(exec, std::chrono::steady_clock::time_point::max());
    watch::reset();
    const auto asio_timeout = emscripten_set_timeout(
        [](void* raw) {
            auto* state = static_cast<boost::asio::steady_timer*>(raw);
            boost::asio::post(state->get_executor(), [state] { state->cancel(); });
        },
        0, &timer);
    auto [asio_error] = co_await cobalt::as_tuple(timer.async_wait(cobalt::use_op));
    emscripten_clear_timeout(asio_timeout);
    BOOST_TEST(asio_error == boost::asio::error::operation_aborted);
    check_stack("co_main, Asio timer wait", true);
    co_return webcpp::test::exit_status(boost::report_errors());
}
