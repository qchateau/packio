// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef PACKIO_EXTRA_SSL_H
#define PACKIO_EXTRA_SSL_H

//! @dir
//! Namespace @ref packio::extra
//! @file
//! Adapters for SSL streams
//! @namespace packio::extra
//! Optional utilities that are not included by default

#include "../internal/config.h"
#include "../internal/utils.h"

#if PACKIO_STANDALONE_ASIO
#include <asio/ssl.hpp>
#else // PACKIO_STANDALONE_ASIO
#include <boost/asio/ssl.hpp>
#endif // PACKIO_STANDALONE_ASIO

namespace packio {
namespace extra {

//! Adapter class to support SSL streams
//! @tparam SslStream The SSL stream to adapt.
//! Most likely a asio::ssl::stream<asio::ip::tcp::socket>
template <typename SslStream>
class ssl_stream_adapter : public SslStream {
public:
    using protocol_type = typename SslStream::lowest_layer_type::protocol_type;

    using SslStream::SslStream;

    //! Reflect the state of the lower layer.
    bool is_open() const { return this->lowest_layer().is_open(); }

    //! Close the lowest layer.
    template <typename... Args>
    auto close(Args&&... args)
    {
        return this->lowest_layer().close(std::forward<Args>(args)...);
    }

    //! Cancel is not possible on websockets, raise a compile-time error is the
    //! user tries to use it.
    template <typename... Args>
    auto cancel(Args&&...)
    {
        static_assert(
            internal::always_false_v<Args...>,
            "websockets do not support cancel operations");
    }
};

//! Adapter class to support websockets servers
//! @tparam Acceptor The low level acceptor.
//! Most likely a boost::asio::ip::tcp::acceptor
//! @tparam SslStreamAdapter The SSL stream type to use.
//! Most likely a @ref ssl_stream_adapter "ssl_stream_adapter"
template <typename Acceptor, typename SslStreamAdapter>
class ssl_acceptor_adapter : public Acceptor {
public:
    template <typename Arg>
    explicit ssl_acceptor_adapter(Arg&& arg, packio::net::ssl::context& context)
        : Acceptor(std::forward<Arg>(arg)), context_(context)
    {
    }

    //! Don't need definition, but declaration is used to determine the socket type.
    SslStreamAdapter accept();

    //! Accept the low level connection and perform a SSL handshake.
    template <typename Handler>
    void async_accept(Handler&& handler)
    {
        Acceptor::async_accept([this, handler = std::forward<Handler>(handler)](
                                   auto ec, auto sock) mutable {
            if (ec) {
                handler(ec, SslStreamAdapter(std::move(sock), context_));
                return;
            }
            auto stream = std::make_unique<SslStreamAdapter>(
                std::move(sock), context_);
            stream->async_handshake(
                packio::net::ssl::stream_base::server,
                [handler = std::forward<Handler>(handler),
                 stream = std::move(stream)](auto ec) mutable {
                    handler(ec, std::move(*stream));
                });
        });
    }

private:
    packio::net::ssl::context& context_;
};

} // extra
} // packio

#endif // PACKIO_EXTRA_SSL_H
