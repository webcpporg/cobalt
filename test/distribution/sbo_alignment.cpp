// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
// See accompanying file LICENSE_1_0.txt or https://www.boost.org/LICENSE_1_0.txt.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <webcpp/cobalt/detail/backend/detail/sbo_resource.hpp>

int main() {
    alignas(256) std::array<std::byte, 512> storage{};
    webcpp::cobalt::detail::sbo_resource resource(storage.data() + 1, storage.size() - 1);
    void* pointer = resource.allocate(8, 256);
    // Do not let memory_resource::allocate's alignment annotation turn the
    // observation into an optimizer assumption about a faulty implementation.
    const volatile std::uintptr_t observed = reinterpret_cast<std::uintptr_t>(pointer);
    const bool aligned = observed % 256 == 0;
    resource.deallocate(pointer, 8, 256);
    std::puts(aligned ? "Cobalt SBO over-alignment: passed" : "Cobalt SBO over-alignment: failed");
    return aligned ? 0 : 1;
}
