// Copyright (c) 2026 WebCpp.org
// Distributed under the Boost Software License, Version 1.0.

#include <webcpp/cobalt.hpp>

#include <boost/asio/steady_timer.hpp>
#include <boost/cobalt/io/acceptor.hpp>
#include <boost/cobalt/io/write.hpp>

#include <array>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <list>
#include <string_view>

namespace asio = boost::asio;
namespace cobalt = boost::cobalt;
namespace io = boost::cobalt::io;

namespace {
struct server_state {
    int accepted = 0;
    int completed = 0;
    int failures = 0;
    std::size_t bytes = 0;
    bool cancelled = false;
};

// The list constructs each session in place. Its named eager promise is never
// moved while pending, including in no-exceptions builds (Cobalt issue 287).
struct session {
    io::stream_socket socket;
    server_state& state;
    cobalt::promise<void> running;

    session(io::stream_socket value, server_state& owner)
        : socket(std::move(value)), state(owner), running(echo()) {}

    // tag::echo-session[]
    cobalt::promise<void> echo() {
        std::array<char, 257> buffer{};
        for (;;) {
            auto [error, count] = co_await cobalt::as_tuple(socket.read_some(asio::buffer(buffer)));
            if (error) {
                if (error != asio::error::eof &&
                    !(state.cancelled && error == asio::error::operation_aborted)) {
                    ++state.failures;
                }
                break;
            }
            auto [write_error, written] =
                co_await cobalt::as_tuple(io::write(socket, asio::buffer(buffer.data(), count)));
            state.bytes += written;
            if (write_error) {
                if (!(state.cancelled && write_error == asio::error::operation_aborted)) {
                    ++state.failures;
                }
                break;
            }
            if (written != count) {
                ++state.failures;
                break;
            }
        }
        if (!socket.close()) {
            ++state.failures;
        }
        ++state.completed;
    }

    // end::echo-session[]
};

cobalt::promise<void> accept_sessions(io::acceptor& listener, std::list<session>& sessions,
                                      server_state& state, int limit) {
    while (state.accepted < limit && !state.cancelled) {
        auto [error, socket] = co_await cobalt::as_tuple(listener.accept());
        if (error) {
            if (!(state.cancelled && error == asio::error::operation_aborted)) {
                ++state.failures;
            }
            break;
        }
        ++state.accepted;
        sessions.emplace_back(std::move(socket), state);
    }
}

bool parse(std::string_view text, int& value) {
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}
}  // namespace

// tag::echo-main[]
cobalt::main co_main(int argc, char** argv) {
    int port = 0;
    int limit = 0;
    int cancel_ms = 0;
    if ((argc != 3 && argc != 4) || !parse(argv[1], port) || !parse(argv[2], limit) ||
        (argc == 4 && !parse(argv[3], cancel_ms)) || port < 1 || port > 65535 || limit < 1 ||
        limit > 32 || cancel_ms < 0 || cancel_ms > 60000) {
        std::fputs("usage: echo_server <port 1..65535> <sessions 1..32> [cancel-ms]\n", stderr);
        co_return 2;
    }
    server_state state;
    std::list<session> sessions;
    io::acceptor listener(io::endpoint{io::tcp_v4, "127.0.0.1", port});
    auto accepting = accept_sessions(listener, sessions, state, limit);
    // NOLINTNEXTLINE(readability-static-accessed-through-instance)
    const auto executor = co_await cobalt::this_coro::executor;
    asio::steady_timer canceller(executor, std::chrono::milliseconds(cancel_ms));
    if (cancel_ms != 0) {
        canceller.async_wait([&](boost::system::error_code error) {
            if (!error) {
                state.cancelled = true;
                accepting.cancel();
                for (auto& current : sessions) {
                    if (current.socket.is_open() && !current.socket.cancel()) {
                        ++state.failures;
                    }
                }
            }
        });
    }
    std::printf("listening: port=%d limit=%d\n", port, limit);
    std::fflush(stdout);
    co_await accepting;
    for (auto& current : sessions) {
        co_await current.running;
    }
    canceller.cancel();
    // Drain the timer handler before the state and named promises are destroyed.
    co_await asio::post(executor, cobalt::use_op);
    std::printf("done: accepted=%d completed=%d bytes=%zu cancelled=%d failures=%d\n",
                state.accepted, state.completed, state.bytes, state.cancelled, state.failures);
    co_return state.failures ? 1 : 0;
}

// end::echo-main[]
