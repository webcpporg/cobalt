// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

#include <webcpp/test/exit_status.hpp>

#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/cobalt/io/acceptor.hpp>
#include <boost/cobalt/io/read.hpp>
#include <boost/cobalt/io/ssl.hpp>
#include <boost/cobalt/io/write.hpp>
#include <boost/cobalt/main.hpp>
#include <boost/cobalt/op.hpp>
#include <boost/cobalt/promise.hpp>
#include <boost/cobalt/task.hpp>
#include <boost/core/lightweight_test.hpp>

#include <poll.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <source_location>
#include <string_view>
#include <tuple>
#include <vector>

namespace cobalt = boost::cobalt;
namespace io = cobalt::io;
namespace asio = boost::asio;

namespace {

using transfer_result = std::tuple<boost::system::error_code, std::size_t>;

void require(bool condition, std::source_location location = std::source_location::current()) {
    if (!BOOST_TEST(condition)) {
        static_cast<void>(std::fprintf(stderr, "Precondition failed at %s:%u\n",
                                       location.file_name(), location.line()));
        std::abort();
    }
}

cobalt::promise<void> accept(io::acceptor& listener, io::socket& peer) {
    const auto [error] = co_await cobalt::as_tuple(listener.accept(peer));
    require(!error);
}

cobalt::promise<void> handshake(io::ssl::stream& stream) {
    const auto [error] =
        co_await cobalt::as_tuple(stream.handshake(asio::ssl::stream_base::server));
    require(!error && stream.secure());
}

struct tls_pair {
    io::ssl::stream client;
    io::ssl::stream peer;
    cobalt::executor exec;

    tls_pair(io::ssl::context& client_context, io::ssl::context& server_context,
             cobalt::executor executor)
        : client(client_context, executor),
          peer(server_context, executor),
          exec(std::move(executor)) {}

