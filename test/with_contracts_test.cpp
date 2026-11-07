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

struct resource {
    int* cleanups;

    cobalt::promise<void> await_exit(std::exception_ptr error) const {
        BOOST_TEST(!error);
        co_await deferred_value(0);
        ++*cleanups;
    }
};

cobalt::promise<void> use_resource([[maybe_unused]] resource& value) {
    co_await deferred_value(0);
}

}  // namespace

#ifdef WEBCPP_TARGET_NATIVE
// Boost 1.92 binds an rvalue cleanup argument to an lvalue in two overloads.
// expected-error@* 2 {{rvalue reference to type}}
#endif

cobalt::main co_main([[maybe_unused]] int argc, [[maybe_unused]] char** argv) {
    webcpp::test::write_stderr("core: with\n");
    int cleanups = 0;
    co_await cobalt::with(resource{&cleanups}, use_resource);
    BOOST_TEST_EQ(cleanups, 1);
    co_await cobalt::with(resource{&cleanups}, [](resource&) {});
    BOOST_TEST_EQ(cleanups, 2);
    BOOST_TEST_EQ(co_await cobalt::with(resource{&cleanups}, [](resource&) { return 7; }), 7);
    BOOST_TEST_EQ(cleanups, 3);
    const auto asynchronous = [](resource&) -> cobalt::promise<int> {
        co_return co_await deferred_value(42);
    };
    BOOST_TEST_EQ(co_await cobalt::with(resource{&cleanups}, asynchronous), 42);
    BOOST_TEST_EQ(cleanups, 4);
    co_return webcpp::test::exit_status(boost::report_errors());
}
