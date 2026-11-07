// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
// See accompanying file LICENSE_1_0.txt or https://www.boost.org/LICENSE_1_0.txt.

#include <webcpp/cobalt/detail/native_ssl_transfer.hpp>

#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>

namespace webcpp::cobalt::detail {

void native_ssl_read::initiate(
    ::boost::cobalt::completion_handler<boost::system::error_code, std::size_t> complete) {
    if (stream.started_ && !stream.upgraded_) {
        complete(boost::asio::error::not_connected, 0);
        return;
    }
    visit(buffers, [&](const auto& sequence) {
        if (stream.upgraded_) {
            boost::asio::async_read(stream.stream_socket_, sequence, std::move(complete));
        } else {
            boost::asio::async_read(stream.stream_socket_.next_layer(), sequence,
                                    std::move(complete));
        }
    });
}

void native_ssl_write::initiate(
    ::boost::cobalt::completion_handler<boost::system::error_code, std::size_t> complete) {
    if (stream.started_ && !stream.upgraded_) {
        complete(boost::asio::error::not_connected, 0);
        return;
    }
    visit(buffers, [&](const auto& sequence) {
        if (stream.upgraded_) {
            boost::asio::async_write(stream.stream_socket_, sequence, std::move(complete));
        } else {
            boost::asio::async_write(stream.stream_socket_.next_layer(), sequence,
                                     std::move(complete));
        }
    });
}
}  // namespace webcpp::cobalt::detail
