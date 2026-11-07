// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

#include <atomic>
#include <cstdio>
#include <webcpp/asio/post.hpp>
#include <webcpp/cobalt/op.hpp>
#include <webcpp/cobalt/this_coro.hpp>
#include <webcpp/cobalt/thread.hpp>

webcpp::cobalt::thread worker(std::atomic<int>& completed) {
    const auto executor = co_await webcpp::cobalt::this_coro::executor;
    co_await webcpp::asio::post(executor, webcpp::cobalt::use_op);
    completed.store(42);
}

int main() {
    std::atomic<int> completed{0};
    auto operation = worker(completed);
    operation.join();
    if (completed.load() != 42) {
        return 1;
    }
    std::puts("distribution Cobalt thread coroutine and join: passed");
}
