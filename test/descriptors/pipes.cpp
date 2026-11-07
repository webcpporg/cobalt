// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
#include <webcpp/cobalt.hpp>
#include <webcpp/test/exit_status.hpp>

#include "../output.hpp"

#include <boost/core/lightweight_test.hpp>
#include <webcpp/asio/post.hpp>
#include <webcpp/asio/steady_timer.hpp>
#include <webcpp/cobalt/io/pipe.hpp>
#include <webcpp/cobalt/io/read.hpp>
#include <webcpp/cobalt/io/write.hpp>

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace cobalt = webcpp::cobalt;
namespace io = cobalt::io;

cobalt::main co_main([[maybe_unused]] int argc, [[maybe_unused]] char** argv) {
    // NOLINTNEXTLINE(readability-static-accessed-through-instance)
    const auto executor = co_await cobalt::this_coro::executor;
    webcpp::asio::steady_timer deadline(executor, std::chrono::seconds(5));
    deadline.async_wait([](boost::system::error_code error) {
        if (!error) {
            webcpp::test::write_stderr("pipes: deadline exceeded\n");
            std::_Exit(99);
        }
    });
    webcpp::test::write_stderr("pipes: creating\n");
    auto pipes = io::pipe();
#ifdef __wasip2__
    BOOST_TEST(!pipes.has_value());
    if (!pipes) {
        BOOST_TEST(pipes.error() == webcpp::asio::error::operation_not_supported);
    }
    std::puts(
        "SDK-BLOCKED: wasi-libc pipe creation is unavailable on P2; error returned without "
        "throwing");
    deadline.cancel();
    co_return webcpp::test::exit_status(boost::report_errors());
#else
    if (!BOOST_TEST(pipes.has_value())) {
        const auto message = pipes.error().message();
        BOOST_TEST(std::fprintf(stderr, "pipe: %s\n", message.c_str()) >= 0);
        deadline.cancel();
        co_return webcpp::test::exit_status(boost::report_errors());
    }
    auto& [reader, writer] = *pipes;
    constexpr std::string_view message = "cobalt pipe";
    const auto [write_error, written] =
        co_await cobalt::as_tuple(io::write(writer, io::buffer(message)));
    webcpp::test::write_stderr("pipes: write complete\n");
    BOOST_TEST(!write_error);
    BOOST_TEST_EQ(written, message.size());
    std::array<char, message.size()> bytes{};
    const auto [read_error, read] = co_await cobalt::as_tuple(io::read(reader, io::buffer(bytes)));
    BOOST_TEST(!read_error);
    BOOST_TEST_EQ(read, message.size());
    BOOST_TEST(std::string_view(bytes.data(), read) == message);
    webcpp::test::write_stderr("pipes: read complete\n");
    webcpp::asio::post(executor, [&] { BOOST_TEST(reader.cancel().has_value()); });
    const auto [cancel_error, cancel_count] =
        co_await cobalt::as_tuple(reader.read_some(io::buffer(bytes)));
    BOOST_TEST(cancel_error == webcpp::asio::error::operation_aborted);
    BOOST_TEST_EQ(cancel_count, 0U);
    webcpp::test::write_stderr("pipes: cancel complete\n");
    BOOST_TEST(writer.close().has_value());
#ifdef __EMSCRIPTEN__
    BOOST_TEST_EQ(WEBCPP_COBALT_HAS_PIPE_CREATION, 0);
    BOOST_TEST(reader.close().has_value());
    std::puts("pipes: byte transfer and pending cancellation checked");
    std::puts("SDK-BLOCKED: Emscripten pipe EOF is unsupported; see pipe_eof_sdk_limitation");
#else
    const auto [eof_error, eof_count] =
        co_await cobalt::as_tuple(reader.read_some(io::buffer(bytes)));
    const auto eof_message = eof_error.message();
    BOOST_TEST(std::fprintf(stderr,
                            "pipes: writer close category=%s value=%d message=%s bytes=%zu\n",
                            eof_error.category().name(), eof_error.value(), eof_message.c_str(),
                            eof_count) >= 0);
#if defined(WEBCPP_TARGET_NATIVE) && defined(_WIN32)
    // Boost.Asio retains Win32 ERROR_BROKEN_PIPE; see chriskohlhoff/asio#1442.
    BOOST_TEST(eof_error == webcpp::asio::error::broken_pipe);
#else
    BOOST_TEST(eof_error == webcpp::asio::error::eof);
#endif
    BOOST_TEST_EQ(eof_count, 0U);
    BOOST_TEST(reader.close().has_value());
    std::puts("pipes: guest-created pair, bytes, pending cancellation and writer closure checked");
#endif
#endif
    deadline.cancel();
    co_return webcpp::test::exit_status(boost::report_errors());
}
