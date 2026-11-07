// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

// Native Boost omits this promise hook without exceptions. MSVC refuses the
// coroutine; the same source must compile with exceptions enabled.
#include <webcpp/cobalt/main.hpp>

#include <boost/cobalt/main.hpp>

#include <concepts>
#include <coroutine>

static_assert(std::same_as<webcpp::cobalt::main, boost::cobalt::main>);

#ifdef WEBCPP_TEST_MAIN_PROMISE_SHAPE
#ifndef BOOST_NO_EXCEPTIONS
#error "The missing-hook witness must be compiled without exceptions"
#endif

template <class Promise>
concept has_unhandled_exception = requires(Promise& promise) { promise.unhandled_exception(); };

using main_promise = std::coroutine_traits<webcpp::cobalt::main, int, char**>::promise_type;
static_assert(!has_unhandled_exception<main_promise>);
#else
webcpp::cobalt::main co_main([[maybe_unused]] int argc, [[maybe_unused]] char** argv) {
    co_return 0;
}
#endif
