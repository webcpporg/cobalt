// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

#include <webcpp/asio/io_context.hpp>
#include <webcpp/cobalt.hpp>
#include <webcpp/test/exit_status.hpp>

#include <boost/core/lightweight_test.hpp>
#include <webcpp/asio/post.hpp>

#include <cstdio>

namespace {

// GCC 14 misidentifies upstream coroutine allocation: boostorg/cobalt#7.
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 14
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif

webcpp::cobalt::promise<int> work() {
    // NOLINTNEXTLINE(readability-static-accessed-through-instance)
    const auto executor = co_await webcpp::cobalt::this_coro::executor;
    co_await webcpp::asio::post(executor, webcpp::cobalt::use_op);
    co_return 42;
}

#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 14
#pragma GCC diagnostic pop
#endif

}  // namespace

int main() {
    webcpp::asio::io_context context;
    webcpp::cobalt::this_thread::set_executor(context.get_executor());
    auto operation = work();
    context.run();  // lint-run: this application's custom main drives its context
    if (!BOOST_TEST(operation.ready())) {
        return webcpp::test::exit_status(1);
    }
    BOOST_TEST_EQ(operation.get(), 42);
    std::puts("cobalt: custom main links core without the co_main runner");
    return webcpp::test::exit_status(boost::report_errors());
}
