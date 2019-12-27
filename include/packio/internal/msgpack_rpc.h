// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef PACKIO_MSGPACK_RPC_H
#define PACKIO_MSGPACK_RPC_H

namespace packio {

enum class msgpack_rpc_type { request = 0, response = 1, notification = 2 };

} // packio

#endif // PACKIO_MSGPACK_RPC_H