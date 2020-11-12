// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef PACKIO_JSON_RPC_JSON_RPC_H
#define PACKIO_JSON_RPC_JSON_RPC_H

//! @file
//! Typedefs and functions to use the JSON-RPC protocol
//! based on the boost::json library

#include "../client.h"
#include "../server.h"
#include "rpc.h"

//! @namespace packio::json_rpc
//! The packio::json_rpc namespace contains the JSON-RPC
//! implementation based on the boost::json library
namespace packio {
namespace json_rpc {

//! The @ref packio::completion_handler "completion_handler" for JSON-RPC
using completion_handler = completion_handler<rpc>;

//! The @ref packio::dispatcher "dispatcher" for JSON-RPC
template <template <class...> class Map = default_map, typename Lockable = default_mutex>
using dispatcher = dispatcher<rpc, Map, Lockable>;

//! The @ref packio::client "client" for JSON-RPC
template <typename Socket, template <class...> class Map = default_map>
using client = ::packio::client<rpc, Socket, Map>;

//! The @ref packio::make_client "make_client" function for JSON-RPC
template <typename Socket, template <class...> class Map = default_map>
auto make_client(Socket&& socket)
{
    return std::make_shared<client<Socket, Map>>(std::forward<Socket>(socket));
}

//! The @ref packio::server "server" for JSON-RPC
template <typename Acceptor, typename Dispatcher = dispatcher<>>
using server = ::packio::server<rpc, Acceptor, Dispatcher>;

//! The @ref packio::make_server "make_server" function for JSON-RPC
template <typename Acceptor, typename Dispatcher = dispatcher<>>
auto make_server(Acceptor&& acceptor)
{
    return std::make_shared<server<Acceptor, Dispatcher>>(
        std::forward<Acceptor>(acceptor));
}

} // json_rpc
} // packio

#endif // PACKIO_JSON_RPC_JSON_RPC_H
