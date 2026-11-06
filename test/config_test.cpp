// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

// <webcpp/cobalt/config.hpp> defines WEBCPP_COBALT_NO_EXCEPTIONS exactly when the
// collection has no exceptions, and keeps the developer's own definition in a build
// that has them. test/Jamfile builds this file as the build asks, with all webcpp exceptions
// disabled, and with the library macro defined by the developer.

#include <webcpp/cobalt/config.hpp>

// Boost.Config, which config.hpp includes, has said whether the build has
// exceptions: without it, BOOST_NO_EXCEPTIONS would never be defined.
#if defined(__GNUC__) && defined(__cpp_exceptions) == defined(BOOST_NO_EXCEPTIONS)
#error "config.hpp does not include <boost/config.hpp>, which defines BOOST_NO_EXCEPTIONS"
#endif

#ifdef WEBCPP_TEST_COBALT_DEVELOPER_DEFINED
#ifndef WEBCPP_COBALT_NO_EXCEPTIONS
#error "config.hpp drops WEBCPP_COBALT_NO_EXCEPTIONS, which the developer defined"
#endif
#elif defined(WEBCPP_HAS_EXCEPTIONS) == defined(WEBCPP_COBALT_NO_EXCEPTIONS)
#error "WEBCPP_COBALT_NO_EXCEPTIONS must follow the collection exception configuration"
#endif

#if !defined(WEBCPP_TARGET_NATIVE) && !defined(WEBCPP_TARGET_EMSCRIPTEN) && \
    !defined(WEBCPP_TARGET_WASIP2) && !defined(WEBCPP_TARGET_WASIP3)
#error "Cobalt must expose the collection target configuration"
#endif

#ifdef WEBCPP_TARGET_EMSCRIPTEN
static_assert(WEBCPP_COBALT_HAS_BROWSER_BRIDGE == 1);
static_assert(WEBCPP_COBALT_HAS_SOCKET_BACKEND == 0);
static_assert(WEBCPP_COBALT_HAS_OPENSSL_BACKEND == 0);
#else
static_assert(WEBCPP_COBALT_HAS_BROWSER_BRIDGE == 0);
static_assert(WEBCPP_COBALT_HAS_SOCKET_BACKEND == 1);
static_assert(WEBCPP_COBALT_HAS_OPENSSL_BACKEND == 1);
#endif
#ifdef WEBCPP_TARGET_WASIP2
static_assert(WEBCPP_COBALT_HAS_THREADS == 0);
#endif
#if defined(WEBCPP_TARGET_WASIP2) || defined(WEBCPP_TARGET_WASIP3)
static_assert(WEBCPP_COBALT_HAS_SIGNALS == 0);
static_assert(WEBCPP_COBALT_HAS_LOCAL_SOCKETS == 0);
static_assert(WEBCPP_COBALT_HAS_SERIAL_PORT == 0);
static_assert(WEBCPP_COBALT_HAS_REVERSE_DNS == 0);
#endif
