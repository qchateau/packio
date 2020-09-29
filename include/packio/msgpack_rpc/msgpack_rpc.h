// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef PACKIO_MSGPACK_RPC_MSGPACK_RPC_H
#define PACKIO_MSGPACK_RPC_MSGPACK_RPC_H

//! @file
//! Typedefs and functions to use the msgpack-RPC protocol

#include "../client.h"
#include "../server.h"
#include "rpc.h"

//! @namespace packio::msgpack_rpc
//! The packio::msgpack_rpc namespace contains the msgpack-RPC implementation
namespace packio {
namespace msgpack_rpc {

//! The @ref packio::completion_handler "completion_handler" for msgpack-RPC
using completion_handler = completion_handler<rpc>;

//! The @ref packio::dispatcher "dispatcher" for msgpack-RPC
template <template <class...> class Map = default_map, typename Lockable = default_mutex>
using dispatcher = dispatcher<rpc, Map, Lockable>;

//! The @ref packio::client "client" for msgpack-RPC
template <typename Socket, template <class...> class Map = default_map>
using client = ::packio::client<rpc, Socket, Map>;

//! The @ref packio::make_client "make_client" function for msgpack-RPC
template <typename Socket, template <class...> class Map = default_map>
auto make_client(Socket&& socket)
{
    return std::make_shared<client<Socket, Map>>(std::forward<Socket>(socket));
}

//! The @ref packio::server "server" for msgpack-RPC
template <typename Acceptor, typename Dispatcher = dispatcher<>>
using server = ::packio::server<rpc, Acceptor, Dispatcher>;

//! The @ref packio::make_server "make_server" function for msgpack-RPC
template <typename Acceptor, typename Dispatcher = dispatcher<>>
auto make_server(Acceptor&& acceptor)
{
    return std::make_shared<server<Acceptor, Dispatcher>>(
        std::forward<Acceptor>(acceptor));
}

} // msgpack_rpc
} // packio

#endif // PACKIO_MSGPACK_RPC_MSGPACK_RPC_H
