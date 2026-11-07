// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

#include <webcpp/asio/ip/udp.hpp>
#include <webcpp/asio/steady_timer.hpp>
#include <webcpp/cobalt/io/datagram_socket.hpp>
#include <webcpp/cobalt/main.hpp>
#include <webcpp/cobalt/op.hpp>
#include <webcpp/cobalt/this_coro.hpp>
#include <webcpp/test/exit_status.hpp>

#include <array>
#include <boost/core/lightweight_test.hpp>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace cobalt = webcpp::cobalt;
namespace io = cobalt::io;

cobalt::main co_main([[maybe_unused]] int argc, [[maybe_unused]] char** argv) {
    // NOLINTNEXTLINE(readability-static-accessed-through-instance)
    const auto exec = co_await cobalt::this_coro::executor;
    const auto udp = webcpp::asio::ip::udp::v6();
#ifdef WEBCPP_TARGET_NATIVE
    // Native identity preserves Boost 1.92's incorrect ICMP protocol constant.
    // The explicit protocol control proves that the OS can exchange IPv6 UDP.
    if (!BOOST_TEST(io::udp_v6.protocol() != udp.protocol())) {
        co_return webcpp::test::exit_status(boost::report_errors());
    }
    std::puts("UPSTREAM-LIMITATION Boost 1.92 udp_v6 selects ICMP; using explicit UDP control");
    constexpr io::static_protocol<io::udp_v6.family(), io::udp_v6.type(), io::udp_v4.protocol()>
        protocol{};
#else
    if (!BOOST_TEST_EQ(io::udp_v6.protocol(), udp.protocol())) {
        co_return webcpp::test::exit_status(boost::report_errors());
    }
    constexpr auto protocol = io::udp_v6;
#endif
    webcpp::asio::steady_timer watchdog(exec, std::chrono::seconds(5));
    watchdog.async_wait([](boost::system::error_code error) {
        if (!error) {
            std::abort();
        }
    });
    io::datagram_socket left({protocol, "::1", 0}, exec);
    io::datagram_socket right({protocol, "::1", 0}, exec);
    const auto left_endpoint = left.local_endpoint();
    const auto right_endpoint = right.local_endpoint();
    if (!BOOST_TEST(left_endpoint && right_endpoint)) {
        co_return webcpp::test::exit_status(boost::report_errors());
    }
    const auto [left_error] = co_await cobalt::as_tuple(left.connect(*right_endpoint));
    const auto [right_error] = co_await cobalt::as_tuple(right.connect(*left_endpoint));
    if (!BOOST_TEST(!left_error && !right_error)) {
        co_return webcpp::test::exit_status(boost::report_errors());
    }
    constexpr std::string_view message = "IPv6 UDP";
    const auto [write_error, sent] = co_await cobalt::as_tuple(left.send(io::buffer(message)));
    BOOST_TEST(!write_error);
    BOOST_TEST_EQ(sent, message.size());
    std::array<char, 32> bytes{};
    const auto [read_error, received] = co_await cobalt::as_tuple(right.receive(io::buffer(bytes)));
    BOOST_TEST(!read_error);
    BOOST_TEST(std::string_view(bytes.data(), received) == message);
    watchdog.cancel();
    co_return webcpp::test::exit_status(boost::report_errors());
}
