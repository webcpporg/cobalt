// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

// A separate observer leaves the imported source unchanged. A process that
// quietly exits before its suspended main finishes cannot satisfy this marker.
#include <cstdio>
#include <cstdlib>

namespace {
struct normal_exit_observer {
    ~normal_exit_observer() {
        if (std::fputs("webcpp: upstream application reached normal exit\n", stderr) < 0 ||
            std::fflush(stderr) != 0) {
            std::_Exit(98);
        }
    }
};

// Constant initialization cannot throw. The runtime invokes the destructor on
// normal exit, leaving imported applications and their entry points unchanged.
const normal_exit_observer registered{};
}  // namespace