    cobalt::task<void> connect() {
        io::acceptor listener({io::tcp_v4, "127.0.0.1", 0}, exec);
        auto accepted = accept(listener, peer);
        const auto [error] = co_await cobalt::as_tuple(client.connect(listener.local_endpoint()));
        require(!error);
        co_await accepted;
        auto server_handshake = handshake(peer);
        const auto [handshake_error] =
            co_await cobalt::as_tuple(client.handshake(asio::ssl::stream_base::client));
        require(!handshake_error && client.secure());
        co_await server_handshake;
    }
};

cobalt::promise<transfer_result> read_transfer(io::ssl::stream& stream, io::mutable_buffer buffer,
                                               bool composed) {
    if (composed) {
        co_return co_await cobalt::as_tuple(io::read(stream, buffer));
    }
    co_return co_await cobalt::as_tuple(stream.read_some(buffer));
}

cobalt::promise<transfer_result> write_transfer(io::ssl::stream& stream, io::const_buffer buffer,
                                                bool composed) {
    if (composed) {
        co_return co_await cobalt::as_tuple(io::write(stream, buffer));
    }
    co_return co_await cobalt::as_tuple(stream.write_some(buffer));
}

cobalt::promise<boost::system::error_code> shutdown(io::ssl::stream& stream) {
    const auto [error] = co_await cobalt::as_tuple(stream.shutdown());
    co_return error;
}

cobalt::task<void> cancel_read(tls_pair& pair, bool composed) {
    std::array<char, 8> data{};
    auto reading = read_transfer(pair.client, io::buffer(data), composed);
    if (composed) {
        const auto [error, size] =
            co_await cobalt::as_tuple(io::write(pair.peer, io::buffer(std::string_view("ping"))));
        require(!error && size == 4);
        // Observe delivered bytes, not a guessed delay, before cancelling the remainder.
        while (data[3] != 'g') {
            co_await asio::post(pair.exec, cobalt::use_op);
        }
        require(std::string_view(data.data(), 4) == "ping");
    } else {
        co_await asio::post(pair.exec, cobalt::use_op);
    }
    require(!reading.ready());
    std::puts("Cancelling pending TLS read");
    static_cast<void>(std::fflush(stdout));
    reading.cancel();
    const auto [error, size] = co_await reading;
    BOOST_TEST(error == asio::error::operation_aborted);
    BOOST_TEST_EQ(size, composed ? 4U : 0U);
    BOOST_TEST(pair.client.secure());
    // Terminal cancellation ends this connection's use; it does not undo TLS negotiation.
    require(static_cast<bool>(pair.client.close()));
    require(static_cast<bool>(pair.peer.close()));
}

cobalt::task<int> wait_for_blocked_write(tls_pair& pair,
                                         const cobalt::promise<transfer_result>& writing,
                                         const cobalt::promise<transfer_result>& receiving,
                                         bool can_cancel) {
    pollfd descriptor{.fd = pair.client.native_handle(), .events = POLLOUT, .revents = 0};
    int ready = 1;
    while (!writing.ready()) {
        co_await asio::post(pair.exec, cobalt::use_op);
        ready = ::poll(&descriptor, 1, 0);
        constexpr unsigned failure_events = static_cast<unsigned>(POLLERR) |
                                            static_cast<unsigned>(POLLHUP) |
                                            static_cast<unsigned>(POLLNVAL);
        require(ready >= 0 && (static_cast<unsigned>(descriptor.revents) & failure_events) == 0);
        if (ready == 0 && receiving.ready() && can_cancel) {
            // WASIp2 may already have a successful SSL completion queued while
            // libc reports no current write permit. Let the reactor progress,
            // then require that the same operation is still blocked before
            // cancelling it. This timer is an event-loop barrier, not evidence
            // of backpressure; the second poll and promise state provide that.
            asio::steady_timer barrier(pair.exec, std::chrono::milliseconds(1));
            const auto [barrier_error] =
                co_await cobalt::as_tuple(barrier.async_wait(cobalt::use_op));
            require(!barrier_error);
            descriptor.revents = 0;
            ready = ::poll(&descriptor, 1, 0);
            require(ready >= 0 &&
                    (static_cast<unsigned>(descriptor.revents) & failure_events) == 0);
            if (ready == 0 && !writing.ready()) {
                break;
            }
        }
    }
    co_return ready;
}

cobalt::task<bool> cancel_write(tls_pair& pair, bool composed) {
    require(static_cast<bool>(pair.client.set_send_buffer_size(4096)));
    require(static_cast<bool>(pair.peer.set_receive_buffer_size(4096)));
    const auto send_capacity = pair.client.get_send_buffer_size();
    const auto receive_capacity = pair.peer.get_receive_buffer_size();
    require(send_capacity && receive_capacity);
    require(*send_capacity > 0 && *receive_capacity > 0);
    std::vector<char> data(std::size_t{16} * 1024U * 1024U, 'x');
    require(static_cast<std::size_t>(*send_capacity) + static_cast<std::size_t>(*receive_capacity) <
            data.size() / 2);
    std::array<char, 1> received{};
    auto receiving = read_transfer(pair.peer, io::buffer(received), false);
    std::size_t total = 0;
    bool interrupted = false;
    while (total < data.size()) {
        // One promise per primitive write: a cancelled promise must not start another await.
        auto writing = write_transfer(pair.client, io::buffer(data) + total, composed);
        const int ready =
            co_await wait_for_blocked_write(pair, writing, receiving, composed || total != 0);
        const bool cancel = !writing.ready();
        if (cancel) {
            require(ready == 0 && receiving.ready());
            writing.cancel();
        }
        const auto [error, written] = co_await writing;
        BOOST_TEST_LE(written, data.size() - total);
        total += written;
        if (error) {
            BOOST_TEST(error == asio::error::operation_aborted);
            require(cancel);
            interrupted = error == asio::error::operation_aborted;
            break;
        }
        require(written != 0);
        if (cancel) {
            // TCP backpressure cannot rule out an already-queued successful SSL completion.
            // Do not manufacture an error or reuse a terminally cancelled connection.
            std::puts("Cancellation raced a successful TLS write; retry with a fresh connection");
            break;
        }
    }
    const auto [read_error, read_size] = co_await receiving;
    require(!read_error && read_size == 1 && received[0] == 'x');
    BOOST_TEST_LT(total, data.size());
    BOOST_TEST(pair.client.secure());
    static_cast<void>(std::printf("TLS %s write: %zu plaintext bytes, interrupted=%d\n",
                                  composed ? "composed" : "primitive", total,
                                  static_cast<int>(interrupted)));
    require(static_cast<bool>(pair.client.close()));
    require(static_cast<bool>(pair.peer.close()));
    co_return interrupted;
}

cobalt::task<void> closed_state(io::ssl::stream& stream) {
    if (!BOOST_TEST(!stream.secure())) {
        co_return;
    }
    std::array<char, 1> data{};
    const auto [read_error, read_size] =
        co_await cobalt::as_tuple(stream.read_some(io::buffer(data)));
    BOOST_TEST(read_error == asio::error::not_connected);
    BOOST_TEST_EQ(read_size, 0U);
    const auto [write_error, write_size] =
        co_await cobalt::as_tuple(stream.write_some(io::buffer(data)));
    BOOST_TEST(write_error == asio::error::not_connected);
    BOOST_TEST_EQ(write_size, 0U);
    const auto [repeat_error] = co_await cobalt::as_tuple(stream.shutdown());
    BOOST_TEST(repeat_error == asio::error::no_protocol_option);
}

cobalt::task<void> interrupted_shutdown(tls_pair& pair, bool cancel) {
    auto closing = shutdown(pair.client);
    std::array<char, 1> data{};
    const auto [peer_error, size] =
        co_await cobalt::as_tuple(pair.peer.read_some(io::buffer(data)));
    require(peer_error == asio::error::eof && size == 0);
    const auto flags = static_cast<unsigned>(SSL_get_shutdown(pair.peer.native_ssl_handle()));
    require((flags & SSL_RECEIVED_SHUTDOWN) != 0 && (flags & SSL_SENT_SHUTDOWN) == 0);
    require(!closing.ready());
    if (cancel) {
        closing.cancel();
    } else {
        // Close TCP without replying with close_notify: this must not become clean TLS EOF.
        require(static_cast<bool>(pair.peer.close()));
    }
    const auto error = co_await closing;
    if (cancel) {
        BOOST_TEST(error == asio::error::operation_aborted);
    } else {
        BOOST_TEST(error == asio::ssl::error::stream_truncated);
    }
    co_await closed_state(pair.client);
}

cobalt::task<bool> run_case(tls_pair& pair, std::string_view name) {
    if (name == "read_some" || name == "read") {
        co_await cancel_read(pair, name == "read");
    } else if (name == "write_some" || name == "write") {
        co_return co_await cancel_write(pair, name == "write");
    } else {
        co_await interrupted_shutdown(pair, name == "shutdown_cancel");
    }
    co_return true;
}

}  // namespace

