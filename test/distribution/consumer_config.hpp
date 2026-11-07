// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

#ifndef WEBCPP_COBALT_TEST_DISTRIBUTION_CONSUMER_CONFIG_HPP
#define WEBCPP_COBALT_TEST_DISTRIBUTION_CONSUMER_CONFIG_HPP

// Preinclude this application-owned header in EVERY source, including Cobalt's
// core, runner and optional SSL. Do not include a Cobalt header before it.
#ifdef BOOST_COBALT_CUSTOM_EXECUTOR
#include <webcpp/asio/io_context.hpp>

namespace boost::cobalt {
using executor = webcpp::asio_backend::io_context::executor_type;
}
#endif

#ifdef BOOST_COBALT_USE_CUSTOM_PMR
#include <atomic>
#include <cstddef>
#include <memory_resource>

namespace consumer_memory {
using namespace std::pmr;

inline std::atomic<std::size_t> allocations{0};

class counting_resource final : public memory_resource {
    void* do_allocate(std::size_t bytes, std::size_t alignment) override {
        void* result = new_delete_resource()->allocate(bytes, alignment);
        allocations.fetch_add(1, std::memory_order_relaxed);
        return result;
    }

    void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override {
        new_delete_resource()->deallocate(pointer, bytes, alignment);
    }

    bool do_is_equal(const memory_resource& other) const noexcept override {
        return this == &other;
    }
};

inline memory_resource* get_default_resource() noexcept {
    static counting_resource resource;
    return &resource;
}

class unsynchronized_pool_resource final : public std::pmr::unsynchronized_pool_resource {
public:
    unsynchronized_pool_resource()
        : std::pmr::unsynchronized_pool_resource(consumer_memory::get_default_resource()) {}

    explicit unsynchronized_pool_resource(memory_resource* upstream)
        : std::pmr::unsynchronized_pool_resource(upstream) {}
};
}  // namespace consumer_memory

namespace boost::cobalt {
namespace pmr = consumer_memory;
}
#endif

#endif
