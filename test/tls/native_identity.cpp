// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
// See accompanying file LICENSE_1_0.txt or https://www.boost.org/LICENSE_1_0.txt.

#ifdef WEBCPP_TEST_BOOST_FIRST
#include <boost/cobalt/io/ssl.hpp>
#endif
#include <type_traits>

#include <webcpp/cobalt/io/ssl.hpp>
#include <webcpp/cobalt/task.hpp>

#ifndef WEBCPP_TEST_BOOST_FIRST
#include <boost/cobalt/io/ssl.hpp>
#endif

static_assert(std::is_same_v<webcpp::cobalt::task<int>, boost::cobalt::task<int>>);
static_assert(std::is_same_v<webcpp::cobalt::io::stream_socket, boost::cobalt::io::stream_socket>);
static_assert(std::is_same_v<webcpp::cobalt::io::ssl::context, boost::asio::ssl::context>);
static_assert(!std::is_same_v<webcpp::cobalt::io::ssl::stream, boost::cobalt::io::ssl::stream>);