cobalt::main co_main(int argc, char** argv) {
    if (argc != 5 && argc != 6) {
        co_return 1;
    }
    // NOLINTNEXTLINE(readability-static-accessed-through-instance)
    const auto exec = co_await cobalt::this_coro::executor;
    asio::steady_timer watchdog(exec, std::chrono::seconds(10));
    watchdog.async_wait([](boost::system::error_code error) {
        if (!error) {
            static_cast<void>(std::fputs("FAIL TLS cancellation watchdog\n", stderr));
            std::abort();
        }
    });
    io::ssl::context client(asio::ssl::context::tls_client);
    io::ssl::context server(asio::ssl::context::tls_server);
    client.add_certificate_authority(asio::buffer(std::string_view(argv[3])));
    client.set_verify_mode(asio::ssl::verify_peer);
    server.use_certificate_chain(asio::buffer(std::string_view(argv[1])));
    server.use_private_key(asio::buffer(std::string_view(argv[2])), asio::ssl::context::pem);
    const int version = std::string_view(argv[4]) == "1.2" ? TLS1_2_VERSION : TLS1_3_VERSION;
    require(SSL_CTX_set_min_proto_version(client.native_handle(), version) == 1);
    require(SSL_CTX_set_max_proto_version(client.native_handle(), version) == 1);
    require(SSL_CTX_set_min_proto_version(server.native_handle(), version) == 1);
    require(SSL_CTX_set_max_proto_version(server.native_handle(), version) == 1);
    bool selected = false;
    for (const auto* name :
         {"read_some", "write_some", "read", "write", "shutdown_error", "shutdown_cancel"}) {
        if (argc == 6 && std::string_view(name) != argv[5]) {
            continue;
        }
        selected = true;
        static_cast<void>(std::printf("BEGIN TLS %s\n", name));
        static_cast<void>(std::fflush(stdout));
        bool verified = false;
        for (int attempt = 0; !verified && attempt < 8; ++attempt) {
            tls_pair pair(client, server, exec);
            co_await pair.connect();
            verified = co_await run_case(pair, name);
        }
        require(verified);
        static_cast<void>(std::printf("Completed TLS %s\n", name));
    }
    BOOST_TEST(selected);
    watchdog.cancel();
    co_return webcpp::test::exit_status(boost::report_errors());
}
