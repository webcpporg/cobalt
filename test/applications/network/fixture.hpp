// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

#ifndef WEBCPP_COBALT_NETWORK_FIXTURE_HPP
#define WEBCPP_COBALT_NETWORK_FIXTURE_HPP

#include <charconv>
#include <cstdlib>
#include <cstring>

namespace webcpp_fixture {
inline const char* required(const char* name) {
    const auto* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        std::abort();
    }
    return value;
}

inline const char* remote_port() {
    return required("WEBCPP_COBALT_REMOTE_PORT");
}

inline const char* ca_file() {
    return required("WEBCPP_COBALT_CA");
}

inline unsigned short listener_port() {
    const auto* text = required("WEBCPP_COBALT_LISTENER_PORT");
    unsigned short port = 0;
    const auto parsed = std::from_chars(text, text + std::strlen(text), port);
    if (parsed.ec != std::errc{} || *parsed.ptr != '\0' || port == 0) {
        std::abort();
    }
    return port;
}
}  // namespace webcpp_fixture

#endif
