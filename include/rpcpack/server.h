#ifndef RPCPACK_SERVER_H
#define RPCPACK_SERVER_H

#include <memory>
#include <boost/asio.hpp>
#include <msgpack.hpp>

#include "dispatcher.h"
#include "internal/log.h"
#include "internal/server_session.h"
#include "internal/utils.h"

namespace rpcpack {

template <typename Protocol, typename Dispatcher>
class server {
public:
    using protocol_type = Protocol;
    using dispatcher_type = Dispatcher;
    using session_type = internal::server_session<protocol_type, dispatcher_type>;
    using acceptor_type = typename Protocol::acceptor;
    using socket_type = typename Protocol::socket;
    using executor_type = typename boost::asio::io_context::executor_type;
    using async_serve_handler_type =
        std::function<void(boost::system::error_code, std::shared_ptr<session_type>)>;

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
            INFO("cancel failed: {}", ec.message());
        }
        DEBUG("stopped server");
    }

    acceptor_type& acceptor() { return acceptor_; }
    const acceptor_type& acceptor() const { return acceptor_; }

    std::shared_ptr<dispatcher_type> dispatcher() { return dispatcher_ptr_; }
    std::shared_ptr<const dispatcher_type> dispatcher() const
    {
        return dispatcher_ptr_;
    }

    executor_type get_executor() { return acceptor().get_executor(); }

    void async_serve(async_serve_handler_type handler)
    {
        TRACE("async_serve");
        acceptor_.async_accept(
            [this, handler](boost::system::error_code ec, socket_type sock) {
                std::shared_ptr<session_type> session;
                if (!ec) {
                    session = std::make_shared<session_type>(
                        std::move(sock), dispatcher_ptr_);
                }
                else {
                    DEBUG("error: {}", ec.message());
                }

                handler(ec, std::move(session));
            });
    }

    void async_serve_forever()
    {
        async_serve([this](auto ec, auto session) {
            if (ec) {
                INFO("error: {}", ec.message());
            }
            else {
                session->start();
                async_serve_forever();
            }
        });
    }

private:
    acceptor_type acceptor_;
    std::shared_ptr<dispatcher_type> dispatcher_ptr_;
};

using ip_server = server<boost::asio::ip::tcp, default_dispatcher>;

#if defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)
using local_server =
    server<boost::asio::local::stream_protocol, default_dispatcher>;
#endif // defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)

} // rpcpack

#endif // RPCPACK_SERVER_H