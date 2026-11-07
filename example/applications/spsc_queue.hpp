// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

//
// Copyright (c) 2023 Klemens Morgenstern (klemens.morgenstern@gmx.net)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef WEBCPP_COBALT_EXAMPLE_SPSC_QUEUE_HPP
#define WEBCPP_COBALT_EXAMPLE_SPSC_QUEUE_HPP

#include <webcpp/cobalt.hpp>

#include <boost/lockfree/spsc_queue.hpp>
#include <webcpp/asio/post.hpp>

namespace cobalt = webcpp::cobalt;

/// This is a simple class making a lockfree::spsc_queue awaitable. It's not movable, since the
/// spsc_queue isn't.
template <typename T, typename... Options>
struct awaitable_spsc_queue {
    boost::lockfree::spsc_queue<T, Options...> queue;

    template <typename... Args>
    explicit awaitable_spsc_queue(Args&&... args) : queue(std::forward<Args>(args)...) {}

    // if the queue gets destroyed, destroying the awaiters is all we can do.
    ~awaitable_spsc_queue() {
        if (auto* r = reader.load(); r != nullptr) {
            std::coroutine_handle<void>::from_address(r).destroy();
        }

        if (auto* w = writer.load(); w != nullptr) {
            std::coroutine_handle<void>::from_address(w).destroy();
        }
    }

    // to avoid locks, put the coroutine handles into atomics.
    std::atomic<void*> reader{nullptr}, writer{nullptr};

    // capture the read & write executor
    cobalt::executor read_executor, write_executor;

    // the awaitable to read a value from the queue
    struct read_op {
        awaitable_spsc_queue* owner;

        // if reads are available we don't need to suspend
        bool await_ready() { return owner->queue.read_available(); }

        // The suspend implementation. We expect the coroutine promise to have an associated
        // executor. If it doesn't resumption will end up on the system_executor
        template <typename Promise>
        void await_suspend(std::coroutine_handle<Promise> h) {
            // Capture the read_executor.
            if constexpr (requires { h.promise().get_executor(); }) {
                owner->read_executor = h.promise().get_executor();
            } else {
                owner->read_executor = cobalt::this_thread::get_executor();
            }

            // Make sure there's only one coroutine awaiting the read
            assert(owner->reader == nullptr);
            // Publish the waiter, then recheck readiness. The exchange also
            // acquires a producer notification that arrived before publication.
            static_cast<void>(owner->reader.exchange(h.address()));
            if (owner->queue.read_available()) {
                auto ready =
                    cobalt::unique_handle<void>::from_address(owner->reader.exchange(nullptr));
                if (ready) {
                    const auto executor = owner->read_executor;
                    webcpp::asio::post(executor, std::move(ready));
                }
            }
        }

        T await_resume() {
            T res;
            // Grab the value from the queue
            owner->queue.pop(res);
            // if a writer is waiting post it to complete on it's thread
            auto w = cobalt::unique_handle<void>::from_address(owner->writer.exchange(nullptr));
            if (w) {
                const auto executor = owner->write_executor;
                webcpp::asio::post(executor, std::move(w));
            }

            return res;
        }
    };

    // The function to be used with for reading.
    read_op read() { return {this}; }

    // the awaitable to write a value from the queue
    struct write_op {
        awaitable_spsc_queue* owner;
        // The value to write
        T value;

        // If there is room in the queue we don't need to suspend
        bool await_ready() { return owner->queue.write_available(); }

        // The suspend implementation. Similar to the writer one.
        template <typename Promise>
        void await_suspend(std::coroutine_handle<Promise> h) {
            // Capture the read_executor.
            if constexpr (requires { h.promise().get_executor(); }) {
                owner->write_executor = h.promise().get_executor();
            } else {
                owner->write_executor = cobalt::this_thread::get_executor();
            }

            assert(owner->writer == nullptr);
            static_cast<void>(owner->writer.exchange(h.address()));
            if (owner->queue.write_available()) {
                auto ready =
                    cobalt::unique_handle<void>::from_address(owner->writer.exchange(nullptr));
                if (ready) {
                    const auto executor = owner->write_executor;
                    webcpp::asio::post(executor, std::move(ready));
                }
            }
        }

        // Resume the coroutine, this is where we do the actual write.
        // The reason is that we suspend when we cannot write because the queue is full.
        void await_resume() {
            owner->queue.push(std::move(value));
            // if a writer is waiting post it
            auto r = cobalt::unique_handle<void>::from_address(owner->reader.exchange(nullptr));
            if (r) {
                const auto executor = owner->read_executor;
                webcpp::asio::post(executor, std::move(r));
            }
        }
    };

    // The function to be used for writing.
    write_op write(T value) { return {this, std::move(value)}; }
};

// Adapted from Boost.Cobalt 1.92 spsc.cpp; keep the original in the test corpus.

#endif
