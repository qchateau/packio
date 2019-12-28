// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef PACKIO_SERVER_SESSION_H
#define PACKIO_SERVER_SESSION_H

#include <memory>
#include <boost/asio.hpp>
#include <msgpack.hpp>

#include "../error_code.h"
#include "log.h"
#include "msgpack_rpc.h"
#include "utils.h"

namespace packio {
namespace internal {

template <typename Protocol, typename Dispatcher>
class server_session
    : public std::enable_shared_from_this<server_session<Protocol, Dispatcher>> {
public:
    using protocol_type = Protocol;
    using socket_type = typename protocol_type::socket;
    using std::enable_shared_from_this<server_session<Protocol, Dispatcher>>::shared_from_this;

    static constexpr size_t kBufferReserveSize = 4096;

    server_session(socket_type sock, std::shared_ptr<Dispatcher> dispatcher_ptr)
        : socket_{std::move(sock)}, dispatcher_ptr_{std::move(dispatcher_ptr)}
    {
        DEBUG("starting session {:p}", fmt::ptr(this));
    }

    ~server_session()
    {
        boost::system::error_code ec;
        socket_.cancel(ec);
        if (ec) {
            INFO("cancel failed: {}", ec.message());
        }
        DEBUG("stopped session {:p}", fmt::ptr(this));
    }

    socket_type& socket() { return socket_; }
    const socket_type& socket() const { return socket_; }

    void start() { async_read(std::make_unique<msgpack::unpacker>()); }

private:
    void async_read(std::unique_ptr<msgpack::unpacker> unpacker)
    {
        // abort R/W on error
        if (error_.load()) {
            return;
        }

        auto self{shared_from_this()};
        unpacker->reserve_buffer(kBufferReserveSize);

        auto buffer = boost::asio::buffer(
            unpacker->buffer(), unpacker->buffer_capacity());
        socket_.async_read_some(
            buffer,
            [this, self, unpacker = std::move(unpacker)](
                boost::system::error_code ec, size_t length) mutable {
                if (ec) {
                    DEBUG("error: {}", ec.message());
                    error_.store(true);
                    return;
                }

                TRACE("read: {}", length);
                unpacker->buffer_consumed(length);

                for (msgpack::object_handle call; unpacker->next(call);) {
                    // handle the call asynchronously (post)
                    // to schedule the next read immediately
                    // this will allow parallel call handling
                    // in multi-threaded environments
                    async_dispatch(std::move(call));
                }

                async_read(std::move(unpacker));
            });
    }

    void async_dispatch(msgpack::object_handle call)
    {
        auto self{shared_from_this()};
        boost::asio::post(
            socket_.get_executor(),
            [this, self, call = std::move(call)] { dispatch(call.get()); });
    }

    void dispatch(const msgpack::object& call)
    {
        if (call.type != msgpack::type::ARRAY) {
            WARN("unexpected message type: {}", call.type);
            error_.store(true);
            return;
        }
        if (call.via.array.size < 3 || call.via.array.size > 4) {
            WARN("unexpected message size: {}", call.via.array.size);
            error_.store(true);
            return;
        }

        int idx = 0;
        uint32_t id = 0;
        int type = call.via.array.ptr[idx++].as<int>();

        switch (static_cast<msgpack_rpc_type>(type)) {
        default:
            WARN("unexpected type: {}", type);
            error_.store(true);
            return;
        case msgpack_rpc_type::request:
            id = call.via.array.ptr[idx++].as<uint32_t>();
            [[fallthrough]];
        case msgpack_rpc_type::notification:
            std::string name = call.via.array.ptr[idx++].as<std::string>();
            const msgpack::object& args = call.via.array.ptr[idx++];
            if (args.type != msgpack::type::ARRAY) {
                WARN("unexpected arguments type: {}", type);
                error_.store(true);
                return;
            }

            auto completion_handler = [this, type, id, self = shared_from_this()](
                                          boost::system::error_code ec,
                                          msgpack::object_handle result) {
                if (type == static_cast<int>(msgpack_rpc_type::request)) {
                    TRACE("result: {}", ec.message());
                    async_write(id, ec, std::move(result));
                }
            };

            const auto function = dispatcher_ptr_->get(name);
            if (function) {
                TRACE("call: {} (id={})", name, id);
                (*function)(completion_handler, args);
            }
            else {
                DEBUG("unknown function {}", name);
                completion_handler(make_error_code(error::unknown_function), {});
            }
        }
    }

    void async_write(
        uint32_t id,
        boost::system::error_code ec,
        msgpack::object_handle result_handle)
    {
        // abort R/W on error
        if (error_.load()) {
            return;
        }

        auto self(shared_from_this());
        auto packer_buf = std::make_unique<msgpack::vrefbuffer>();
        msgpack::packer<msgpack::vrefbuffer> packer(*packer_buf);

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

        auto buffer = buffer_to_asio(*packer_buf);
        boost::asio::async_write(
            socket_,
            buffer,
            [this,
             self,
             packer_buf = std::move(packer_buf),
             result_handle = std::move(result_handle)](
                boost::system::error_code ec, size_t length) {
                if (ec) {
                    DEBUG("error: {}", ec.message());
                    error_.store(true);
                    return;
                }

                TRACE("write: {}", length);
                (void)length;
            });
    }

    socket_type socket_;
    std::shared_ptr<Dispatcher> dispatcher_ptr_;
    std::atomic<bool> error_{false};
};

} // internal
} // packio

#endif // PACKIO_SERVER_SESSION_H
