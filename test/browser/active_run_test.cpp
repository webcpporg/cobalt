// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

#include <webcpp/test/exit_status.hpp>

#include "../../example/applications/active_run.hpp"

#include <boost/core/lightweight_test.hpp>
#include <boost/throw_exception.hpp>

#include <stdexcept>

namespace {

#ifndef BOOST_NO_EXCEPTIONS
void fail_during_run(int& state) {
    const active_run registration("registry-unwind", &state);
    BOOST_TEST(registration.registered());
    BOOST_TEST(active_run::find("registry-unwind") == &state);
    boost::throw_exception(std::runtime_error("expected registry unwind"));
}
#endif

}  // namespace

int main() {
    int first = 1;
    int second = 2;
    BOOST_TEST(active_run::find("registry-test") == nullptr);
    {
        active_run registration("registry-test", &first);
        BOOST_TEST(registration.registered());
        BOOST_TEST(active_run::find("registry-test") == &first);
        {
            const active_run duplicate("registry-test", &second);
            BOOST_TEST(!duplicate.registered());
            BOOST_TEST(active_run::find("registry-test") == &first);
        }
        BOOST_TEST(active_run::find("registry-test") == &first);
        registration.reset();
        BOOST_TEST(!registration.registered());
        BOOST_TEST(active_run::find("registry-test") == nullptr);
        const active_run next("registry-test", &second);
        BOOST_TEST(next.registered());
        BOOST_TEST(active_run::find("registry-test") == &second);
    }
    BOOST_TEST(active_run::find("registry-test") == nullptr);
#ifndef BOOST_NO_EXCEPTIONS
    bool caught = false;
    try {
        fail_during_run(first);
    } catch (const std::runtime_error&) {
        caught = true;
    }
    BOOST_TEST(caught);
    BOOST_TEST(active_run::find("registry-unwind") == nullptr);
#endif
    return webcpp::test::exit_status(boost::report_errors());
}
