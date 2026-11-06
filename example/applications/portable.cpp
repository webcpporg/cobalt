// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

#include <webcpp/cobalt.hpp>
#include <webcpp/test/exit_status.hpp>

#include "signal_awaitable.hpp"

#include <boost/asio/steady_timer.hpp>
#include <boost/outcome/coroutine_support.hpp>

#include <charconv>
#include <chrono>
#include <cstdio>
#include <string_view>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
EM_JS(void, cobalt_event, (const char* type, int count, int value, int cancelled), {
    if (Module.onCobaltEvent) {
        Module.onCobaltEvent(
            {type : UTF8ToString(type), count, value, cancelled : Boolean(cancelled)});
    }
});
#else
static void cobalt_event(const char*, int, int, int) {}
#endif

namespace asio = boost::asio;

namespace {

// These finite counterparts retain the five upstream algorithms. Added CLI
// bounds, active Release checks and events are shared by CLI and browser hosts.
struct run_state {
    asio::steady_timer timer;
    int count = 4;
    int delay = 2;
    int cancel_after = 0;
    int completed = 0;
    int failures = 0;
    int ready = 0;
    int initiated = 0;
    bool cancelled = false;

    explicit run_state(const cobalt::executor& executor) : timer(executor) {}

    void cancel() {
        cancelled = true;
        timer.cancel();
    }

    void progress(int value) {
        ++completed;
        std::printf("progress: count=%d value=%d\n", completed, value);
        cobalt_event("progress", completed, value, cancelled);
        if (cancel_after != 0 && completed == cancel_after) {
            cancel();
        }
    }
};

run_state* active = nullptr;

// tag::custom-operation[]
struct wait_op final : cobalt::op<boost::system::error_code> {
    run_state& state;

    explicit wait_op(run_state& value) : state(value) {}

    void ready(cobalt::handler<boost::system::error_code> handler) override {
        if (state.timer.expiry() < std::chrono::steady_clock::now()) {
            ++state.ready;
            handler(boost::system::error_code{});
        }
    }

    void initiate(cobalt::completion_handler<boost::system::error_code> complete) override {
        ++state.initiated;
        state.timer.async_wait(std::move(complete));
    }
};

// end::custom-operation[]

cobalt::promise<bool> pause(run_state& state, bool custom = false) {
    if (state.cancelled) {
        co_return false;
    }
    if (state.delay == 0) {
        // A past deadline makes the ready-path demonstration independent of
        // clock resolution and of whether two successive clock reads differ.
        state.timer.expires_at(std::chrono::steady_clock::time_point::min());
    } else {
        state.timer.expires_after(std::chrono::milliseconds(state.delay));
    }
    boost::system::error_code error;
    if (custom) {
        std::tie(error) = co_await cobalt::as_tuple(wait_op(state));
    } else {
        std::tie(error) = co_await cobalt::as_tuple(state.timer.async_wait(cobalt::use_op));
    }
    if (error && error != asio::error::operation_aborted) {
        ++state.failures;
    }
    co_return !error && !state.cancelled;
}

// tag::channel[]
cobalt::promise<void> produce(cobalt::channel<int>& channel, run_state& state) {
    for (int value = 0; value < state.count; ++value) {
        if (!(co_await pause(state))) {
            break;
        }
        co_await channel.write(value);
    }
    // A terminal value drains the unbuffered channel before closing it. This
    // lets a UI cancellation stop a pending timer without abandoning a reader.
    co_await channel.write(-1);
}

cobalt::promise<void> channel_example(run_state& state) {
    cobalt::channel<int> channel;
    auto producer = produce(channel, state);
    for (;;) {
        const int value = co_await channel.read();
        if (value == -1) {
            break;
        }
        if (value != state.completed) {
            ++state.failures;
        }
        state.progress(value);
    }
    co_await producer;
    channel.close();
}

// end::channel[]

// tag::outcome[]
boost::outcome_v2::awaitables::lazy<int> lazy_value(int value) {
    co_return value + 1;
}

boost::outcome_v2::awaitables::eager<int> eager_value(int value) {
    co_return value + 1;
}

// end::outcome[]

cobalt::promise<void> scalar_examples(run_state& state, std::string_view kind) {
    for (int index = 0; index < state.count; ++index) {
        if (kind == "signals") {
            // The pending coroutine is owned by the single-shot connection.
            signals::signal<void(int)> signal;
            auto pending = await_signal(signal);
            if (signal.num_slots() != 1) {
                ++state.failures;
            }
            const bool delivered = co_await pause(state);
            signal(delivered ? 42 : -1);
            const int value = co_await pending;
            if (signal.num_slots() != 0 || value != (delivered ? 42 : -1)) {
                ++state.failures;
            }
            if (!delivered) {
                break;
            }
            state.progress(value);
        } else {
            if (!(co_await pause(state, kind == "custom"))) {
                break;
            }
            int value = 1;
            if (kind == "outcome") {
                const auto lazy = co_await lazy_value(10);
                const auto eager = co_await eager_value(10);
                if (lazy != 11 || eager != 11) {
                    ++state.failures;
                }
                value = lazy;
            }
            state.progress(value);
        }
    }
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
    int cobalt_cancel() {
    if (!active) {
        return 0;
    }
    active->cancel();
    return 1;
}

// tag::program[]
cobalt::main co_main(int argc, char** argv) {
    // NOLINTNEXTLINE(readability-static-accessed-through-instance)
    const auto executor = co_await cobalt::this_coro::executor;
    run_state state(executor);
    const std::string_view kind = argc > 1 ? argv[1] : "timer";
    int cancel_ms = 0;
    if (argc > 6 || (argc > 2 && !parse(argv[2], state.count)) ||
        (argc > 3 && !parse(argv[3], state.delay)) ||
        (argc > 4 && !parse(argv[4], state.cancel_after)) ||
        (argc > 5 && !parse(argv[5], cancel_ms)) ||
        (kind != "timer" && kind != "custom" && kind != "channel" && kind != "outcome" &&
         kind != "signals") ||
        state.count < 1 || state.count > 10000 || state.delay < 0 || state.delay > 1000 ||
        state.cancel_after < 0 || state.cancel_after >= state.count || cancel_ms < 0 ||
        cancel_ms > 60000) {
        std::fputs(
            "usage: portable <timer|custom|channel|outcome|signals> "
            "[count 1..10000] [delay-ms 0..1000] [cancel-after] [cancel-ms]\n",
            stderr);
        co_return webcpp::test::exit_status(2);
    }
    active = &state;
    asio::steady_timer canceller(executor, std::chrono::milliseconds(cancel_ms));
    if (cancel_ms != 0) {
        canceller.async_wait([&](boost::system::error_code error) {
            if (!error) {
                state.cancel();
            }
        });
    }
    cobalt_event("started", 0, 0, 0);
    if (kind == "channel") {
        co_await channel_example(state);
    } else {
        co_await scalar_examples(state, kind);
    }
    active = nullptr;
    canceller.cancel();
    // Drain the cancelled timer callback before its referenced state dies.
    co_await asio::post(executor, cobalt::use_op);
    std::printf("done: kind=%.*s count=%d cancelled=%d\n", static_cast<int>(kind.size()),
                kind.data(), state.completed, state.cancelled ? 1 : 0);
    if (kind == "custom") {
        std::printf("custom: ready=%d initiated=%d\n", state.ready, state.initiated);
    }
    cobalt_event(state.failures ? "error" : "done", state.completed, state.failures,
                 state.cancelled);
    co_return webcpp::test::exit_status(state.failures ? 1 : 0);
}

// end::program[]
