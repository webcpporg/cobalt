// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

#ifndef WEBCPP_COBALT_BENCH_ALLOCATION_HPP
#define WEBCPP_COBALT_BENCH_ALLOCATION_HPP

#include <cstddef>
#include <memory_resource>

namespace benchmark {
struct allocation_counts {
    std::size_t calls = 0;
    std::size_t requested = 0;
    std::size_t live = 0;
    std::size_t peak = 0;
};

allocation_counts allocations() noexcept;
void reset_allocations() noexcept;
void verify_allocation_hooks();

class counting_resource final : public std::pmr::memory_resource {
    std::pmr::memory_resource* upstream_;
    allocation_counts counts_;

    void* do_allocate(std::size_t bytes, std::size_t alignment) override {
        auto* result = upstream_->allocate(bytes, alignment);
        ++counts_.calls;
        counts_.requested += bytes;
        counts_.live += bytes;
        if (counts_.live > counts_.peak) {
            counts_.peak = counts_.live;
        }
        return result;
    }

    void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override {
        upstream_->deallocate(pointer, bytes, alignment);
        counts_.live -= bytes;
    }

    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }

public:
    explicit counting_resource(std::pmr::memory_resource* upstream) : upstream_(upstream) {}

    allocation_counts counts() const noexcept { return counts_; }
};
}  // namespace benchmark

#endif
