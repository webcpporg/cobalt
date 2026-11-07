// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

#ifndef WEBCPP_COBALT_TEST_OUTPUT_HPP
#define WEBCPP_COBALT_TEST_OUTPUT_HPP

#include <cstdio>
#include <cstdlib>

namespace webcpp::test {

// Drivers use these progress markers to release blocked input or diagnose a
// timeout. A failed write must fail the process instead of hiding the marker.
inline void write_stderr(const char* message) {
    if (std::fputs(message, stderr) < 0) {
        std::_Exit(98);
    }
}

inline void flush_stderr() {
    if (std::fflush(stderr) != 0) {
        std::_Exit(98);
    }
}

inline void flush_stdout() {
    if (std::fflush(stdout) != 0) {
        std::_Exit(98);
    }
}

}  // namespace webcpp::test

#endif
