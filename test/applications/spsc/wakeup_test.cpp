// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

#include <webcpp/asio/io_context.hpp>
#include <webcpp/cobalt.hpp>
#include <webcpp/test/exit_status.hpp>

#ifdef WEBCPP_TEST_ORIGINAL_SPSC
#include "original_queue.hpp"
#else
#include "../../../example/applications/spsc_queue.hpp"
#endif

#include <boost/core/lightweight_test.hpp>

#include <coroutine>
#include <cstdio>
#include <exception>

namespace {

struct continuation {
    struct promise_type {
        continuation get_return_object() {
            return {std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        static std::suspend_always initial_suspend() noexcept { return {}; }

        static std::suspend_never final_suspend() noexcept { return {}; }

        void return_void() const noexcept {}

        static void unhandled_exception() noexcept { std::terminate(); }
    };

    std::coroutine_handle<promise_type> handle;
};

continuation resumed(bool& value) {
    value = true;
    co_return;
}

}  // namespace

cobalt::main co_main([[maybe_unused]] int argc, [[maybe_unused]] char** argv) {
    // The two interleavings are forced without sleeps or OS scheduling:
    // queue availability changes between await_ready and await_suspend.
    // NOLINTNEXTLINE(readability-static-accessed-through-instance)
    const auto creator = co_await cobalt::this_coro::executor;
    webcpp::asio::io_context executor;
    cobalt::this_thread::set_executor(executor.get_executor());
    bool read_resumed = false;
    bool write_resumed = false;
    {
        awaitable_spsc_queue<int> queue(1);
        auto reading = queue.read();
        BOOST_TEST(!reading.await_ready());
        BOOST_TEST(queue.queue.push(42));
        reading.await_suspend(resumed(read_resumed).handle);
        executor.poll();
#ifdef WEBCPP_TEST_ORIGINAL_SPSC
        BOOST_TEST(!read_resumed);
#else
        BOOST_TEST(read_resumed);
#endif
    }
    executor.restart();
    {
        awaitable_spsc_queue<int> queue(1);
        BOOST_TEST(queue.queue.push(7));
        auto writing = queue.write(42);
        BOOST_TEST(!writing.await_ready());
        int ignored = 0;
        BOOST_TEST(queue.queue.pop(ignored));
        writing.await_suspend(resumed(write_resumed).handle);
        executor.poll();
#ifdef WEBCPP_TEST_ORIGINAL_SPSC
        BOOST_TEST(!write_resumed);
#else
        BOOST_TEST(write_resumed);
#endif
    }
#ifdef WEBCPP_TEST_ORIGINAL_SPSC
    std::puts("spsc: original loses both wakeups in the forced interleavings");
#else
    std::puts("spsc: reader and writer both resume after readiness changes");
#endif
    cobalt::this_thread::set_executor(creator);
    co_return webcpp::test::exit_status(boost::report_errors());
}
