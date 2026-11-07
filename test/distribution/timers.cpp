// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
#include <chrono>
#include <cstdio>
#include <webcpp/asio/post.hpp>
#include <webcpp/asio/steady_timer.hpp>
#include <webcpp/cobalt/io/steady_timer.hpp>
#include <webcpp/cobalt/main.hpp>
#include <webcpp/cobalt/op.hpp>
#include <webcpp/cobalt/this_coro.hpp>

bool selected_configuration_is_consistent(const void* expected_resource);

webcpp::cobalt::main co_main(int, char**) {
    using namespace std::chrono_literals;
    namespace cobalt = webcpp::cobalt;
    const auto executor = co_await cobalt::this_coro::executor;
    co_await webcpp::asio::post(executor, cobalt::use_op);
    cobalt::io::steady_timer coroutine_timer(2ms, executor);
    co_await coroutine_timer.wait();
    webcpp::asio::steady_timer asio_timer(executor, 2ms);
    co_await asio_timer.async_wait(cobalt::use_op);
    const void* resource = nullptr;
#ifndef BOOST_COBALT_NO_PMR
    resource = cobalt::this_thread::get_default_resource();
#endif
    if (!selected_configuration_is_consistent(resource)) {
        std::puts("distribution Cobalt configuration: inconsistent");
        co_return 1;
    }
#ifdef BOOST_COBALT_USE_CUSTOM_PMR
    std::puts("distribution custom PMR allocations and cross-TU resource: passed");
#endif
    std::puts("distribution Cobalt/Asio timers: passed");
    co_return 0;
}
