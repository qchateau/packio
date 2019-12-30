// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef PACKIO_SERVER_H
#define PACKIO_SERVER_H

#include <memory>
#include <boost/asio.hpp>
#include <msgpack.hpp>

#include "dispatcher.h"
#include "internal/log.h"
#include "internal/utils.h"
#include "server_session.h"

namespace packio {

template <typename Protocol, typename Dispatcher = dispatcher<>>
class server {
public:
    using protocol_type = Protocol;
    using dispatcher_type = Dispatcher;
    using session_type = server_session<protocol_type, dispatcher_type>;
    using acceptor_type = typename Protocol::acceptor;
    using socket_type = typename Protocol::socket;
    using executor_type = typename boost::asio::io_context::executor_type;

    explicit server(acceptor_type acceptor)
        : server{std::move(acceptor), std::make_shared<dispatcher_type>()}
    {
    }

    server(acceptor_type acceptor, std::shared_ptr<dispatcher_type> dispatcher)
        : acceptor_{std::move(acceptor)}, dispatcher_ptr_{std::move(dispatcher)}
    {
    }

    ~server()
    {
        boost::system::error_code ec;
        acceptor_.cancel(ec);
        if (ec) {
            WARN("cancel failed: {}", ec.message());
        }
        INFO("stopped server");
    }

    acceptor_type& acceptor() { return acceptor_; }
    const acceptor_type& acceptor() const { return acceptor_; }

    std::shared_ptr<dispatcher_type> dispatcher() { return dispatcher_ptr_; }
    std::shared_ptr<const dispatcher_type> dispatcher() const
    {
        return dispatcher_ptr_;
    }

    executor_type get_executor() { return acceptor().get_executor(); }

    template <typename ServerHandler>
    void async_serve(ServerHandler&& handler)
    {
        TRACE("async_serve");
        acceptor_.async_accept(
            [this, handler = std::forward<ServerHandler>(handler)](
                boost::system::error_code ec, socket_type sock) mutable {
                std::shared_ptr<session_type> session;
                if (ec) {
                    WARN("accept error: {}", ec.message());
                }
                else {
                    internal::set_no_delay(sock);
                    session = std::make_shared<session_type>(
                        std::move(sock), dispatcher_ptr_);
                }
                handler(ec, std::move(session));
            });
    }

    void async_serve_forever()
    {
        async_serve([this](auto ec, auto session) {
            if (ec) {
                return;
            }

            session->start();
            async_serve_forever();
        });
    }

private:
    acceptor_type acceptor_;
    std::shared_ptr<dispatcher_type> dispatcher_ptr_;
};

} // packio

#endif // PACKIO_SERVER_H