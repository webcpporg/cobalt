// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

#include <webcpp/test/exit_status.hpp>

#include <boost/core/lightweight_test.hpp>
#include <webcpp/asio/post.hpp>
#include <webcpp/asio/steady_timer.hpp>
#include <webcpp/cobalt/io/acceptor.hpp>
#include <webcpp/cobalt/io/read.hpp>
#include <webcpp/cobalt/io/ssl.hpp>
#include <webcpp/cobalt/io/write.hpp>
#include <webcpp/cobalt/main.hpp>
#include <webcpp/cobalt/promise.hpp>
#include <webcpp/cobalt/task.hpp>

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace cobalt = webcpp::cobalt;
namespace io = cobalt::io;
namespace asio = webcpp::asio;

namespace {

cobalt::promise<void> serve(io::acceptor& listener, io::ssl::context& context,
                            const cobalt::executor& exec) {
    io::stream_socket socket(exec);
    const auto [accepted] = co_await cobalt::as_tuple(listener.accept(socket));
    if (!BOOST_TEST(!accepted)) {
        co_return;
    }
    std::array<char, 5> start{};
    const auto [read_start, start_size] =
        co_await cobalt::as_tuple(io::read(socket, io::buffer(start)));
    BOOST_TEST(!read_start);
    BOOST_TEST_EQ(start_size, start.size());
    BOOST_TEST(std::string_view(start.data(), start.size()) == "start");
    const auto [write_ready, ready_size] =
        co_await cobalt::as_tuple(io::write(socket, io::buffer(std::string_view("ready"))));
    BOOST_TEST(!write_ready);
    BOOST_TEST_EQ(ready_size, 5U);

    // Read actual ClientHello bytes before transferring the socket into TLS.
    std::array<char, 16384> hello{};
    const auto [read_hello, hello_size] =
        co_await cobalt::as_tuple(socket.read_some(io::buffer(hello)));
    if (!BOOST_TEST(!read_hello && hello_size != 0)) {
        co_return;
    }
    io::ssl::stream stream(context, std::move(socket));
    auto shutdown = stream.shutdown();
    auto duplicate = stream.handshake(asio::ssl::stream_base::server);
    const auto [handshake_error, consumed] = co_await cobalt::as_tuple(
        stream.handshake(asio::ssl::stream_base::server, io::buffer(hello.data(), hello_size)));
    if (!BOOST_TEST(!handshake_error && stream.secure())) {
        co_return;
    }
    BOOST_TEST_EQ(consumed, hello_size);
    // Operations created before the handshake must observe the current state.
    const auto [duplicate_error] = co_await cobalt::as_tuple(duplicate);
    BOOST_TEST(duplicate_error == asio::error::already_started);
    std::array<char, 4> request{};
    const auto [read_error, received] =
        co_await cobalt::as_tuple(io::read(stream, io::buffer(request)));
    BOOST_TEST(!read_error);
    BOOST_TEST_EQ(received, 4U);
    BOOST_TEST(std::string_view(request.data(), request.size()) == "ping");
    const auto [write_error, written] =
        co_await cobalt::as_tuple(io::write(stream, io::buffer(std::string_view("pong"))));
    BOOST_TEST(!write_error);
    BOOST_TEST_EQ(written, 4U);
    const auto [shutdown_error] = co_await cobalt::as_tuple(shutdown);
    BOOST_TEST(!shutdown_error);
    BOOST_TEST(!stream.secure());
}

cobalt::task<void> starttls(io::ssl::context& client_context, io::ssl::context& server_context,
                            const cobalt::executor& exec) {
    io::acceptor listener({io::tcp_v4, "127.0.0.1", 0}, exec);
    auto server = serve(listener, server_context, exec);
    io::ssl::stream stream(client_context, exec);
    const auto [connected] = co_await cobalt::as_tuple(stream.connect(listener.local_endpoint()));
    BOOST_TEST(!connected);
    const auto [write_start, start_size] =
        co_await cobalt::as_tuple(io::write(stream, io::buffer(std::string_view("start"))));
    BOOST_TEST(!write_start);
    BOOST_TEST_EQ(start_size, 5U);
    std::array<char, 5> ready{};
    const auto [read_ready, ready_size] =
        co_await cobalt::as_tuple(io::read(stream, io::buffer(ready)));
    BOOST_TEST(!read_ready);
    BOOST_TEST_EQ(ready_size, 5U);
    BOOST_TEST(std::string_view(ready.data(), ready.size()) == "ready");
    const auto [handshake_error] =
        co_await cobalt::as_tuple(stream.handshake(asio::ssl::stream_base::client));
    BOOST_TEST(!handshake_error);
    BOOST_TEST(stream.secure());
    const auto [write_error, written] =
        co_await cobalt::as_tuple(io::write(stream, io::buffer(std::string_view("ping"))));
    BOOST_TEST(!write_error);
    BOOST_TEST_EQ(written, 4U);
    std::array<char, 4> response{};
    const auto [read_error, received] =
        co_await cobalt::as_tuple(io::read(stream, io::buffer(response)));
    BOOST_TEST(!read_error);
    BOOST_TEST_EQ(received, 4U);
    BOOST_TEST(std::string_view(response.data(), response.size()) == "pong");
    const auto [shutdown_error] = co_await cobalt::as_tuple(stream.shutdown());
    BOOST_TEST(!shutdown_error);
    BOOST_TEST(!stream.secure());
    const auto [closed_error, closed_size] =
        co_await cobalt::as_tuple(stream.write_some(io::buffer(response)));
    BOOST_TEST(closed_error == asio::error::not_connected);
    BOOST_TEST_EQ(closed_size, 0U);
    co_await server;
}

cobalt::promise<boost::system::error_code> handshake(io::ssl::stream& stream) {
    const auto [error] =
        co_await cobalt::as_tuple(stream.handshake(asio::ssl::stream_base::client));
    co_return error;
}

cobalt::promise<void> accept(io::acceptor& listener, io::stream_socket& peer) {
    const auto [error] = co_await cobalt::as_tuple(listener.accept(peer));
    BOOST_TEST(!error);
}

cobalt::task<void> cancel_handshake(io::ssl::context& context, const cobalt::executor& exec) {
    io::acceptor listener({io::tcp_v4, "127.0.0.1", 0}, exec);
    io::stream_socket peer(exec);
    auto accepted = accept(listener, peer);
    io::ssl::stream stream(context, exec);
    const auto [connected] = co_await cobalt::as_tuple(stream.connect(listener.local_endpoint()));
    BOOST_TEST(!connected);
    co_await accepted;
    auto operation = handshake(stream);
    // The peer reads ClientHello but deliberately never starts TLS.
    std::array<char, 4096> hello{};
    const auto [hello_error, hello_size] =
        co_await cobalt::as_tuple(peer.read_some(io::buffer(hello)));
    BOOST_TEST(!hello_error);
    BOOST_TEST_GT(hello_size, 0U);
    BOOST_TEST(!operation.ready());
    asio::post(exec, [&operation] { operation.cancel(); });
    const auto error = co_await operation;
    BOOST_TEST(error == asio::error::operation_aborted);
    BOOST_TEST(!stream.secure());
    const auto [write_error, size] = co_await cobalt::as_tuple(
        stream.write_some(io::buffer(std::string_view("must not escape"))));
    BOOST_TEST(write_error == asio::error::not_connected);
    BOOST_TEST_EQ(size, 0U);
    const auto [retry] =
        co_await cobalt::as_tuple(stream.handshake(asio::ssl::stream_base::client));
    BOOST_TEST(retry == asio::error::already_started);
}

}  // namespace

cobalt::main co_main(int argc, char** argv) {
    if (argc != 5) {
        co_return 1;
    }
    // NOLINTNEXTLINE(readability-static-accessed-through-instance)
    const auto exec = co_await cobalt::this_coro::executor;
    asio::steady_timer watchdog(exec, std::chrono::seconds(10));
    watchdog.async_wait([](boost::system::error_code error) {
        if (!error) {
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
    BOOST_TEST_EQ(SSL_CTX_set_min_proto_version(client.native_handle(), version), 1);
    BOOST_TEST_EQ(SSL_CTX_set_max_proto_version(client.native_handle(), version), 1);
    BOOST_TEST_EQ(SSL_CTX_set_min_proto_version(server.native_handle(), version), 1);
    BOOST_TEST_EQ(SSL_CTX_set_max_proto_version(server.native_handle(), version), 1);
    co_await starttls(client, server, exec);
    co_await cancel_handshake(client, exec);
    watchdog.cancel();
    std::puts("Cobalt SSL: STARTTLS, buffered ClientHello, state and cancellation checked");
    co_return webcpp::test::exit_status(boost::report_errors());
}
