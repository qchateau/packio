#ifndef PACKIO_PACKIO_H
#define PACKIO_PACKIO_H

//! @dir
//! Namespace @ref packio
//! @file
//! Main include file
//! @namespace packio
//! The packio namespace

#include "internal/config.h"

#include "arg.h"
#include "client.h"
#include "dispatcher.h"
#include "handler.h"
#include "server.h"

#if PACKIO_HAS_MSGPACK
#include "msgpack_rpc/msgpack_rpc.h"
#endif // PACKIO_HAS_MSGPACK

#if PACKIO_HAS_NLOHMANN_JSON
#include "nl_json_rpc/nl_json_rpc.h"
#endif // PACKIO_HAS_NLOHMANN_JSON

#if PACKIO_HAS_BOOST_JSON
#include "json_rpc/json_rpc.h"
#endif // PACKIO_HAS_BOOST_JSON

#endif // PACKIO_PACKIO_H
