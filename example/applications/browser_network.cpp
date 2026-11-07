// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

// Browser transport counterpart for the upstream HTTP and ticker clients.
// The browser owns TLS and WebSocket framing. No TCP listener is emulated.
#include <emscripten/val.h>
#include <boost/json.hpp>
#include <webcpp/asio/ssl.hpp>
#include <webcpp/beast/core.hpp>
#include <webcpp/beast/http.hpp>
#include <webcpp/beast/http/read.hpp>
#include <webcpp/beast/http/write.hpp>
#include <webcpp/beast/ssl.hpp>
#include <webcpp/beast/tcp_stream.hpp>
#include <webcpp/beast/websocket.hpp>
#include <webcpp/cobalt.hpp>

#include <cstdio>
#include <string>

namespace asio = webcpp::asio;
namespace beast = webcpp::beast;
namespace cobalt = webcpp::cobalt;

cobalt::main co_main([[maybe_unused]] int argc, [[maybe_unused]] char** argv) {
    // NOLINTNEXTLINE(readability-static-accessed-through-instance)
    const auto executor = co_await cobalt::this_coro::executor;
    const auto port = emscripten::val::global("beastSecurePort").as<std::string>();
    asio::ssl::context context{asio::ssl::context::tls_client};
    boost::system::error_code context_error;
    context.set_verify_mode(asio::ssl::verify_peer, context_error);
    if (context_error) {
        co_return 1;
    }
    asio::ip::tcp::resolver resolver{executor};
    auto [resolve_error, endpoints] =
        co_await cobalt::as_tuple(resolver.async_resolve("localhost", port, cobalt::use_op));
    if (resolve_error) {
        co_return 1;
    }
    using transport = beast::ssl_stream<beast::tcp_stream>;
#ifdef WEBCPP_COBALT_TICKER_COUNTERPART
    beast::websocket::stream<transport> websocket{executor, context};
    auto& stream = websocket.next_layer();
#else
    transport stream{executor, context};
#endif
    if (!beast::set_server_name(stream, "localhost")) {
        co_return 2;
    }
    stream.set_verify_callback(asio::ssl::host_name_verification("localhost"));
    auto [connect_error, endpoint] = co_await cobalt::as_tuple(
        beast::get_lowest_layer(stream).async_connect(endpoints, cobalt::use_op));
    static_cast<void>(endpoint);
    if (connect_error) {
        co_return 3;
    }
    auto [tls_error] = co_await cobalt::as_tuple(
        stream.async_handshake(asio::ssl::stream_base::client, cobalt::use_op));
    if (tls_error) {
        co_return 4;
    }
#ifdef WEBCPP_COBALT_TICKER_COUNTERPART
    auto [upgrade_error] = co_await cobalt::as_tuple(
        websocket.async_handshake("localhost:" + port, "/", cobalt::use_op));
    if (upgrade_error) {
        co_return 5;
    }
    cobalt::channel<boost::json::object> subscriptions{1};
    for (int price = 100; price < 103; ++price) {
        const auto outgoing = boost::json::serialize(boost::json::object{
            {"event", "updated"},
            {"symbol", "BTC-USD"},
            {"last_trade_price", price},
        });
        auto [write_error, written] = co_await cobalt::as_tuple(
            websocket.async_write(asio::buffer(outgoing), cobalt::use_op));
        if (write_error || written != outgoing.size()) {
            co_return 6;
        }
        beast::flat_buffer buffer;
        auto [read_error, count] =
            co_await cobalt::as_tuple(websocket.async_read(buffer, cobalt::use_op));
        if (read_error || count != outgoing.size()) {
            co_return 7;
        }
        boost::system::error_code parse_error;
        const auto data = buffer.cdata();
        auto value =
            boost::json::parse({static_cast<const char*>(data.data()), data.size()}, parse_error);
        if (parse_error || !value.is_object()) {
            co_return 8;
        }
        co_await subscriptions.write(value.as_object());
        const auto update = co_await subscriptions.read();
        if (update.at("symbol") != "BTC-USD" || update.at("last_trade_price") != price) {
            co_return 9;
        }
    }
    auto [close_error] = co_await cobalt::as_tuple(
        websocket.async_close(beast::websocket::close_code::normal, cobalt::use_op));
    if (close_error) {
        co_return 10;
    }
    std::puts("application cobalt-ticker-ssl: passed");
#else
    beast::http::request<beast::http::empty_body> request{beast::http::verb::get, "/http", 11};
    request.set(beast::http::field::host, "localhost");
    auto [write_error, written] =
        co_await cobalt::as_tuple(beast::http::async_write(stream, request, cobalt::use_op));
    static_cast<void>(written);
    if (write_error) {
        co_return 5;
    }
    beast::flat_buffer buffer;
    beast::http::response<beast::http::string_body> response;
    auto [read_error, count] = co_await cobalt::as_tuple(
        beast::http::async_read(stream, buffer, response, cobalt::use_op));
    static_cast<void>(count);
    if (read_error || response.result_int() != 200 || response.body() != "fetch body") {
        co_return 6;
    }
    std::puts("application cobalt-http-ssl: passed");
#endif
    co_return 0;
}
