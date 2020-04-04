// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef PACKIO_CONFIG_H
#define PACKIO_CONFIG_H

#if defined(BOOST_ASIO_HAS_CO_AWAIT)
#define PACKIO_HAS_CO_AWAIT BOOST_ASIO_HAS_CO_AWAIT
#endif // defined(BOOST_ASIO_HAS_CO_AWAIT)

#if defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)
#define PACKIO_HAS_LOCAL_SOCKETS BOOST_ASIO_HAS_LOCAL_SOCKETS
#endif // defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)

#endif // PACKIO_CONFIG_H
