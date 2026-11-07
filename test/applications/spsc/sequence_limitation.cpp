// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

#include <webcpp/cobalt.hpp>
#include <webcpp/test/exit_status.hpp>

#include <boost/core/lightweight_test.hpp>
#include <boost/lockfree/spsc_queue.hpp>

#include <coroutine>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <functional>

namespace {
struct interleaved_value {
    int value = -1;
};
}  // namespace

namespace boost::lockfree {

// Keep real lockfree storage and return its actual results. The callback
// schedules the other producer between a completed pop and its notification,
// without relying on OS timing or changing either awaitable queue.
template <>
class spsc_queue<interleaved_value> {
    spsc_queue<int> storage_;

public:
    explicit spsc_queue(std::size_t size) : storage_(size) {}

    std::function<void()> after_pop;

    bool push(interleaved_value value) { return storage_.push(value.value); }

    bool pop(interleaved_value& value) {
        const bool result = storage_.pop(value.value);
        if (after_pop) {
            after_pop();
        }
        return result;
    }

    [[nodiscard]] std::size_t read_available() const { return storage_.read_available(); }

    [[nodiscard]] std::size_t write_available() const { return storage_.write_available(); }
};

}  // namespace boost::lockfree

#ifdef WEBCPP_TEST_ORIGINAL_SPSC
#include "original_queue.hpp"
#else
#include "../../../example/applications/spsc_queue.hpp"
#endif

namespace {

struct producer {
    struct promise_type {
        cobalt::executor executor = cobalt::this_thread::get_executor();

        static producer get_return_object() { return {}; }

        static std::suspend_never initial_suspend() noexcept { return {}; }

        static std::suspend_never final_suspend() noexcept { return {}; }

        void return_void() const noexcept {}

        static void unhandled_exception() noexcept { std::terminate(); }

        [[nodiscard]] cobalt::executor get_executor() const { return executor; }
    };
};

// The coroutine transformation calls static hooks through its promise object.
// NOLINTNEXTLINE(readability-static-accessed-through-instance)
producer write_two(awaitable_spsc_queue<interleaved_value>& queue, bool& completed) {
    co_await queue.write(interleaved_value{1});
    co_await queue.write(interleaved_value{2});
    completed = true;
}

}  // namespace

int main() {
    webcpp::asio::io_context context;
    cobalt::this_thread::set_executor(context.get_executor());
    awaitable_spsc_queue<interleaved_value> queue(1);
    BOOST_TEST(queue.queue.push(interleaved_value{0}));
    bool completed = false;
    queue.queue.after_pop = [&] {
        // The first write fills the freed slot. The next suspends on the
        // full queue before the older consumer sends its notification.
        write_two(queue, completed);
        BOOST_TEST(!completed);
        BOOST_TEST(queue.writer.load() != nullptr);
        BOOST_TEST_EQ(queue.queue.write_available(), 0U);
    };
    BOOST_TEST_EQ(queue.read().await_resume().value, 0);
    queue.queue.after_pop = {};
    BOOST_TEST_EQ(context.run(), 1U);  // lint-run: dispatch the actual producer continuation
    BOOST_TEST(completed);
    interleaved_value received;
    BOOST_TEST(queue.queue.pop(received));
    BOOST_TEST_EQ(received.value, 1);
    // This is an expected defect, not successful sequence transfer: the
    // second write reported completion but its value never entered storage.
    BOOST_TEST(!queue.queue.pop(received));
#ifdef WEBCPP_TEST_ORIGINAL_SPSC
    std::puts("UPSTREAM-LIMITATION original: delayed notification drops value 2");
#else
    std::puts("UPSTREAM-LIMITATION counterpart: delayed notification drops value 2");
#endif
    return webcpp::test::exit_status(boost::report_errors());
}
