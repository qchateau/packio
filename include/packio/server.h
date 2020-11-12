// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef PACKIO_SERVER_H
#define PACKIO_SERVER_H

//! @file
//! Class @ref packio::server "server"

#include <memory>

#include "dispatcher.h"
#include "internal/config.h"
#include "internal/log.h"
#include "internal/utils.h"
#include "server_session.h"
#include "traits.h"

namespace packio {

//! The server class
//! @tparam Rpc RPC protocol implementation
//! @tparam Acceptor Acceptor type to use for this server
//! @tparam Dispatcher Dispatcher used to store and dispatch procedures. See @ref dispatcher
template <typename Rpc, typename Acceptor, typename Dispatcher = dispatcher<Rpc>>
class server
    : public std::enable_shared_from_this<server<Rpc, Acceptor, Dispatcher>> {
public:
    using rpc_type = Rpc; //!< The RPC protocol type
    using acceptor_type = Acceptor; //!< The acceptor type
    using protocol_type = typename Acceptor::protocol_type; //!< The protocol type
    using dispatcher_type = Dispatcher; //!< The dispatcher type
    using executor_type =
        typename acceptor_type::executor_type; //!< The executor type
    using socket_type = std::decay_t<decltype(
        std::declval<acceptor_type>().accept())>; //!< The connection socket type
    using session_type = server_session<rpc_type, socket_type, dispatcher_type>;

    using std::enable_shared_from_this<server<Rpc, Acceptor, Dispatcher>>::shared_from_this;

    //! The constructor
    //!
    //! @param acceptor The acceptor that the server will use
    //! @param dispatcher A shared pointer to the dispatcher that the server will use
    server(acceptor_type acceptor, std::shared_ptr<dispatcher_type> dispatcher)
        : acceptor_{std::move(acceptor)}, dispatcher_ptr_{std::move(dispatcher)}
    {
    }

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
    template <PACKIO_COMPLETION_TOKEN_FOR(void(error_code, std::shared_ptr<session_type>))
                  ServeHandler PACKIO_DEFAULT_COMPLETION_TOKEN_TYPE(executor_type)>
    auto async_serve(
        ServeHandler&& handler PACKIO_DEFAULT_COMPLETION_TOKEN(executor_type))
    {
        return net::async_initiate<
            ServeHandler,
            void(error_code, std::shared_ptr<session_type>)>(
            initiate_async_serve(this), handler);
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
    class initiate_async_serve {
    public:
        using executor_type = typename server::executor_type;

        explicit initiate_async_serve(server* self) : self_(self) {}

        executor_type get_executor() const noexcept
        {
            return self_->get_executor();
        }

        template <typename ServeHandler>
        void operator()(ServeHandler&& handler)
        {
            PACKIO_STATIC_ASSERT_TTRAIT(ServeHandler, session_type);
            PACKIO_TRACE("async_serve");

            self_->acceptor_.async_accept(
                [self = self_->shared_from_this(),
                 handler = std::forward<ServeHandler>(handler)](
                    error_code ec, socket_type sock) mutable {
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

    private:
        server* self_;
    };

    acceptor_type acceptor_;
    std::shared_ptr<dispatcher_type> dispatcher_ptr_;
};

//! Create a server from an acceptor
//! @tparam Rpc RPC protocol implementation
//! @tparam Acceptor Acceptor type to use for this server
//! @tparam Dispatcher Dispatcher used to store and dispatch procedures. See @ref dispatcher
template <typename Rpc, typename Acceptor, typename Dispatcher = dispatcher<Rpc>>
auto make_server(Acceptor&& acceptor)
{
    return std::make_shared<server<Rpc, Acceptor, Dispatcher>>(
        std::forward<Acceptor>(acceptor));
}

} // packio

#endif // PACKIO_SERVER_H
