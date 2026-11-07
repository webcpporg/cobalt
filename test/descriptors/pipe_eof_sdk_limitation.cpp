// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
#include <webcpp/test/exit_status.hpp>

#include <boost/core/lightweight_test.hpp>

#include <array>
#include <cerrno>
#include <cstdio>

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

// Exercise the SDK directly: no Cobalt or Asio headers or compiled sources.
// Returning EOF here must fail this limitation witness so support is re-audited.
int main() {
    std::array<int, 2> descriptors{};
    if (!BOOST_TEST_EQ(::pipe(descriptors.data()), 0)) {
        return webcpp::test::exit_status(boost::report_errors());
    }
    if (!BOOST_TEST_EQ(::fcntl(descriptors[0], F_SETFL, O_NONBLOCK), 0)) {
        BOOST_TEST_EQ(::close(descriptors[1]), 0);
        BOOST_TEST_EQ(::close(descriptors[0]), 0);
        return webcpp::test::exit_status(boost::report_errors());
    }
    char byte{};
    BOOST_TEST_EQ(::write(descriptors[1], "x", 1), 1);
    BOOST_TEST_EQ(::read(descriptors[0], &byte, 1), 1);
    BOOST_TEST_EQ(byte, 'x');
    errno = 0;
    const auto pending = ::read(descriptors[0], &byte, 1);
    const auto pending_error = errno;
    BOOST_TEST_EQ(pending, -1);
    BOOST_TEST_EQ(pending_error, EAGAIN);
    BOOST_TEST_EQ(::close(descriptors[1]), 0);

    // Poll with a zero timeout and read a nonblocking descriptor: neither can
    // hang even if a future SDK changes its close/readiness behavior.
    pollfd reader{.fd = descriptors[0], .events = POLLIN, .revents = 0};
    BOOST_TEST_EQ(::poll(&reader, 1, 0), 1);
    BOOST_TEST_EQ(reader.revents, POLLIN | POLLHUP);
    errno = 0;
    const auto after_close = ::read(descriptors[0], &byte, 1);
    const auto after_close_error = errno;
    BOOST_TEST_EQ(after_close, -1);
    BOOST_TEST_EQ(after_close_error, EAGAIN);
    BOOST_TEST_EQ(::close(descriptors[0]), 0);
    if (boost::report_errors() != 0) {
        return webcpp::test::exit_status(1);
    }
    std::puts(
        "SDK-LIMITATION: Emscripten reports POLLIN|POLLHUP after pipe writer close, "
        "but read returns EAGAIN; EOF remains unsupported");
    return webcpp::test::exit_status(0);
}
