// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef PACKIO_SERVER_SESSION_H
#define PACKIO_SERVER_SESSION_H

//! @file
//! Class @ref packio::server_session "server_session"

#include <memory>
#include <queue>
#include <msgpack.hpp>

#include "error_code.h"
#include "internal/config.h"
#include "internal/log.h"
#include "internal/manual_strand.h"
#include "internal/msgpack_rpc.h"
#include "internal/utils.h"

namespace packio {

//! The server_session class, created by the @ref server
template <typename Socket, typename Dispatcher>
class server_session
    : public std::enable_shared_from_this<server_session<Socket, Dispatcher>> {
public:
    using socket_type = Socket; //!< The socket type
    using protocol_type =
        typename socket_type::protocol_type; //!< The protocol type
    using executor_type =
        typename socket_type::executor_type; //!< The executor type
    using std::enable_shared_from_this<server_session<Socket, Dispatcher>>::shared_from_this;

    //! The default size reserved by the reception buffer
    static constexpr size_t kDefaultBufferReserveSize = 4096;

    server_session(socket_type sock, std::shared_ptr<Dispatcher> dispatcher_ptr)
        : socket_{std::move(sock)},
          dispatcher_ptr_{std::move(dispatcher_ptr)},
          wstrand_{socket_.get_executor()}
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
    void start() { async_read(std::make_unique<msgpack::unpacker>()); }

private:
    using buffer_type = msgpack::vrefbuffer; // non-owning buffer
    using message_type = std::tuple<buffer_type, msgpack::object_handle>;
    using message_queue = std::queue<std::unique_ptr<message_type>>;

    struct Call {
        msgpack_rpc_type type;
        id_type id;
        std::string name;
        msgpack::object args;
    };

    void async_read(std::unique_ptr<msgpack::unpacker> unpacker)
    {
        // abort R/W on error
        if (!socket_.is_open()) {
            return;
        }

        unpacker->reserve_buffer(buffer_reserve_size_);
        auto buffer = packio::asio::buffer(
            unpacker->buffer(), unpacker->buffer_capacity());
        socket_.async_read_some(
            buffer,
            [self = shared_from_this(), unpacker = std::move(unpacker)](
                packio::err::error_code ec, size_t length) mutable {
                if (ec) {
                    PACKIO_WARN("read error: {}", ec.message());
                    self->close_connection();
                    return;
                }

                PACKIO_TRACE("read: {}", length);
                unpacker->buffer_consumed(length);

                for (msgpack::object_handle call; unpacker->next(call);) {
                    // handle the call asynchronously (post)
                    // to schedule the next read immediately
                    // this will allow parallel call handling
                    // in multi-threaded environments
                    packio::asio::post(
                        self->get_executor(), [self, call = std::move(call)] {
                            self->dispatch(call.get());
                        });
                }

                self->async_read(std::move(unpacker));
            });
    }

    void dispatch(const msgpack::object& msgpack_call)
    {
        std::optional<Call> call = parse_call(msgpack_call);
        if (!call) {
            close_connection();
            return;
        }

        auto completion_handler =
            [type = call->type, id = call->id, self = shared_from_this()](
                packio::err::error_code ec, msgpack::object_handle result) {
                if (type == msgpack_rpc_type::request) {
                    PACKIO_TRACE("result: {}", ec.message());
                    self->async_send_result(id, ec, std::move(result));
                }
            };

        const auto function = dispatcher_ptr_->get(call->name);
        if (function) {
            PACKIO_TRACE("call: {} (id={})", call->name, call->id);
            (*function)(completion_handler, call->args);
        }
        else {
            PACKIO_DEBUG("unknown function {}", call->name);
            completion_handler(make_error_code(error::unknown_procedure), {});
        }
    }

    std::optional<Call> parse_call(const msgpack::object& call)
    {
        if (call.type != msgpack::type::ARRAY || call.via.array.size < 3) {
            PACKIO_ERROR("unexpected message type: {}", call.type);
            return std::nullopt;
        }

        try {
            int idx = 0;
            id_type id = 0;
            msgpack_rpc_type type = static_cast<msgpack_rpc_type>(
                call.via.array.ptr[idx++].as<int>());

            std::size_t expected_size;
            switch (type) {
            case msgpack_rpc_type::request:
                id = call.via.array.ptr[idx++].as<id_type>();
                expected_size = 4;
                break;
            case msgpack_rpc_type::notification:
                expected_size = 3;
                break;
            default:
                PACKIO_ERROR("unexpected type: {}", type);
                return std::nullopt;
            }

            if (call.via.array.size != expected_size) {
                PACKIO_ERROR("unexpected message size: {}", call.via.array.size);
                return std::nullopt;
            }

            std::string name = call.via.array.ptr[idx++].as<std::string>();
            const msgpack::object& args = call.via.array.ptr[idx++];

            return Call{type, id, name, args};
        }
        catch (msgpack::type_error& exc) {
            PACKIO_ERROR("unexpected message content: {}", exc.what());
            (void)exc;
            return std::nullopt;
        }
    }

    void async_send_result(
        id_type id,
        packio::err::error_code ec,
        msgpack::object_handle result_handle)
    {
        // abort R/W on error
        if (!socket_.is_open()) {
            return;
        }

        auto message_ptr = std::make_unique<message_type>();
        msgpack::packer<buffer_type> packer(std::get<buffer_type>(*message_ptr));

        // serialize the result into the buffer
        const auto pack = [&](auto&& error, auto&& result) {
            packer.pack(std::forward_as_tuple(
                static_cast<int>(msgpack_rpc_type::response),
                id,
                std::forward<decltype(error)>(error),
                std::forward<decltype(result)>(result)));
        };

        if (ec) {
            if (result_handle.get().is_nil()) {
                pack(ec.message(), msgpack::type::nil_t{});
            }
            else {
                pack(result_handle.get(), msgpack::type::nil_t{});
            }
        }
        else {
            pack(msgpack::type::nil_t{}, result_handle.get());
        }

        // move the result handle to the message pointer
        // as the buffer is non-owning, we need to keep the result handle
        // with the buffer
        std::get<msgpack::object_handle>(*message_ptr) = std::move(result_handle);
        async_send_message(std::move(message_ptr));
    }

    void async_send_message(std::unique_ptr<message_type> message_ptr)
    {
        wstrand_.push([this,
                       self = shared_from_this(),
                       message_ptr = std::move(message_ptr)]() mutable {
            using internal::buffer;

            auto buf = buffer(std::get<buffer_type>(*message_ptr));
            packio::asio::async_write(
                socket_,
                buf,
                [self = std::move(self), message_ptr = std::move(message_ptr)](
                    packio::err::error_code ec, size_t length) {
                    self->wstrand_.next();

                    if (ec) {
                        PACKIO_WARN("write error: {}", ec.message());
                        self->close_connection();
                        return;
                    }

                    PACKIO_TRACE("write: {}", length);
                    (void)length;
                });
        });
    };

    void close_connection()
    {
        packio::err::error_code ec;
        socket_.close(ec);
        if (ec) {
            PACKIO_WARN("close error: {}", ec.message());
        }
    }

    socket_type socket_;
    std::size_t buffer_reserve_size_{kDefaultBufferReserveSize};
    std::shared_ptr<Dispatcher> dispatcher_ptr_;
    internal::manual_strand<typename socket_type::executor_type> wstrand_;
};

} // packio

#endif // PACKIO_SERVER_SESSION_H
