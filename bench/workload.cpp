// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

// Matched, finite workloads. This is not an unchanged upstream benchmark.
#include "allocation.hpp"

#include <webcpp/asio.hpp>
#include <webcpp/asio/experimental/channel.hpp>
#include <webcpp/asio/experimental/parallel_group.hpp>
#include <webcpp/beast/http.hpp>
#include <webcpp/beast/http/parser.hpp>

#ifdef WEBCPP_BENCH_COBALT
#include <webcpp/cobalt.hpp>
#endif

#include <array>
#include <charconv>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory_resource>
#include <string_view>

namespace asio = webcpp::asio;
namespace beast = webcpp::beast;
#ifdef WEBCPP_BENCH_COBALT
namespace cobalt = webcpp::cobalt;
using operation = cobalt::promise<void>;
constexpr auto token = cobalt::use_op;
constexpr auto current_executor = cobalt::this_coro::executor;
#else
using operation = asio::awaitable<void>;
constexpr auto token = asio::use_awaitable;
constexpr auto current_executor = asio::this_coro::executor;
#endif

namespace {
void require(bool condition) {
    if (!condition) {
        std::fputs("benchmark correctness failure\n", stderr);
        std::abort();
    }
}

std::size_t memory_bytes() {
#ifdef __wasm__
    return __builtin_wasm_memory_size(0) * 65536;
#else
    return 0;
#endif
}

operation workload(std::string_view kind, std::uint64_t count, std::uint64_t& checksum) {
    const auto executor = co_await current_executor;
    asio::experimental::channel<void(boost::system::error_code, std::uint64_t)> channel{
        executor, kind == "immediate" ? 1U : 0U};
    asio::steady_timer timer{executor};
#ifdef WEBCPP_BENCH_COBALT
    cobalt::channel<std::uint64_t> cobalt_channel{0};
#endif
    std::array<std::byte, 2048> storage{};
    std::pmr::monotonic_buffer_resource memory{storage.data(), storage.size()};
    for (std::uint64_t index = 1; index <= count; ++index) {
        if (kind == "post") {
            co_await asio::post(executor, token);
            checksum += index;
        } else if (kind == "immediate") {
            co_await channel.async_send(boost::system::error_code{}, index, token);
            checksum += co_await channel.async_receive(token);
        } else if (kind == "channel" || kind == "parallel") {
#ifdef WEBCPP_BENCH_COBALT
            if (kind == "channel") {
                const auto values =
                    co_await cobalt::join(cobalt_channel.write(index), cobalt_channel.read());
                checksum += std::get<1>(values);
            } else {
                const auto values = co_await cobalt::join(
                    channel.async_send(boost::system::error_code{}, index, token),
                    channel.async_receive(token));
                checksum += std::get<1>(values);
            }
#else
            const auto [order, send_error, receive_error, value] =
                co_await asio::experimental::make_parallel_group(
                    channel.async_send(boost::system::error_code{}, index, asio::deferred),
                    channel.async_receive(asio::deferred))
                    .async_wait(asio::experimental::wait_for_all(), token);
            static_cast<void>(order);
            require(!send_error && !receive_error);
            checksum += value;
#endif
        } else if (kind == "allocation") {
            memory.release();
            for (int step = 0; step < 5; ++step) {
                co_await asio::post(executor, token);
                const auto bytes = step == 0 ? 512U : 256U;
                auto* pointer = static_cast<volatile std::byte*>(memory.allocate(bytes));
                pointer[0] = std::byte(index & 255);
                require(pointer[0] == std::byte(index & 255));
            }
            checksum += index;
        } else if (kind == "timer") {
            timer.expires_after(std::chrono::milliseconds(0));
            co_await timer.async_wait(token);
            checksum += index;
        } else if (kind == "http") {
            constexpr std::string_view message =
                "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nX-Sequence: witness\r\n\r\nhello";
            beast::http::response_parser<beast::http::string_body> parser;
            parser.eager(true);
            boost::system::error_code error;
            const auto consumed = parser.put(asio::buffer(message.data(), message.size()), error);
            require(!error && consumed == message.size() && parser.is_done());
            require(parser.get().result_int() == 200 && parser.get().body() == "hello");
            checksum += index;
        } else {
            require(false);
        }
    }
}

std::uint64_t number(const char* text) {
    std::uint64_t result = 0;
    const auto* end = text + std::strlen(text);
    const auto parsed = std::from_chars(text, end, result);
    require(parsed.ec == std::errc{} && parsed.ptr == end && result > 0 && result <= 10000000);
    return result;
}

operation measure(int argc, char** argv) {
    require(argc == 4);
    const std::string_view kind = argv[1];
    const auto count = number(argv[2]);
    const auto warmup = number(argv[3]);
    benchmark::verify_allocation_hooks();
    std::uint64_t sum = 0;
    co_await workload(kind, warmup, sum);
    require(sum == warmup * (warmup + 1) / 2);
    sum = 0;
    const auto baseline_live = benchmark::allocations().live;
    const auto memory_before = memory_bytes();
    benchmark::reset_allocations();
#if defined(WEBCPP_BENCH_COBALT) && defined(WEBCPP_BENCH_TRACK_CPP_ALLOCATIONS)
    // Count coroutine-frame requests separately from the root pool's backing allocations.
    benchmark::counting_resource frame_resource{cobalt::this_thread::get_default_resource()};
    auto* previous_resource = cobalt::this_thread::set_default_resource(&frame_resource);
#endif
    const auto start = std::chrono::steady_clock::now();
    co_await workload(kind, count, sum);
    const auto elapsed =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    const auto allocations = benchmark::allocations();
    const auto memory_after = memory_bytes();
#if defined(WEBCPP_BENCH_COBALT) && defined(WEBCPP_BENCH_TRACK_CPP_ALLOCATIONS)
    cobalt::this_thread::set_default_resource(previous_resource);
    const auto frames = frame_resource.counts();
    require(frames.live == 0);
#else
    const benchmark::allocation_counts frames;
#endif
    require(sum == count * (count + 1) / 2);
    std::printf("RESULT {\"workload\":\"%.*s\",\"operations\":%" PRIu64 ",\"checksum\":%" PRIu64
                ",\"elapsed_ms\":%.9f,"
                "\"cpp_allocations\":%zu,\"cpp_requested_bytes\":%zu,"
                "\"cpp_live_before\":%zu,\"cpp_live_after\":%zu,\"cpp_peak_bytes\":%zu,"
                "\"linear_memory_before\":%zu,\"linear_memory_after\":%zu,"
                "\"cobalt_frame_requests\":%zu,\"cobalt_frame_requested_bytes\":%zu,"
                "\"cobalt_frame_peak_bytes\":%zu,\"cobalt_frame_live_after\":%zu}\n",
                static_cast<int>(kind.size()), kind.data(), count, sum, elapsed, allocations.calls,
                allocations.requested, baseline_live, allocations.live, allocations.peak,
                memory_before, memory_after, frames.calls, frames.requested, frames.peak,
                frames.live);
}
}  // namespace

#ifdef WEBCPP_BENCH_COBALT
cobalt::main co_main(int argc, char** argv) {
    auto pending = measure(argc, argv);
    co_await pending;
    co_return 0;
}
#else
int main(int argc, char** argv) {
    asio::io_context context;
    bool finished = false;
    asio::co_spawn(context, measure(argc, argv), [&](std::exception_ptr error) {
        require(!error);
        finished = true;
    });
    context.run();  // lint-run: complete the measured coroutine and its scheduled operations
    require(finished);
}
#endif
