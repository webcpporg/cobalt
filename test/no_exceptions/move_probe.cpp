// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

// A bounded witness for the exception_ptr limitation behind Cobalt issue 287.
// The driver verifies the libc++ diagnostic, not just a nonzero process status.
#include <webcpp/asio/io_context.hpp>
#include <webcpp/cobalt.hpp>
#include <webcpp/test/exit_status.hpp>

#include <boost/asio/post.hpp>

#include <cstdio>
#include <cstring>
#include <exception>
#include <utility>

namespace cobalt = boost::cobalt;

namespace {

cobalt::task<int> lazy_value() {
    co_return 42;
}

cobalt::promise<int> pending_value() {
    // NOLINTNEXTLINE(readability-static-accessed-through-instance)
    const auto executor = co_await cobalt::this_coro::executor;
    co_await boost::asio::post(executor, cobalt::use_op);
    co_return 42;
}

cobalt::generator<int> sequence() {
    co_yield 20;
    co_return 22;
}

cobalt::promise<int> consume(cobalt::generator<int>& generator) {
    int sum = 0;
    BOOST_COBALT_FOR(const int value, generator) {
        sum += value;
    }
    co_return sum;
}

cobalt::promise<int> complete(cobalt::task<int>& task) {
    co_return co_await task;
}

void before() {
    std::fputs("before move\n", stderr);
}

void after() {
    std::fputs("after move\n", stderr);
}

}  // namespace

#ifdef WEBCPP_TEST_BORROWED_RACE
cobalt::promise<int> borrowed_race() {
    auto left = pending_value();
    auto right = pending_value();
    const auto result = co_await cobalt::race(left, right);
    co_return static_cast<int>(result.index());
}
#endif

int main(int argc, char** argv) {
    if (argc != 2) {
        return 2;
    }
#ifdef BOOST_NO_EXCEPTIONS
    constexpr auto handling = "off";
#else
    constexpr auto handling = "on";
#endif
#ifdef _LIBCPP_VERSION
    std::printf("exception_handling=%s standard_library=libc++ version=%d\n", handling,
                _LIBCPP_VERSION);
#elif defined(_GLIBCXX_RELEASE)
    std::printf("exception_handling=%s standard_library=libstdc++ version=%d\n", handling,
                _GLIBCXX_RELEASE);
#else
    std::printf("exception_handling=%s standard_library=unknown version=0\n", handling);
#endif
    std::fflush(stdout);
    webcpp::asio::io_context context;
    cobalt::this_thread::set_executor(context.get_executor());
    int result = 0;
    if (std::strcmp(argv[1], "pending-task") == 0) {
        auto first = lazy_value();
        before();
        auto second = std::move(first);
        after();
        auto completed = complete(second);
        context.run();  // lint-run: consume the moved task and verify its result
        result = completed.ready() && completed.get() == 42 ? 0 : 1;
    } else if (std::strcmp(argv[1], "pending-promise") == 0) {
        auto first = pending_value();
        before();
        auto second = std::move(first);
        after();
        context.run();  // lint-run: drain this isolated pending-promise witness
        result = second.ready() && second.get() == 42 ? 0 : 1;
    } else if (std::strcmp(argv[1], "pending-generator") == 0) {
        auto first = sequence();
        before();
        auto second = std::move(first);
        after();
        auto consumed = consume(second);
        context.run();  // lint-run: consume the generator after ownership transfer
        result = consumed.ready() && consumed.get() == 42 ? 0 : 1;
    } else if (std::strcmp(argv[1], "completed-task") == 0) {
        auto first = lazy_value();
        auto completed = complete(first);
        context.run();  // lint-run: prove real coroutine completion before moving it
        if (!completed.ready() || completed.get() != 42) {
            return 3;
        }
        before();
        auto second = std::move(first);
        after();
        static_cast<void>(second);
    } else if (std::strcmp(argv[1], "ready-promise") == 0) {
        cobalt::promise<int> first{cobalt::noop(42)};
        before();
        auto second = std::move(first);
        after();
        result = second.ready() && second.get() == 42 ? 0 : 1;
    } else if (std::strcmp(argv[1], "ready-generator") == 0) {
        cobalt::generator<int> first{cobalt::noop(42)};
        before();
        auto second = std::move(first);
        after();
        result = second.ready() && second.get() == 42 ? 0 : 1;
    } else if (std::strcmp(argv[1], "exception-ptr") == 0) {
        before();
        const auto error = std::make_exception_ptr(42);
        after();
        result = error ? 0 : 1;
#ifdef WEBCPP_TEST_BORROWED_RACE
    } else if (std::strcmp(argv[1], "borrowed-race") == 0) {
        before();
        auto raced = borrowed_race();
        context.run();  // lint-run: observe the losing borrowed await interruption
        result = raced.ready() && raced.get() == 0 ? 0 : 1;
        after();
#endif
    } else {
        return 2;
    }
    return webcpp::test::exit_status(result);
}
