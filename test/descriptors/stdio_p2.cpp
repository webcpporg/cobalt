// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
#include <webcpp/asio/wasi/stdio_stream.hpp>
#include <webcpp/cobalt.hpp>

#include "../output.hpp"

#include <boost/core/lightweight_test.hpp>
#include <webcpp/asio/post.hpp>
#include <webcpp/asio/write.hpp>

#include <array>
#include <cstdio>

namespace asio = webcpp::asio;
namespace cobalt = webcpp::cobalt;

namespace {

cobalt::promise<boost::system::error_code> read_once(webcpp::asio::wasi::stdio_stream& stream,
                                                     std::array<char, 64>& bytes) {
    const auto [error, size] =
        co_await cobalt::as_tuple(stream.async_read_some(asio::buffer(bytes), cobalt::use_op));
    BOOST_TEST_EQ(size, 0U);
    co_return error;
}

}  // namespace

cobalt::main co_main([[maybe_unused]] int argc, [[maybe_unused]] char** argv) {
    // The CLI runner owns an io_context. Its concrete executor drives the
    // existing P2 pollable stream; Cobalt supplies the completion token only.
    // NOLINTNEXTLINE(readability-static-accessed-through-instance)
    const auto executor = co_await cobalt::this_coro::executor;
    auto& context = static_cast<asio::io_context&>(asio::query(executor, asio::execution::context));
    webcpp::asio::wasi::stdio_stream stream(context.get_executor());
    std::array<char, 64> bytes{};
    auto pending = read_once(stream, bytes);
    asio::post(executor, [&] { pending.cancel(); });
    BOOST_TEST(co_await pending == asio::error::operation_aborted);
    webcpp::test::write_stderr("ready-for-input\n");
    webcpp::test::flush_stderr();
    const auto [error, count] =
        co_await cobalt::as_tuple(stream.async_read_some(asio::buffer(bytes), cobalt::use_op));
    BOOST_TEST(!error);
    BOOST_TEST(count > 0);
    const auto [write_error, written] = co_await cobalt::as_tuple(
        asio::async_write(stream, asio::buffer(bytes.data(), count), cobalt::use_op));
    BOOST_TEST(!write_error);
    BOOST_TEST_EQ(written, count);
    stream.close();
    co_return boost::report_errors();
}
