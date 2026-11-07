// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

#include <type_traits>
#include <webcpp/asio/io_context.hpp>
#include <webcpp/cobalt.hpp>

#ifdef BOOST_COBALT_USE_IO_CONTEXT
static_assert(
    std::is_same_v<webcpp::cobalt::executor, webcpp::asio_backend::io_context::executor_type>);
#endif

#if defined(BOOST_COBALT_NO_PMR) && !defined(WEBCPP_TARGET_NATIVE)
#ifndef WEBCPP_COBALT_BACKEND_NO_PMR
#error "The shipped implementation ignored the consumer's no-PMR policy"
#endif
#endif

// This separate translation unit checks the configuration used by a compiled
// consumer. The main translation unit and library sources receive the same flags.
bool selected_configuration_is_consistent(const void* expected_resource) {
#ifdef BOOST_COBALT_CUSTOM_EXECUTOR
    static_assert(
        std::is_same_v<webcpp::cobalt::executor, webcpp::asio_backend::io_context::executor_type>);
    // This crosses into the compiled core and must return the selected executor.
    auto executor = webcpp::cobalt::this_thread::get_executor();
    (void)executor;
#endif
#ifdef BOOST_COBALT_USE_CUSTOM_PMR
    auto allocator = webcpp::cobalt::this_thread::get_allocator();
    const auto before = consumer_memory::allocations.load(std::memory_order_relaxed);
    void* storage = allocator.allocate_bytes(32768);
    allocator.deallocate_bytes(storage, 32768);
    return allocator.resource() == expected_resource &&
           consumer_memory::allocations.load(std::memory_order_relaxed) > before;
#else
    (void)expected_resource;
    return sizeof(webcpp::cobalt::executor) > 0;
#endif
}
