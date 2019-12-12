#ifndef RPCPACK_SERVER_SESSION_H
#define RPCPACK_SERVER_SESSION_H

#include <memory>
#include <boost/asio.hpp>
#include <msgpack.hpp>

#include "../error_code.h"
#include "log.h"
#include "msgpack_rpc.h"
#include "utils.h"

namespace rpcpack {
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

    void start() { async_read(std::make_shared<msgpack::unpacker>()); }

private:
    void async_read(std::shared_ptr<msgpack::unpacker> unpacker)
    {
        // abort R/W on error
        if (error_.load()) {
            return;
        }

        auto self{shared_from_this()};
        unpacker->reserve_buffer(kBufferReserveSize);

        socket_.async_read_some(
            boost::asio::buffer(unpacker->buffer(), unpacker->buffer_capacity()),
            [this, self, unpacker](boost::system::error_code ec, size_t length) {
                if (ec) {
                    DEBUG("error: {}", ec.message());
                    error_.store(true);
                    return;
                }

                TRACE("read: {}", length);
                unpacker->buffer_consumed(length);

                for (msgpack::object_handle call; unpacker->next(call);) {
                    async_dispatch(std::move(call));
                }

                async_read(std::move(unpacker));
            });
    }

    void async_dispatch(msgpack::object_handle call)
    {
        auto self{shared_from_this()};
        auto call_ptr = std::make_shared<msgpack::object_handle>(std::move(call));
        boost::asio::post(socket_.get_executor(), [this, self, call_ptr] {
            dispatch(std::move(*call_ptr));
        });
    }

    void dispatch(msgpack::object_handle call_handle)
    {
        const msgpack::object& call = call_handle.get();
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
        boost::system::error_code ec;
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

            const auto completion_handler = [this, type, id](
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
        auto packer_buf = std::make_shared<msgpack::vrefbuffer>();
        msgpack::packer<msgpack::vrefbuffer> packer(*packer_buf);

        if (ec) {
            packer.pack(std::forward_as_tuple(
                static_cast<int>(msgpack_rpc_type::response),
                id,
                ec.message(),
                msgpack::type::nil_t{}));
        }
        else {
            packer.pack(std::forward_as_tuple(
                static_cast<int>(msgpack_rpc_type::response),
                id,
                msgpack::type::nil_t{},
                result_handle.get()));
        }

        auto buffer = buffer_to_asio(*packer_buf);

        boost::asio::async_write(
            socket_,
            buffer,
            [this, self, packer_buf, result_handle = std::move(result_handle)](
                boost::system::error_code ec, size_t length) {
                if (ec) {
                    DEBUG("error: {}", ec.message());
                    error_.store(true);
                    return;
                }

                TRACE("write: {}", length);
            });
    }

    socket_type socket_;
    std::shared_ptr<Dispatcher> dispatcher_ptr_;
    std::atomic<bool> error_{false};
};

} // internal
} // rpcpack

#endif // RPCPACK_SERVER_SESSION_H
