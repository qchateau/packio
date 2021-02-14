// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef PACKIO_EXTRA_WEBSOCKET_H
#define PACKIO_EXTRA_WEBSOCKET_H

//! @dir
//! Namespace @ref packio::extra
//! @file
//! Adapters for websockets
//! @namespace packio::extra
//! Optional utilities that are not included by default

#include "../internal/config.h"
#include "../internal/utils.h"

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

namespace packio {
namespace extra {

//! Adapter class to support websockets
//! @tparam Websocket The websocket type to adapt.
//! @tparam kBinary Configure the websocket in binary mode.
//! Most likely a boost::beast::websocket::stream<boost::beast::tcp_stream>
template <typename Websocket, bool kBinary = false>
class websocket_adapter : public Websocket {
public:
    using protocol_type = typename Websocket::next_layer_type::protocol_type;

    template <typename... Args>
    websocket_adapter(Args&&... args) : Websocket(std::forward<Args>(args)...)
    {
        Websocket::binary(kBinary);
    }

    //! Set the no_delay option on the lowest layer and Write a websocket message.
    template <typename Buffer, typename Handler>
    auto async_write_some(Buffer&& buffer, Handler&& handler)
    {
        internal::set_no_delay(boost::beast::get_lowest_layer(*this).socket());
        return Websocket::async_write(
            std::forward<Buffer>(buffer), std::forward<Handler>(handler));
    }

    //! Close the lowest layer, closing a websocket is a blocking operation
    //! which packio does not expect.
    template <typename... Args>
    auto close(Args&&... args)
    {
        return boost::beast::get_lowest_layer(*this).socket().close(
            std::forward<Args>(args)...);
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
//! @tparam WebsocketAdapter The websocket type to use.
//! Most likely a @ref packio::extra::websocket_adapter "websocket_adapter"
template <typename Acceptor, typename WebsocketAdapter>
class websocket_acceptor_adapter : public Acceptor {
public:
    using Acceptor::Acceptor;

    //! Don't need definition, but declaration is used to determine the socket type
    WebsocketAdapter accept();

    //! Accept the low level connection and configure the recommended timeout
    //! settings. If you need to configure the websocket differently, you can
    //! either write your own adapter, of change the session socket options in
    //! @ref packio::server::async_serve "async_serve"
    template <typename Handler>
    void async_accept(Handler&& handler)
    {
        Acceptor::async_accept([handler = std::forward<Handler>(handler)](
                                   auto ec, auto sock) mutable {
            if (ec) {
                handler(ec, WebsocketAdapter(std::move(sock)));
                return;
            }
            auto ws = std::make_unique<WebsocketAdapter>(std::move(sock));
            ws->set_option(boost::beast::websocket::stream_base::timeout::suggested(
                boost::beast::role_type::server));
            ws->async_accept([handler = std::forward<Handler>(handler),
                              ws = std::move(ws)](auto ec) mutable {
                handler(ec, std::move(*ws));
            });
        });
    }
};

} // extra
} // packio

#endif // PACKIO_EXTRA_WEBSOCKET_H
