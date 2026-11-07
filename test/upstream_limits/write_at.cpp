// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

#include <webcpp/cobalt/io/random_access_file.hpp>
#include <webcpp/cobalt/io/write.hpp>

void positioned_write(webcpp::cobalt::io::random_access_file& file,
                      webcpp::cobalt::io::const_buffer_sequence buffers) {
    // Boost 1.92 requires write_op even though write_some_at returns write_at_op.
    // expected-error@+1 {{no matching function for call to 'write_at'}}
    auto operation = webcpp::cobalt::io::write_at(file, 0, buffers);
    static_cast<void>(operation);
}
