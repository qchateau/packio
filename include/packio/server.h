// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef PACKIO_SERVER_H
#define PACKIO_SERVER_H

//! @file
//! Class @ref packio::server "server"

#include <memory>
#include <boost/asio.hpp>
#include <msgpack.hpp>

#include "dispatcher.h"
#include "internal/log.h"
#include "internal/utils.h"
#include "server_session.h"
#include "traits.h"

namespace packio {

//! The server class
//! @tparam Protocol Protocol to use for this client, use a boost::asio protocol
//! @tparam Dispatcher Dispatcher used to store and dispatch procedures. See @ref dispatcher
template <typename Protocol, typename Dispatcher = default_dispatcher>
class server : public std::enable_shared_from_this<server<Protocol, Dispatcher>> {
public:
    using protocol_type = Protocol; //!< The protocol type
    using dispatcher_type = Dispatcher; //!< The dispatcher type
    using session_type =
        server_session<protocol_type, dispatcher_type>; //!< The session type
    using acceptor_type = typename Protocol::acceptor; //!< The acceptor type
    using socket_type = typename Protocol::socket; //!< The socket type
    using executor_type =
        typename acceptor_type::executor_type; //!< The executor type
    using std::enable_shared_from_this<server<Protocol, Dispatcher>>::shared_from_this;

    //! The constructor
    //!
    //! @param acceptor The acceptor that the server will use
    //! @param dispatcher A shared pointer to the dispatcher that the server will use
    server(acceptor_type acceptor, std::shared_ptr<dispatcher_type> dispatcher)
        : acceptor_{std::move(acceptor)}, dispatcher_ptr_{std::move(dispatcher)}
    {
    }

    //! Simplified constructor: will allocate a new dispatcher automatically
    //! @overload
    explicit server(acceptor_type acceptor)
        : server{std::move(acceptor), std::make_shared<dispatcher_type>()}
    {
    }

    //! Get the underlying acceptor
    acceptor_type& acceptor() { return acceptor_; }
    //! Get the underlying acceptor, const
    const acceptor_type& acceptor() const { return acceptor_; }

    //! Get the dispatcher
    std::shared_ptr<dispatcher_type> dispatcher() { return dispatcher_ptr_; }
    //! Get the dispatcher, const
    std::shared_ptr<const dispatcher_type> dispatcher() const
    {
        return dispatcher_ptr_;
    }

    //! Get the executor associated with the object
    executor_type get_executor() { return acceptor().get_executor(); }

    //! Accept one connection and initialize a session for it
    //!
    //! @param handler Handler called when a connection is accepted.
    //! The handler is responsible for calling server_session::start.
    //! Must satisfy the @ref traits::ServeHandler trait
    template <typename ServeHandler>
    void async_serve(ServeHandler&& handler)
    {
        PACKIO_STATIC_ASSERT_TTRAIT(ServeHandler, session_type);
        PACKIO_TRACE("async_serve");
        acceptor_.async_accept(
            [self = shared_from_this(),
             handler = std::forward<ServeHandler>(handler)](
                boost::system::error_code ec, socket_type sock) mutable {
                std::shared_ptr<session_type> session;
                if (ec) {
                    PACKIO_WARN("accept error: {}", ec.message());
                }
                else {
                    internal::set_no_delay(sock);
                    session = std::make_shared<session_type>(
                        std::move(sock), self->dispatcher_ptr_);
                }
                handler(ec, std::move(session));
            });
    }

    //! Accept connections and automatically start the associated sessions forever
    void async_serve_forever()
    {
        async_serve([self = shared_from_this()](auto ec, auto session) {
            if (ec) {
                return;
            }

            session->start();
            self->async_serve_forever();
        });
    }

private:
    acceptor_type acceptor_;
    std::shared_ptr<dispatcher_type> dispatcher_ptr_;
};

} // packio

#endif // PACKIO_SERVER_H