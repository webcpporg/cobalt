// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

/**
 The WebCPP Cobalt configuration and coroutine interfaces.

 On native targets coroutine types are the original Boost.Cobalt types.
 The optional io::ssl wrapper is WebCPP's corrected implementation on every
 guest TLS target, including native.
 WebAssembly selects the shipped coroutine implementation and Asio backend.
 Optional I/O and SSL wrappers have webcpp/cobalt/io headers; SSL requires the io_ssl or
 cobalt_ssl build target and the shared OpenSSL provider.
*/
#ifndef WEBCPP_COBALT_HPP
#define WEBCPP_COBALT_HPP

#include <webcpp/cobalt/config.hpp>

#ifdef WEBCPP_TARGET_NATIVE
#include <boost/cobalt.hpp>
#else
#include <webcpp/cobalt/detail/backend.hpp>
#endif
#include <webcpp/cobalt/async_for.hpp>
#include <webcpp/cobalt/result.hpp>

#endif  // WEBCPP_COBALT_HPP
