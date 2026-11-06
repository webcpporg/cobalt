// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

// The public target must link Cobalt's compiled I/O wrappers and core APIs.
#include <webcpp/test/exit_status.hpp>

#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/cobalt.hpp>
#include <boost/cobalt/io.hpp>
#include <boost/core/lightweight_test.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace cobalt = boost::cobalt;
namespace io = cobalt::io;

namespace {

// GCC 14 misidentifies Cobalt's placement coroutine allocation and sized delete.
// Both use the resource stored before the frame; GCC 15 accepts these functions.
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 14
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif

cobalt::promise<void> accept_client(io::acceptor& listener, io::stream_socket& peer) {
    const auto [error] = co_await cobalt::as_tuple(listener.accept(peer));
    BOOST_TEST(!error);
}

cobalt::task<int> answer() {
    co_return 42;
}

cobalt::task<void> refused_connection(const cobalt::executor& exec) {
    // Select a local port, then close it: a bound, non-listening socket can time out
    // instead of refusing connections on macOS.
    io::stream_socket reservation({io::tcp_v4, "127.0.0.1", 0}, exec);
    const auto endpoint = reservation.local_endpoint();
    if (!BOOST_TEST(endpoint.has_value())) {
        co_return;
    }
    BOOST_TEST(reservation.close().has_value());
    io::stream_socket client(exec);
    const auto [error] = co_await cobalt::as_tuple(client.connect(*endpoint));
    BOOST_TEST_EQ(error,
                  boost::asio::error::make_error_code(boost::asio::error::connection_refused));
}

#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 14
#pragma GCC diagnostic pop
#endif

}  // namespace

