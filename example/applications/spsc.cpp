// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

// Derived from Boost.Cobalt 1.92 example/spsc.cpp by Klemens Morgenstern.
// This counterpart bounds the work, verifies sequence/checksum, closes the
// lost-wakeup window and keeps the queue alive until its worker has joined.
#include <webcpp/cobalt.hpp>
#include <webcpp/test/exit_status.hpp>

#include "active_run.hpp"
#include "events.hpp"
#include "spsc_queue.hpp"

#include <boost/core/lightweight_test.hpp>

#include <atomic>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <string_view>

namespace {
cobalt::thread produce(awaitable_spsc_queue<int>& queue, int count, const std::atomic<bool>& stop) {
    for (int value = 0; value < count && !stop.load(); ++value) {
        co_await queue.write(value);
    }
    // Explicit end-of-stream lets the consumer drain outstanding values and
    // join the worker even when cancellation arrives while the queue is full.
    co_await queue.write(-1);
}

bool parse(std::string_view text, int& value) {
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}
}  // namespace

extern "C"
#ifdef __EMSCRIPTEN__
    EMSCRIPTEN_KEEPALIVE
#endif
    int cobalt_cancel_spsc() {
    auto* active_stop = static_cast<std::atomic<bool>*>(active_run::find("spsc"));
    if (active_stop == nullptr) {
        return 0;
    }
    active_stop->store(true);
    return 1;
}

// tag::program[]
cobalt::main co_main(int argc, char** argv) {
    int count = 100000;
    int cancel_after = 0;
    if (argc > 3 || (argc > 1 && !parse(argv[1], count)) ||
        (argc > 2 && !parse(argv[2], cancel_after)) || count < 1 || count > 10000000 ||
        cancel_after < 0 || cancel_after >= count) {
        if (std::fputs("usage: spsc <count 1..10000000> [cancel-after <count]\n", stderr) < 0) {
            co_return webcpp::test::exit_status(1);
        }
        co_return webcpp::test::exit_status(2);
    }
    // NOLINTNEXTLINE(readability-static-accessed-through-instance)
    const auto executor = co_await cobalt::this_coro::executor;
    awaitable_spsc_queue<int> queue(128);
    std::atomic<bool> stop{false};
    active_run registration("spsc", &stop);
    if (!registration.registered()) {
        co_return webcpp::test::exit_status(2);
    }
    cobalt_event("started", 0, 0, 0);
    auto worker = produce(queue, count, stop);
    int received = 0;
    std::int64_t checksum = 0;
    for (;;) {
        const int value = co_await queue.read();
        if (value == -1) {
            break;
        }
        BOOST_TEST_EQ(value, received);
        checksum += value;
        ++received;
        if (received % 256 == 0) {
            cobalt_event("progress", received, value, static_cast<int>(stop.load()));
        }
        if (cancel_after != 0 && received == cancel_after) {
            stop.store(true);
        }
        // Keep the UI cancellation callback reachable even while data is ready.
        co_await webcpp::asio::post(executor, cobalt::use_op);
    }
    co_await worker;
    registration.reset();
    BOOST_TEST_EQ(checksum, static_cast<std::int64_t>(received) * (received - 1) / 2);
    if (!stop.load()) {
        BOOST_TEST_EQ(received, count);
    }
    if (cancel_after != 0) {
        BOOST_TEST_GE(received, cancel_after);
        BOOST_TEST_LE(received, cancel_after + 129);
    }
    std::printf("spsc: count=%d checksum=%lld cancelled=%d\n", received,
                static_cast<long long>(checksum), stop.load() ? 1 : 0);
    const int errors = boost::report_errors();
    cobalt_event(errors != 0 ? "error" : "done", received, errors, static_cast<int>(stop.load()));
    co_return webcpp::test::exit_status(errors);
}

// end::program[]
