// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

#include <webcpp/asio/tls_client_context.hpp>

#include <boost/cobalt/io/read.hpp>
#include <boost/cobalt/io/ssl.hpp>
#include <boost/cobalt/io/write.hpp>
#include <boost/cobalt/main.hpp>
#include <boost/cobalt/op.hpp>
#include <boost/cobalt/result.hpp>
#include <boost/cobalt/task.hpp>
#include <boost/cobalt/this_coro.hpp>

#include <array>
#include <charconv>
#include <cstdio>
#include <string_view>

namespace cobalt = boost::cobalt;
namespace asio = boost::asio;

namespace {

cobalt::task<boost::system::error_code> perform_handshake(cobalt::io::ssl::stream& stream,
                                                          bool buffered) {
    const auto [before_shutdown] = co_await cobalt::as_tuple(stream.shutdown());
    if (before_shutdown != asio::error::no_protocol_option) {
        {
            co_return asio::error::invalid_argument;
        }
    }
    if (buffered) {
        const auto [error, consumed] = co_await cobalt::as_tuple(
            stream.handshake(asio::ssl::stream_base::client, cobalt::io::const_buffer{}));
        if (consumed != 0) {
            {
                co_return asio::error::invalid_argument;
            }
        }
        co_return error;
    }
    const auto [error] =
        co_await cobalt::as_tuple(stream.handshake(asio::ssl::stream_base::client));
    co_return error;
}

cobalt::task<bool> rejected_io(cobalt::io::ssl::stream& stream) {
    if (stream.secure()) {
        co_return false;
    }
    const auto [write_error, sent] = co_await cobalt::as_tuple(
        stream.write_some(cobalt::io::buffer(std::string_view("must not escape"))));
    if (write_error != asio::error::not_connected || sent != 0) {
        co_return false;
    }
    std::array<char, 1> bytes{};
    const auto [read_error, received] =
        co_await cobalt::as_tuple(stream.read_some(cobalt::io::buffer(bytes)));
    if (read_error != asio::error::not_connected || received != 0) {
        co_return false;
    }
    std::puts("PASS Cobalt SSL certificate rejection and blocked application I/O");
    co_return true;
}

cobalt::task<bool> exchange(cobalt::io::ssl::stream& stream) {
    const auto [repeat_error] =
        co_await cobalt::as_tuple(stream.handshake(asio::ssl::stream_base::client));
    if (repeat_error != asio::error::already_started) {
        co_return false;
    }
    constexpr std::string_view request = "ping";
    const auto [write_error, sent] =
        co_await cobalt::as_tuple(cobalt::io::write(stream, cobalt::io::buffer(request)));
    if (write_error || sent != request.size()) {
        {
            co_return false;
        }
    }
    std::array<char, 4> response{};
    const auto [read_error, received] =
        co_await cobalt::as_tuple(cobalt::io::read(stream, cobalt::io::buffer(response)));
    if (read_error || received != response.size() ||
        std::string_view(response.data(), received) != "pong") {
        co_return false;
    }
    const auto [shutdown_error] = co_await cobalt::as_tuple(stream.shutdown());
    if (shutdown_error || stream.secure()) {
        {
            co_return false;
        }
    }
    std::puts("PASS Cobalt SSL verified TLS, encrypted ping/pong and clean shutdown");
    co_return true;
}

}  // namespace

cobalt::main co_main(int argc, char** argv) {
    if (argc != 6 && argc != 7) {
        {
            co_return 1;
        }
    }
    unsigned short port = 0;
    const std::string_view port_text(argv[1]);
    const auto parsed =
        std::from_chars(port_text.data(), port_text.data() + port_text.size(), port);
    if (parsed.ec != std::errc{} || parsed.ptr != port_text.data() + port_text.size()) {
        {
            co_return 1;
        }
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
    cobalt::io::ssl::stream stream(context, exec);
    if (stream.secure()) {
        co_return 1;
    }
    if (!stream.set_verify_callback(asio::ssl::host_name_verification(argv[2]))) {
        co_return 1;
    }
    if (SSL_set_tlsext_host_name(stream.native_ssl_handle(), argv[2]) != 1) {
        {
            co_return 1;
        }
    }
    const auto [connect_error] =
        co_await cobalt::as_tuple(stream.connect({cobalt::io::tcp_v4, "127.0.0.1", port}));
    if (connect_error) {
        {
            co_return 1;
        }
    }
    const auto error =
        co_await perform_handshake(stream, argc == 7 && std::string_view(argv[6]) == "buffered");
    const bool reject = std::string_view(argv[4]) == "reject";
    if (error) {
        if (reject && SSL_get_verify_result(stream.native_ssl_handle()) != X509_V_OK) {
            {
                co_return (co_await rejected_io(stream)) ? 0 : 1;
            }
        }
        co_return 1;
    }
    if (reject || !stream.secure()) {
        {
            co_return 1;
        }
    }
    co_return (co_await exchange(stream)) ? 0 : 1;
}
