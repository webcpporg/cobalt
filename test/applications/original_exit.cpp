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
[[maybe_unused]] const int registered =
    std::atexit([] { std::fputs("webcpp: upstream application reached normal exit\n", stderr); });
}  // namespace