cobalt::main co_main([[maybe_unused]] int argc, [[maybe_unused]] char** argv) {
    // NOLINTNEXTLINE(readability-static-accessed-through-instance)
    const auto exec = co_await cobalt::this_coro::executor;
    boost::asio::steady_timer watchdog(exec, std::chrono::seconds(10));
    watchdog.async_wait([](boost::system::error_code error) {
        if (!error) {
            std::abort();
        }
    });
    BOOST_TEST_EQ(co_await answer(), 42);

    io::acceptor listener({io::tcp_v4, "127.0.0.1", 0}, exec);
    io::stream_socket server(exec);
    io::stream_socket client(exec);
    auto accepting = accept_client(listener, server);
    const auto [connect_error] =
        co_await cobalt::as_tuple(client.connect(listener.local_endpoint()));
    if (!BOOST_TEST(!connect_error)) {
        co_return webcpp::test::exit_status(boost::report_errors());
    }
    co_await accepting;
    constexpr std::string_view message = "Cobalt I/O on WASIp3";
    std::array<char, message.size()> bytes{};
    const auto [write_error, written] =
        co_await cobalt::as_tuple(io::write(client, io::buffer(message)));
    BOOST_TEST(!write_error);
    BOOST_TEST_EQ(written, message.size());
    const auto [read_error, received] =
        co_await cobalt::as_tuple(io::read(server, io::buffer(bytes)));
    BOOST_TEST(!read_error);
    BOOST_TEST_EQ(received, message.size());
    BOOST_TEST(std::string_view(bytes.data(), received) == message);

    // A small receive buffer forces observable partial reads independently of
    // the host's TCP segmentation. Send the same bytes using gathered buffers.
    const std::array<boost::asio::const_buffer, 2> pieces{io::buffer(message.substr(0, 3)),
                                                          io::buffer(message.substr(3))};
    const auto [gather_error, gather_size] = co_await cobalt::as_tuple(io::write(client, pieces));
    BOOST_TEST(!gather_error);
    BOOST_TEST_EQ(gather_size, message.size());
    std::size_t offset = 0;
    int read_count = 0;
    while (offset != bytes.size()) {
        const auto [partial_error, count] = co_await cobalt::as_tuple(server.read_some(
            io::buffer(bytes.data() + offset, std::min<std::size_t>(2, bytes.size() - offset))));
        if (!BOOST_TEST(!partial_error && count > 0)) {
            co_return webcpp::test::exit_status(boost::report_errors());
        }
        offset += count;
        ++read_count;
    }
    BOOST_TEST(read_count > 1);
    BOOST_TEST(std::string_view(bytes.data(), offset) == message);
    boost::asio::post(exec, [&client] { BOOST_TEST(client.cancel().has_value()); });
    const auto [cancel_error, cancelled] =
        co_await cobalt::as_tuple(client.read_some(io::buffer(bytes)));
    BOOST_TEST(cancel_error == boost::asio::error::operation_aborted);
    BOOST_TEST_EQ(cancelled, 0U);
    BOOST_TEST(server.shutdown(io::socket::shutdown_type::shutdown_send).has_value());
    const auto [eof_error, eof_size] =
        co_await cobalt::as_tuple(client.read_some(io::buffer(bytes)));
    BOOST_TEST(eof_error == boost::asio::error::eof);
    BOOST_TEST_EQ(eof_size, 0U);

    co_await refused_connection(exec);

    io::datagram_socket left({io::udp_v4, "127.0.0.1", 0}, exec);
    io::datagram_socket right({io::udp_v4, "127.0.0.1", 0}, exec);
    const auto left_endpoint = left.local_endpoint();
    const auto right_endpoint = right.local_endpoint();
    if (!BOOST_TEST(left_endpoint && right_endpoint)) {
        co_return webcpp::test::exit_status(boost::report_errors());
    }
    const auto [left_error] = co_await cobalt::as_tuple(left.connect(*right_endpoint));
    const auto [right_error] = co_await cobalt::as_tuple(right.connect(*left_endpoint));
    BOOST_TEST(!left_error && !right_error);
    const auto [send_error, sent] = co_await cobalt::as_tuple(left.send(io::buffer(message)));
    BOOST_TEST(!send_error);
    BOOST_TEST_EQ(sent, message.size());
    bytes.fill('\0');
    const auto [receive_error, size] = co_await cobalt::as_tuple(right.receive(io::buffer(bytes)));
    BOOST_TEST(!receive_error);
    BOOST_TEST_EQ(size, message.size());
    BOOST_TEST(std::string_view(bytes.data(), size) == message);

    const auto [gather_send_error, gather_sent] = co_await cobalt::as_tuple(left.send(pieces));
    BOOST_TEST(!gather_send_error);
    BOOST_TEST_EQ(gather_sent, message.size());
    bytes.fill('\0');
    const auto [gather_receive_error, gather_received] =
        co_await cobalt::as_tuple(right.receive(io::buffer(bytes)));
    BOOST_TEST(!gather_receive_error);
    BOOST_TEST_EQ(gather_received, message.size());
    BOOST_TEST(std::string_view(bytes.data(), gather_received) == message);

    const auto [empty_send_error, empty_sent] =
        co_await cobalt::as_tuple(left.send(io::buffer(message.data(), 0)));
    BOOST_TEST(!empty_send_error);
    BOOST_TEST_EQ(empty_sent, 0U);
    const auto [empty_receive_error, empty_received] =
        co_await cobalt::as_tuple(right.receive(io::buffer(bytes)));
    BOOST_TEST(!empty_receive_error);
    BOOST_TEST_EQ(empty_received, 0U);
    boost::asio::post(exec, [&right] { BOOST_TEST(right.cancel().has_value()); });
    const auto [udp_cancel_error, udp_cancelled] =
        co_await cobalt::as_tuple(right.receive(io::buffer(bytes)));
    BOOST_TEST(udp_cancel_error == boost::asio::error::operation_aborted);
    BOOST_TEST_EQ(udp_cancelled, 0U);

    io::resolver resolver(exec);
    const auto [resolve_error, endpoints] = co_await cobalt::as_tuple(
        resolver.resolve("127.0.0.1", "443", io::resolver::flags::numeric_host));
    BOOST_TEST(!resolve_error);
    BOOST_TEST(!endpoints.empty());
    const auto [host_error, host_endpoints] =
        co_await cobalt::as_tuple(resolver.resolve("localhost", "443"));
    BOOST_TEST(!host_error);
    BOOST_TEST(!host_endpoints.empty());
    watchdog.cancel();
    std::puts("cobalt: compiled I/O, task, TCP, UDP, cancellation, EOF and resolver checked");
    co_return webcpp::test::exit_status(boost::report_errors());
}
