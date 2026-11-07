// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

#include <webcpp/cobalt.hpp>

#include "../output.hpp"

#include <boost/core/lightweight_test.hpp>
#include <webcpp/asio/post.hpp>
#include <webcpp/cobalt/io/random_access_file.hpp>
#include <webcpp/cobalt/io/read.hpp>
#include <webcpp/cobalt/io/stream_file.hpp>
#include <webcpp/cobalt/io/write.hpp>

#ifndef _WIN32
#include <unistd.h>
#endif
#include <array>
#include <cstdio>
#include <limits>
#include <string_view>

namespace cobalt = webcpp::cobalt;
namespace io = cobalt::io;

namespace {

#ifndef _WIN32
cobalt::promise<void> cancel_stdin() {
    // The driver keeps stdin empty until the cancel callback explicitly
    // announces completion, then sends one byte to bound the negative control.
    io::stream_file input(STDIN_FILENO);
    const auto executor = input.get_executor();
    bool requested = false;
    webcpp::asio::post(executor, [&] {
        BOOST_TEST(input.cancel().has_value());
        requested = true;
        webcpp::test::write_stderr("cancel-issued\n");
        webcpp::test::flush_stderr();
    });
    char byte = 0;
    const auto [error, count] = co_await cobalt::as_tuple(input.read_some(io::buffer(&byte, 1)));
    while (!requested) {
        co_await webcpp::asio::post(executor, cobalt::use_op);
    }
#if defined(__wasip2__) || defined(__wasip3__)
    // SDK 34's stdio descriptors expose no set_blocking/fcntl_setfl callback.
    // This is a negative capability probe, not successful async stdin support.
    BOOST_TEST(error == webcpp::asio::error::invalid_argument);
    std::puts("SDK-BLOCKED: raw POSIX stdin has no nonblocking mode in wasi-libc");
#else
    BOOST_TEST(error == webcpp::asio::error::operation_aborted);
#endif
    BOOST_TEST_EQ(count, 0U);
    BOOST_TEST(input.close().has_value());
}

#endif

std::uint64_t cursor(io::random_access_file& random, std::int64_t offset,
                     io::file::seek_basis basis) {
    auto handle = random.release();
    if (!BOOST_TEST(handle.has_value())) {
        return 0;
    }
    io::stream_file view(*handle);
    auto position = view.seek(offset, basis);
    BOOST_TEST(position.has_value());
    auto restored = view.release();
    if (BOOST_TEST(restored.has_value())) {
        BOOST_TEST(random.assign(*restored).has_value());
    }
    return position ? *position : 0;
}

cobalt::task<void> check_stream(io::stream_file& file, std::array<char, 3>& bytes) {
    constexpr std::string_view message = "0123456789";
    const auto [write_error, written] =
        co_await cobalt::as_tuple(io::write(file, io::buffer(message)));
    BOOST_TEST(!write_error);
    BOOST_TEST_EQ(written, message.size());
    auto size = file.size();
    BOOST_TEST(size.has_value());
    if (size) {
        BOOST_TEST_EQ(*size, message.size());
    }
    auto position = file.seek(3, io::file::seek_set);
    BOOST_TEST(position.has_value());
    if (position) {
        BOOST_TEST_EQ(*position, 3U);
    }
    const auto [read_error, count] = co_await cobalt::as_tuple(io::read(file, io::buffer(bytes)));
    BOOST_TEST(!read_error);
    BOOST_TEST_EQ(count, bytes.size());
    BOOST_TEST(std::string_view(bytes.data(), bytes.size()) == "345");
    BOOST_TEST(file.close().has_value());
    BOOST_TEST(!file.seek(0, io::file::seek_set).has_value());
}

}  // namespace

cobalt::main co_main(int argc, char** argv) {
#ifndef _WIN32
    if (argc == 2 && std::string_view(argv[1]) == "--cancel-stdin") {
        co_await cancel_stdin();
        co_return boost::report_errors();
    }
#endif
    if (argc != 2) {
        co_return 2;
    }
    io::stream_file file;
    const auto opened =
        file.open(argv[1], io::file::read_write | io::file::create | io::file::truncate);
    if (!BOOST_TEST(opened.has_value())) {
        const auto message = opened.error().message();
        BOOST_TEST(std::fprintf(stderr, "open: %s\n", message.c_str()) >= 0);
        co_return boost::report_errors();
    }
    std::array<char, 3> bytes{};
    co_await check_stream(file, bytes);

    io::random_access_file random;
    if (!BOOST_TEST(random.open(argv[1], io::file::read_write).has_value())) {
        co_return boost::report_errors();
    }
    BOOST_TEST_EQ(cursor(random, 2, io::file::seek_set), 2);
    const auto [offset_error, offset_count] =
        co_await cobalt::as_tuple(random.read_some_at(4, io::buffer(bytes)));
    BOOST_TEST(!offset_error);
    BOOST_TEST_EQ(offset_count, bytes.size());
    BOOST_TEST(std::string_view(bytes.data(), bytes.size()) == "456");
    constexpr std::string_view replacement = "XYZ";
    const auto [offset_write_error, offset_written] =
        co_await cobalt::as_tuple(random.write_some_at(5, io::buffer(replacement)));
    BOOST_TEST(!offset_write_error);
    BOOST_TEST_EQ(offset_written, replacement.size());
    BOOST_TEST_EQ(cursor(random, 0, io::file::seek_cur), 2);
    std::array<char, 2> first{};
    std::array<char, 2> second{};
    const std::array<webcpp::asio::mutable_buffer, 3> scattered{
        io::buffer(first.data(), 0),
        io::buffer(first),
        io::buffer(second),
    };
    const auto [scatter_error, scattered_size] =
        co_await cobalt::as_tuple(io::read_at(random, 1, scattered));
    BOOST_TEST(!scatter_error);
    BOOST_TEST_EQ(scattered_size, 4U);
    BOOST_TEST(std::string_view(first.data(), first.size()) == "12");
    BOOST_TEST(std::string_view(second.data(), second.size()) == "34");
    constexpr std::string_view ab = "AB";
    constexpr std::string_view cd = "CD";
    const std::array<webcpp::asio::const_buffer, 3> gathered{
        io::buffer(ab.data(), 0),
        io::buffer(ab),
        io::buffer(cd),
    };
    const auto [gather_error, gathered_size] =
        co_await cobalt::as_tuple(io::write_at(random, 0, gathered));
    BOOST_TEST(!gather_error);
    BOOST_TEST_EQ(gathered_size, 4U);
    BOOST_TEST_EQ(cursor(random, 0, io::file::seek_cur), 2U);
    const auto [eof_error, eof_count] =
        co_await cobalt::as_tuple(random.read_some_at(100, io::buffer(bytes)));
    BOOST_TEST(eof_error == webcpp::asio::error::eof);
    BOOST_TEST_EQ(eof_count, 0U);
    const auto [empty_error, empty_count] =
        co_await cobalt::as_tuple(random.read_some_at(100, io::buffer(bytes.data(), 0)));
    BOOST_TEST(!empty_error);
    BOOST_TEST_EQ(empty_count, 0U);
    const auto [range_error, range_count] = co_await cobalt::as_tuple(
        random.read_some_at(std::numeric_limits<std::uint64_t>::max(), io::buffer(bytes)));
    BOOST_TEST(range_error == webcpp::asio::error::invalid_argument);
    BOOST_TEST_EQ(range_count, 0U);
    BOOST_TEST(random.close().has_value());
    std::puts("files: seek, offset reads/writes and closed descriptor checked");
    co_return boost::report_errors();
}
