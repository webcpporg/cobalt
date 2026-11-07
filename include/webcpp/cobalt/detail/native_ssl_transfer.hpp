// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
// See accompanying file LICENSE_1_0.txt or https://www.boost.org/LICENSE_1_0.txt.

#ifndef WEBCPP_COBALT_DETAIL_NATIVE_SSL_TRANSFER_HPP
#define WEBCPP_COBALT_DETAIL_NATIVE_SSL_TRANSFER_HPP

#include <webcpp/cobalt/config.hpp>

#ifdef WEBCPP_TARGET_NATIVE
#include <boost/cobalt/io/read.hpp>
#include <boost/cobalt/io/write.hpp>
#include <webcpp/cobalt/detail/native_ssl.hpp>

namespace webcpp::cobalt::detail {
/** A lazy native SSL read that preserves the caller's cancellation association. */
struct native_ssl_read final : ::boost::cobalt::op<boost::system::error_code, std::size_t> {
    /** The concrete SSL stream, retained until completion. */
    io::ssl::stream& stream;
    /** The caller's writable storage. */
    io::mutable_buffer_sequence buffers;

    /** Captures the stream and buffers without starting I/O. */
    native_ssl_read(io::ssl::stream& value, io::mutable_buffer_sequence const& sequence)
        : stream(value), buffers(sequence) {}

    /** Starts the read with the awaiting coroutine's completion association. */
    void initiate(
        ::boost::cobalt::completion_handler<boost::system::error_code, std::size_t> complete) final;
};

/** A lazy native SSL write that preserves the caller's cancellation association. */
struct native_ssl_write final : ::boost::cobalt::op<boost::system::error_code, std::size_t> {
    /** The concrete SSL stream, retained until completion. */
    io::ssl::stream& stream;
    /** The caller's readable storage. */
    io::const_buffer_sequence buffers;

    /** Captures the stream and buffers without starting I/O. */
    native_ssl_write(io::ssl::stream& value, io::const_buffer_sequence const& sequence)
        : stream(value), buffers(sequence) {}

    /** Starts the write with the awaiting coroutine's completion association. */
    void initiate(
        ::boost::cobalt::completion_handler<boost::system::error_code, std::size_t> complete) final;
};
}  // namespace webcpp::cobalt::detail

namespace webcpp::cobalt::io {
/** Retains Boost's composed reads for native non-SSL stream types. */
using ::boost::cobalt::io::read;
/** Retains Boost's composed writes for native non-SSL stream types. */
using ::boost::cobalt::io::write;

/** Reads the complete buffer through the corrected native SSL wrapper.

 The operation starts when awaited. The stream and buffer storage must outlive
 completion. Cancellation returns operation_aborted and the transferred count.
 Keep the concrete SSL stream type when selecting this overload; erasing it to
 Boost's generic stream selects the upstream composition and its limitations.

 @param stream SSL stream, or a STARTTLS stream before its first handshake.
 @param buffers Writable storage that remains valid until completion.
 @return A lazy operation with completion signature void(error_code, size_t).
*/
inline ::webcpp::cobalt::detail::native_ssl_read read(ssl::stream& stream,
                                                      mutable_buffer_sequence const& buffers) {
    return {stream, buffers};
}

/** Writes the complete buffer through the corrected native SSL wrapper.

 The operation starts when awaited. A failed or cancelled handshake prevents
 application writes. The buffer storage must outlive completion. Cancellation
 preserves the transferred count and does not close the underlying stream.

 @param stream SSL stream, or a STARTTLS stream before its first handshake.
 @param buffers Readable storage that remains valid until completion.
 @return A lazy operation with completion signature void(error_code, size_t).
*/
inline ::webcpp::cobalt::detail::native_ssl_write write(ssl::stream& stream,
                                                        const_buffer_sequence const& buffers) {
    return {stream, buffers};
}
}  // namespace webcpp::cobalt::io
#endif
#endif
