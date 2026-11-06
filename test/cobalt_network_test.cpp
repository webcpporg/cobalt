// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

// Exercise Asio sockets through Cobalt's completion token, not Cobalt's I/O wrappers.
#include <webcpp/asio/ip/tcp.hpp>
#include <webcpp/asio/ip/udp.hpp>
#include <webcpp/test/exit_status.hpp>

#include <boost/asio/post.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/write.hpp>
#include <boost/cobalt/main.hpp>
#include <boost/cobalt/op.hpp>
#include <boost/cobalt/result.hpp>
#include <boost/cobalt/this_coro.hpp>
#include <boost/core/lightweight_test.hpp>

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace asio = boost::asio;
namespace cobalt = boost::cobalt;
using tcp = webcpp::asio::ip::tcp;
using udp = webcpp::asio::ip::udp;

cobalt::main co_main([[maybe_unused]] int argc, [[maybe_unused]] char** argv) {
    // NOLINTNEXTLINE(readability-static-accessed-through-instance)
    const auto exec = co_await cobalt::this_coro::executor;
    asio::steady_timer watchdog(exec, std::chrono::seconds(5));
    watchdog.async_wait([](boost::system::error_code error) {
        if (!error) {
            static_cast<void>(std::fputs("cobalt network test timed out\n", stderr));
            std::abort();
        }
    });

    tcp::acceptor acceptor(exec, {asio::ip::address_v4::loopback(), 0});
    tcp::socket server(exec);
    tcp::socket client(exec);
    asio::steady_timer accepted(exec, std::chrono::steady_clock::time_point::max());
    bool accept_done = false;
    boost::system::error_code accept_error;
    acceptor.async_accept(server, [&](boost::system::error_code error) {
        accept_error = error;
        accept_done = true;
        accepted.cancel();
    });
    const auto [connect_error] =
        co_await cobalt::as_tuple(client.async_connect(acceptor.local_endpoint(), cobalt::use_op));
    if (!BOOST_TEST(!connect_error)) {
        co_return webcpp::test::exit_status(boost::report_errors());
    }
    if (!accept_done) {
        const auto [wait_error] = co_await cobalt::as_tuple(accepted.async_wait(cobalt::use_op));
        BOOST_TEST(wait_error == asio::error::operation_aborted);
    }
    if (!BOOST_TEST(accept_done && !accept_error)) {
        co_return webcpp::test::exit_status(boost::report_errors());
    }

    constexpr std::string_view message = "cobalt network";
    std::array<char, message.size()> incoming{};
    const auto [write_error, written] =
        co_await cobalt::as_tuple(asio::async_write(client, asio::buffer(message), cobalt::use_op));
    if (!BOOST_TEST(!write_error)) {
        co_return webcpp::test::exit_status(boost::report_errors());
    }
    BOOST_TEST_EQ(written, message.size());
    const auto [read_error, received] =
        co_await cobalt::as_tuple(asio::async_read(server, asio::buffer(incoming), cobalt::use_op));
    BOOST_TEST(!read_error);
    BOOST_TEST_EQ(received, message.size());
    BOOST_TEST(std::string_view(incoming.data(), received) == message);

    asio::post(exec, [&client] { client.cancel(); });
    const auto [cancel_error, cancelled_bytes] =
        co_await cobalt::as_tuple(client.async_read_some(asio::buffer(incoming), cobalt::use_op));
    BOOST_TEST(cancel_error == asio::error::operation_aborted);
    BOOST_TEST_EQ(cancelled_bytes, 0U);
    server.shutdown(tcp::socket::shutdown_send);
    const auto [eof_error, eof_bytes] =
        co_await cobalt::as_tuple(client.async_read_some(asio::buffer(incoming), cobalt::use_op));
    BOOST_TEST(eof_error == asio::error::eof);
    BOOST_TEST_EQ(eof_bytes, 0U);

    udp::socket receiver(exec, {asio::ip::address_v4::loopback(), 0});
    udp::socket sender(exec, udp::v4());
    udp::endpoint peer;
    const auto [send_error, sent] = co_await cobalt::as_tuple(
        sender.async_send_to(asio::buffer(message), receiver.local_endpoint(), cobalt::use_op));
    if (!BOOST_TEST(!send_error)) {
        co_return webcpp::test::exit_status(boost::report_errors());
    }
    BOOST_TEST_EQ(sent, message.size());
    incoming.fill('\0');
    const auto [receive_error, datagram_size] = co_await cobalt::as_tuple(
        receiver.async_receive_from(asio::buffer(incoming), peer, cobalt::use_op));
    BOOST_TEST(!receive_error);
    BOOST_TEST_EQ(datagram_size, message.size());
    BOOST_TEST(std::string_view(incoming.data(), datagram_size) == message);

    asio::post(exec, [&receiver] { receiver.cancel(); });
    const auto [udp_cancel_error, udp_cancel_size] = co_await cobalt::as_tuple(
        receiver.async_receive_from(asio::buffer(incoming), peer, cobalt::use_op));
    BOOST_TEST(udp_cancel_error == asio::error::operation_aborted);
    BOOST_TEST_EQ(udp_cancel_size, 0U);
    watchdog.cancel();
    std::puts("cobalt: TCP, UDP, cancellation and EOF checked");
    co_return webcpp::test::exit_status(boost::report_errors());
}
