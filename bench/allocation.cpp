// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

// Optional C++ allocation instrumentation. malloc, JS and runtime allocations
// are outside this boundary. Bookkeeping bytes are not requested payload bytes.
#include "allocation.hpp"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <new>

#ifdef WEBCPP_BENCH_TRACK_CPP_ALLOCATIONS
namespace {
constinit std::atomic<std::size_t> calls{0}, requested{0}, live{0}, peak{0};

struct header {
    void* base;
    std::size_t bytes;
};

void* allocate(std::size_t bytes, std::size_t alignment) noexcept {
    alignment = alignment < alignof(std::max_align_t) ? alignof(std::max_align_t) : alignment;
    if (bytes > std::numeric_limits<std::size_t>::max() - alignment - sizeof(header)) {
        return nullptr;
    }
    auto* raw = std::malloc(bytes + alignment + sizeof(header));
    if (raw == nullptr) {
        return nullptr;
    }
    const auto begin = reinterpret_cast<std::uintptr_t>(raw) + sizeof(header);
    const auto address = (begin + alignment - 1) & ~(alignment - 1);
    auto* entry = reinterpret_cast<header*>(address) - 1;
    *entry = {raw, bytes};
    calls.fetch_add(1, std::memory_order_relaxed);
    requested.fetch_add(bytes, std::memory_order_relaxed);
    const auto current = live.fetch_add(bytes, std::memory_order_relaxed) + bytes;
    auto maximum = peak.load(std::memory_order_relaxed);
    while (current > maximum &&
           !peak.compare_exchange_weak(maximum, current, std::memory_order_relaxed)) {
    }
    return reinterpret_cast<void*>(address);
}

void release(void* pointer) noexcept {
    if (pointer == nullptr) {
        return;
    }
    const auto* entry = static_cast<header*>(pointer) - 1;
    live.fetch_sub(entry->bytes, std::memory_order_relaxed);
    std::free(entry->base);
}

void* required(std::size_t bytes, std::size_t alignment) {
    auto* result = allocate(bytes, alignment);
    if (result == nullptr) {
#ifdef __cpp_exceptions
        throw std::bad_alloc();
#else
        std::abort();
#endif
    }
    return result;
}
}  // namespace

void* operator new(std::size_t bytes) {
    return required(bytes, alignof(std::max_align_t));
}

void* operator new[](std::size_t bytes) {
    return required(bytes, alignof(std::max_align_t));
}

void* operator new(std::size_t bytes, std::align_val_t alignment) {
    return required(bytes, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t bytes, std::align_val_t alignment) {
    return required(bytes, static_cast<std::size_t>(alignment));
}

void* operator new(std::size_t bytes, const std::nothrow_t&) noexcept {
    return allocate(bytes, alignof(std::max_align_t));
}

void* operator new[](std::size_t bytes, const std::nothrow_t&) noexcept {
    return allocate(bytes, alignof(std::max_align_t));
}

void* operator new(std::size_t bytes, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    return allocate(bytes, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t bytes, std::align_val_t alignment,
                     const std::nothrow_t&) noexcept {
    return allocate(bytes, static_cast<std::size_t>(alignment));
}

void operator delete(void* pointer) noexcept {
    release(pointer);
}

void operator delete[](void* pointer) noexcept {
    release(pointer);
}

void operator delete(void* pointer, std::size_t) noexcept {
    release(pointer);
}

void operator delete[](void* pointer, std::size_t) noexcept {
    release(pointer);
}

void operator delete(void* pointer, std::align_val_t) noexcept {
    release(pointer);
}

void operator delete[](void* pointer, std::align_val_t) noexcept {
    release(pointer);
}

void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept {
    release(pointer);
}

void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept {
    release(pointer);
}

void operator delete(void* pointer, const std::nothrow_t&) noexcept {
    release(pointer);
}

void operator delete[](void* pointer, const std::nothrow_t&) noexcept {
    release(pointer);
}

void operator delete(void* pointer, std::align_val_t, const std::nothrow_t&) noexcept {
    release(pointer);
}

void operator delete[](void* pointer, std::align_val_t, const std::nothrow_t&) noexcept {
    release(pointer);
}
#endif

namespace benchmark {
allocation_counts allocations() noexcept {
#ifdef WEBCPP_BENCH_TRACK_CPP_ALLOCATIONS
    return {calls.load(), requested.load(), live.load(), peak.load()};
#else
    return {};
#endif
}

void reset_allocations() noexcept {
#ifdef WEBCPP_BENCH_TRACK_CPP_ALLOCATIONS
    calls.store(0);
    requested.store(0);
    peak.store(live.load());
#endif
}

void verify_allocation_hooks() {
#ifdef WEBCPP_BENCH_TRACK_CPP_ALLOCATIONS
    const auto before = allocations();
    auto* ordinary = ::operator new(513);
    auto* aligned = ::operator new(256, std::align_val_t(64));
    const auto during = allocations();
    if (during.calls != before.calls + 2 || during.requested != before.requested + 769 ||
        during.live != before.live + 769 || reinterpret_cast<std::uintptr_t>(aligned) % 64 != 0) {
        std::abort();
    }
    ::operator delete(ordinary);
    ::operator delete(aligned, std::align_val_t(64));
    if (allocations().live != before.live) {
        std::abort();
    }
#endif
}
}  // namespace benchmark
