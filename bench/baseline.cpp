// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

#ifdef WEBCPP_BENCH_COBALT
#include <webcpp/cobalt/main.hpp>

webcpp::cobalt::main co_main(int, char**) {
    co_return 0;
}
#else
#include <webcpp/asio/io_context.hpp>

int main() {
    webcpp::asio::io_context context;
    context.run();  // lint-run: measure construction and an empty run of the actual executor
}
#endif
