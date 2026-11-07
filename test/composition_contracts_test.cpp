// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

#include <webcpp/cobalt.hpp>
#include <webcpp/test/exit_status.hpp>

#include "output.hpp"

#include <boost/core/lightweight_test.hpp>
#include <webcpp/asio/deferred.hpp>
#include <webcpp/asio/post.hpp>
#include <webcpp/asio/steady_timer.hpp>
#include <webcpp/cobalt/composition.hpp>

#include <chrono>
#include <coroutine>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <tuple>
#include <utility>

namespace cobalt = webcpp::cobalt;
namespace asio = webcpp::asio;

namespace {

class counting_resource final : public cobalt::pmr::memory_resource {
public:
    std::size_t allocations = 0;
    std::size_t live_bytes = 0;

private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override {
        auto* result = cobalt::pmr::new_delete_resource()->allocate(bytes, alignment);
        ++allocations;
        live_bytes += bytes;
        return result;
    }

    void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override {
        BOOST_TEST(live_bytes >= bytes);
        live_bytes -= bytes;
        cobalt::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }

    [[nodiscard]] bool do_is_equal(
        const cobalt::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }
};

struct composed_wait final : cobalt::op<boost::system::error_code, int> {
    asio::steady_timer& timer;
    int& starts;
    int& completions;

    composed_wait(asio::steady_timer& value, int& start_count, int& completion_count)
        : timer(value), starts(start_count), completions(completion_count) {}

    // The coroutine transformation invokes static promise hooks through its promise object.
    // NOLINTNEXTLINE(readability-static-accessed-through-instance)
    void initiate([[maybe_unused]] cobalt::completion_handler<boost::system::error_code, int>
                      handler) override {
        ++starts;
        // NOLINTNEXTLINE(readability-static-accessed-through-instance)
        const auto executor = co_await cobalt::this_coro::executor;
        BOOST_TEST(executor == timer.get_executor());
        const auto [error] = co_await timer.async_wait(asio::deferred);
        ++completions;
        co_return std::make_tuple(error, error ? 0 : 42);
    }
};

cobalt::promise<boost::system::error_code> allocated_wait(
    [[maybe_unused]] std::allocator_arg_t tag, cobalt::pmr::polymorphic_allocator<void> allocator,
    asio::steady_timer& timer, int& starts, int& completions, bool& resumed) {
    // NOLINTNEXTLINE(readability-static-accessed-through-instance)
    const auto actual_allocator = co_await cobalt::this_coro::allocator;
    BOOST_TEST(actual_allocator.resource() == allocator.resource());
    auto operation = composed_wait(timer, starts, completions);
    BOOST_TEST_EQ(starts, 0);
    const auto [error, value] = co_await cobalt::as_tuple(operation);
    BOOST_TEST_EQ(value, error ? 0 : 42);
    resumed = true;
    co_return error;
}

struct suspended_frame {
    struct promise_type {
        int& destructions;

        explicit promise_type(int& count, [[maybe_unused]] bool& ran) : destructions(count) {}

        ~promise_type() { ++destructions; }

        suspended_frame get_return_object() {
            return {cobalt::unique_handle<promise_type>::from_promise(*this)};
        }

        static std::suspend_always initial_suspend() noexcept { return {}; }

        static std::suspend_always final_suspend() noexcept { return {}; }

        void return_void() const noexcept {}

        static void unhandled_exception() noexcept { std::terminate(); }
    };

    cobalt::unique_handle<promise_type> handle;
};

// The coroutine transformation invokes static promise hooks through its promise object.
// NOLINTNEXTLINE(readability-static-accessed-through-instance)
suspended_frame frame([[maybe_unused]] int& destructions, bool& ran) {
    ran = true;
    co_return;
}

void check_frame_lifetime() {
    int destructions = 0;
    bool ran = false;
    {
        auto first = frame(destructions, ran);
        BOOST_TEST(!ran);
        const auto* const address = first.handle.address();
        auto owned = std::move(first.handle);
        BOOST_TEST(!first.handle);
        BOOST_TEST(owned.address() == address);
        owned.resume();
        BOOST_TEST(ran);
        BOOST_TEST(owned.done());
        const auto raw = owned.release();
        BOOST_TEST(!owned);
        auto restored = cobalt::unique_handle<void>::from_address(raw.address());
        BOOST_TEST(restored.done());
        restored.destroy();
        BOOST_TEST(!restored);
    }
    BOOST_TEST_EQ(destructions, 1);
}

}  // namespace

cobalt::main co_main([[maybe_unused]] int argc, [[maybe_unused]] char** argv) {
    // NOLINTNEXTLINE(readability-static-accessed-through-instance)
    const auto executor = co_await cobalt::this_coro::executor;
    asio::steady_timer deadline(executor, std::chrono::seconds(5));
    deadline.async_wait([](boost::system::error_code error) {
        if (!error) {
            webcpp::test::write_stderr("composition contract timed out\n");
            std::_Exit(99);
        }
    });
    counting_resource resource;
    asio::steady_timer timer(executor);
    for (const bool cancel : {false, true}) {
        int starts = 0;
        int completions = 0;
        bool resumed = false;
        timer.expires_after(cancel ? std::chrono::hours(1) : std::chrono::hours(0));
        {
            auto pending = allocated_wait(std::allocator_arg,
                                          cobalt::pmr::polymorphic_allocator<void>(&resource),
                                          timer, starts, completions, resumed);
            BOOST_TEST_EQ(starts, 1);
            BOOST_TEST_EQ(completions, 0);
            BOOST_TEST(!pending.ready());
            BOOST_TEST(resource.allocations > 0);
            BOOST_TEST(resource.live_bytes > 0);
            if (cancel) {
                pending.cancel();
            }
            const auto error = co_await pending;
            BOOST_TEST(error ==
                       (cancel ? asio::error::operation_aborted : boost::system::error_code{}));
            BOOST_TEST(resumed);
            BOOST_TEST_EQ(completions, 1);
        }
        // Allow any allocator-backed completion cleanup to leave this executor.
        co_await asio::post(executor, cobalt::use_op);
        BOOST_TEST_EQ(resource.live_bytes, 0U);
        BOOST_TEST_EQ(completions, 1);
    }
    check_frame_lifetime();
    deadline.cancel();
    std::puts("cobalt: composed cancellation, allocator, executor and frame lifetime checked");
    co_return webcpp::test::exit_status(boost::report_errors());
}
