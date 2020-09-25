// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef PACKIO_NL_JSON_RPC_NL_JSON_RPC_H
#define PACKIO_NL_JSON_RPC_NL_JSON_RPC_H

//! @file
//! Typedefs and functions to use the JSON-RPC protocol
//! based on the nlohmann::json library

#include "../client.h"
#include "../server.h"
#include "rpc.h"

//! @namespace packio::nl_json_rpc
//! The packio::nl_json_rpc namespace contains the JSON-RPC
//! implementation based on the nlohmann::json library
namespace packio {
namespace nl_json_rpc {

//! The completion_handler class
//!
//! First argument of @ref traits::AsyncProcedure "AsyncProcedure", the
//! completion_handler is a callable used to notify the completion of an
//! asynchronous procedure. You must only call @ref set_value or
//! @ref set_error once.
using completion_handler = completion_handler<rpc>;

//! The dispatcher class, used to store and dispatch procedures
//! @tparam Map The container used to associate procedures to their name
//! @tparam Lockable The lockable used to protect accesses to the procedure map
template <template <class...> class Map = default_map, typename Lockable = default_mutex>
using dispatcher = dispatcher<rpc, Map, Lockable>;

//! The JSON-RPC client class
//! @tparam Socket Socket type to use for this client
//! @tparam Map Container used to associate call IDs and handlers
template <typename Socket, template <class...> class Map = default_map>
using client = ::packio::client<rpc, Socket, Map>;

//! Create a JSON-RPC client from a socket
//! @tparam Socket Socket type to use for this client
//! @tparam Map Container used to associate call IDs and handlers
template <typename Socket, template <class...> class Map = default_map>
auto make_client(Socket&& socket)
{
    return std::make_shared<client<Socket, Map>>(std::forward<Socket>(socket));
}

//! The JSON-RPC server class
//! @tparam Acceptor Acceptor type to use for this server
//! @tparam Dispatcher Dispatcher used to store and dispatch procedures. See @ref dispatcher
template <typename Acceptor, typename Dispatcher = dispatcher<>>
using server = ::packio::server<rpc, Acceptor, Dispatcher>;

//! Create a JSON-RPC server from an acceptor
//! @tparam Acceptor Acceptor type to use for this server
//! @tparam Dispatcher Dispatcher used to store and dispatch procedures. See @ref dispatcher
template <typename Acceptor, typename Dispatcher = dispatcher<>>
auto make_server(Acceptor&& acceptor)
{
    return std::make_shared<server<Acceptor, Dispatcher>>(
        std::forward<Acceptor>(acceptor));
}

} // nl_json_rpc
} // packio

#endif // PACKIO_NL_JSON_RPC_NL_JSON_RPC_H
