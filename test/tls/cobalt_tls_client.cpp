// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

#include <webcpp/asio/tls_client_context.hpp>

#include <webcpp/asio/ip/tcp.hpp>
#include <webcpp/asio/read.hpp>
#include <webcpp/asio/write.hpp>
#include <webcpp/cobalt/main.hpp>
#include <webcpp/cobalt/op.hpp>
#include <webcpp/cobalt/result.hpp>
#include <webcpp/cobalt/this_coro.hpp>

#include <array>
#include <charconv>
#include <cstdio>
#include <string_view>

namespace cobalt = webcpp::cobalt;
namespace asio = webcpp::asio;

cobalt::main co_main(int argc, char** argv) {
    if (argc != 6) {
        co_return 1;
    }
    unsigned short port = 0;
    const std::string_view port_text(argv[1]);
    const auto parsed =
        std::from_chars(port_text.data(), port_text.data() + port_text.size(), port);
    if (parsed.ec != std::errc{} || parsed.ptr != port_text.data() + port_text.size()) {
        co_return 1;
    }
    auto prepared = std::string_view(argv[3]) == "@default"
                        ? webcpp::asio::make_tls_client_context()
                        : webcpp::asio::make_tls_client_context(argv[3]);
    if (!prepared) {
        const auto message = prepared.error().message();
        static_cast<void>(std::fprintf(stderr, "CA preparation: %s\n", message.c_str()));
        co_return 1;
    }
    auto& context = *prepared;
    const int version = std::string_view(argv[5]) == "1.2" ? TLS1_2_VERSION : TLS1_3_VERSION;
    if (!SSL_CTX_set_min_proto_version(context.native_handle(), version) ||
        !SSL_CTX_set_max_proto_version(context.native_handle(), version)) {
        co_return 1;
    }
    // NOLINTNEXTLINE(readability-static-accessed-through-instance)
    const auto exec = co_await cobalt::this_coro::executor;
    asio::ssl::stream<asio::ip::tcp::socket> stream(exec, context);
    stream.set_verify_callback(asio::ssl::host_name_verification(argv[2]));
    if (SSL_set_tlsext_host_name(stream.native_handle(), argv[2]) != 1) {
        co_return 1;
    }
    const auto [connect_error] = co_await cobalt::as_tuple(stream.next_layer().async_connect(
        {asio::ip::address_v4::loopback(), port}, cobalt::use_op));
    if (connect_error) {
        co_return 1;
    }
    const auto [handshake_error] = co_await cobalt::as_tuple(
        stream.async_handshake(asio::ssl::stream_base::client, cobalt::use_op));
    const bool reject = std::string_view(argv[4]) == "reject";
    if (handshake_error) {
        if (reject && SSL_get_verify_result(stream.native_handle()) != X509_V_OK) {
            std::puts("PASS Cobalt certificate rejection");
            co_return 0;
        }
        co_return 1;
    }
    if (reject) {
        co_return 1;
    }
    constexpr std::string_view request = "ping";
    const auto [write_error, sent] =
        co_await cobalt::as_tuple(asio::async_write(stream, asio::buffer(request), cobalt::use_op));
    if (write_error || sent != request.size()) {
        co_return 1;
    }
    std::array<char, 4> response{};
    const auto [read_error, received] =
        co_await cobalt::as_tuple(asio::async_read(stream, asio::buffer(response), cobalt::use_op));
    if (read_error || received != response.size() ||
        std::string_view(response.data(), received) != "pong") {
        co_return 1;
    }
    const auto [shutdown_error] = co_await cobalt::as_tuple(stream.async_shutdown(cobalt::use_op));
    if (shutdown_error) {
        co_return 1;
    }
    std::puts("PASS Cobalt verified TLS, ping/pong and clean shutdown");
    co_return 0;
}
