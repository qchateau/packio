// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef PACKIO_SERVER_SESSION_H
#define PACKIO_SERVER_SESSION_H

//! @file
//! Class @ref packio::server_session "server_session"

#include <memory>
#include <queue>

#include "handler.h"
#include "internal/config.h"
#include "internal/log.h"
#include "internal/manual_strand.h"
#include "internal/rpc.h"
#include "internal/utils.h"

namespace packio {

//! The server_session class, created by the @ref server
template <typename Rpc, typename Socket, typename Dispatcher>
class server_session
    : public std::enable_shared_from_this<server_session<Rpc, Socket, Dispatcher>> {
public:
    using socket_type = Socket; //!< The socket type
    using protocol_type =
        typename socket_type::protocol_type; //!< The protocol type
    using executor_type =
        typename socket_type::executor_type; //!< The executor type

    using std::enable_shared_from_this<server_session<Rpc, Socket, Dispatcher>>::shared_from_this;

    //! The default size reserved by the reception buffer
    static constexpr size_t kDefaultBufferReserveSize = 4096;

    server_session(socket_type sock, std::shared_ptr<Dispatcher> dispatcher_ptr)
        : socket_{std::move(sock)},
          dispatcher_ptr_{std::move(dispatcher_ptr)},
          strand_(socket_.get_executor()),
          wstrand_{strand_}
    {
    }

    //! Get the underlying socket
    socket_type& socket() { return socket_; }
    //! Get the underlying socket, const
    const socket_type& socket() const { return socket_; }

    //! Get the executor associated with the object
    executor_type get_executor() { return socket().get_executor(); }

    //! Set the size reserved by the reception buffer
    void set_buffer_reserve_size(std::size_t size) noexcept
    {
        buffer_reserve_size_ = size;
    }
    //! Get the size reserved by the reception buffer
    std::size_t get_buffer_reserve_size() const noexcept
    {
        return buffer_reserve_size_;
    }

    //! Start the session
    void start()
    {
        net::dispatch(strand_, [self = shared_from_this()]() {
            self->async_read(parser_type{});
        });
    }

private:
    using parser_type = typename Rpc::incremental_parser_type;
    using request_type = typename Rpc::request_type;

    void async_read(parser_type&& parser)
    {
        assert(strand_.running_in_this_thread());

        // abort R/W on error
        if (!socket_.is_open()) {
            return;
        }

        parser.reserve_buffer(buffer_reserve_size_);
        auto buffer = net::buffer(parser.buffer(), parser.buffer_capacity());

        socket_.async_read_some(
            buffer,
            internal::bind_executor(
                strand_,
                [self = shared_from_this(), parser = std::move(parser)](
                    error_code ec, size_t length) mutable {
                    assert(self->strand_.running_in_this_thread());

                    if (ec) {
                        PACKIO_WARN("read error: {}", ec.message());
                        self->close_connection();
                        return;
                    }

                    PACKIO_TRACE("read: {}", length);
                    parser.buffer_consumed(length);

                    while (true) {
                        auto request = parser.get_request();
                        if (!request) {
                            PACKIO_INFO("stop reading: {}", request.error());
                            break;
                        }
                        // handle the call asynchronously (post)
                        // to schedule the next read immediately
                        // this will allow parallel call handling
                        // in multi-threaded environments
                        net::post(
                            self->get_executor(),
                            [self, request = std::move(*request)]() mutable {
                                self->async_handle_request(std::move(request));
                            });
                    }

                    self->async_read(std::move(parser));
                }));
    }

    void async_handle_request(request_type&& request)
    {
        completion_handler<Rpc> handler(
            request.id,
            [type = request.type, id = request.id, self = shared_from_this()](
                auto&& response_buffer) {
                if (type == call_type::request) {
                    PACKIO_TRACE("result (id={})", Rpc::format_id(id));
                    (void)id;
                    self->async_send_response(std::move(response_buffer));
                }
            });

        const auto function = dispatcher_ptr_->get(request.method);
        if (function) {
            PACKIO_TRACE(
                "call: {} (id={})", request.method, Rpc::format_id(request.id));
            (*function)(std::move(handler), std::move(request.args));
        }
        else {
            PACKIO_DEBUG("unknown function {}", request.method);
            handler.set_error("unknown function");
        }
    }

    template <typename Buffer>
    void async_send_response(Buffer&& response_buffer)
    {
        // abort R/W on error
        if (!socket_.is_open()) {
            return;
        }

        auto message_ptr = internal::to_unique_ptr(std::move(response_buffer));

        wstrand_.push([this,
                       self = shared_from_this(),
                       message_ptr = std::move(message_ptr)]() mutable {
            assert(strand_.running_in_this_thread());
            auto buf = Rpc::buffer(*message_ptr);
            net::async_write(
                socket_,
                buf,
                internal::bind_executor(
                    strand_,
                    [self = std::move(self), message_ptr = std::move(message_ptr)](
                        error_code ec, size_t length) {
                        self->wstrand_.next();

                        if (ec) {
                            PACKIO_WARN("write error: {}", ec.message());
                            self->close_connection();
                            return;
                        }

                        PACKIO_TRACE("write: {}", length);
                        (void)length;
                    }));
        });
    }

    void close_connection()
    {
        error_code ec;
        socket_.close(ec);
        if (ec) {
            PACKIO_WARN("close error: {}", ec.message());
        }
    }

    socket_type socket_;
    std::size_t buffer_reserve_size_{kDefaultBufferReserveSize};
    std::shared_ptr<Dispatcher> dispatcher_ptr_;

    net::strand<executor_type> strand_;
    internal::manual_strand<executor_type> wstrand_;
};

} // packio

#endif // PACKIO_SERVER_SESSION_H
