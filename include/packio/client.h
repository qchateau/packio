// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef PACKIO_CLIENT_H
#define PACKIO_CLIENT_H

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <queue>
#include <string_view>

#include <boost/asio.hpp>
#include <msgpack.hpp>

#include "error_code.h"
#include "internal/manual_strand.h"
#include "internal/msgpack_rpc.h"
#include "internal/utils.h"

namespace packio {

template <typename Protocol, template <class...> class Map = std::map>
class client : public std::enable_shared_from_this<client<Protocol, Map>> {
public:
    using protocol_type = Protocol;
    using socket_type = typename protocol_type::socket;
    using endpoint_type = typename protocol_type::endpoint;
    using executor_type = typename socket_type::executor_type;
    using async_call_handler_type =
        std::function<void(boost::system::error_code, msgpack::object_handle)>;
    using std::enable_shared_from_this<client<Protocol, Map>>::shared_from_this;

    static constexpr size_t kDefaultBufferReserveSize = 4096;

    explicit client(socket_type socket)
        : socket_{std::move(socket)},
          wstrand_{socket_.get_executor()},
          call_strand_{socket_.get_executor()}
    {
    }

    socket_type& socket() { return socket_; }
    const socket_type& socket() const { return socket_; }

    void set_buffer_reserve_size(std::size_t size) noexcept
    {
        buffer_reserve_size_ = size;
    }
    std::size_t get_buffer_reserve_size() const noexcept
    {
        return buffer_reserve_size_;
    }

    executor_type get_executor() { return socket().get_executor(); }

    void cancel(id_type id)
    {
        boost::asio::dispatch(call_strand_, [this, self = shared_from_this(), id] {
            auto ec = make_error_code(error::cancelled);
            async_call_handler(
                id, internal::make_msgpack_object(ec.message()), ec);
            maybe_stop_reading();
        });
    }

    void cancel()
    {
        boost::asio::dispatch(call_strand_, [this, self = shared_from_this()] {
            assert(call_strand_.running_in_this_thread());
            auto ec = make_error_code(error::cancelled);
            while (!pending_.empty()) {
                async_call_handler(
                    pending_.begin()->first,
                    internal::make_msgpack_object(ec.message()),
                    ec);
            }
            maybe_stop_reading();
        });
    }

    template <typename Buffer = msgpack::sbuffer, typename NotifyHandler>
    void async_notify(std::string_view name, NotifyHandler&& handler)
    {
        return async_notify<Buffer>(
            name, std::tuple<>{}, std::forward<NotifyHandler>(handler));
    }

    template <typename Buffer = msgpack::sbuffer, typename NotifyHandler, typename... Args>
    void async_notify(
        std::string_view name,
        std::tuple<Args...> args,
        NotifyHandler&& handler)
    {
        DEBUG("async_notify: {}", name);

        auto packer_buf = std::make_unique<Buffer>();
        msgpack::pack(
            *packer_buf,
            std::forward_as_tuple(
                static_cast<int>(msgpack_rpc_type::notification), name, args));

        async_send(
            std::move(packer_buf),
            [handler = std::forward<NotifyHandler>(handler)](
                boost::system::error_code ec, std::size_t length) mutable {
                if (ec) {
                    WARN("write error: {}", ec.message());
                }
                else {
                    TRACE("write: {}", length);
                    (void)length;
                }

                handler(ec);
            });
    }

    template <typename Buffer = msgpack::sbuffer, typename CallHandler>
    id_type async_call(std::string_view name, CallHandler&& handler)
    {
        return async_call<Buffer>(
            name, std::tuple<>{}, std::forward<CallHandler>(handler));
    }

    template <typename Buffer = msgpack::sbuffer, typename CallHandler, typename... Args>
    id_type async_call(
        std::string_view name,
        std::tuple<Args...> args,
        CallHandler&& handler)
    {
        DEBUG("async_call: {}", name);

        auto id = id_.fetch_add(1, std::memory_order_acq_rel);
        auto packer_buf = std::make_unique<Buffer>();
        msgpack::pack(
            *packer_buf,
            std::forward_as_tuple(
                static_cast<int>(msgpack_rpc_type::request), id, name, args));

        boost::asio::dispatch(
            call_strand_,
            [this,
             self = shared_from_this(),
             id,
             handler = std::forward<CallHandler>(handler),
             packer_buf = std::move(packer_buf)]() mutable {
                // we must emplace the id and handler before sending data
                // otherwise we might drop a fast response
                assert(call_strand_.running_in_this_thread());
                pending_.try_emplace(
                    id,
                    internal::make_copyable_function(
                        std::forward<CallHandler>(handler)));

                // if we are not reading, start the read operation
                if (!reading_) {
                    DEBUG("start reading");
                    async_read(std::make_unique<msgpack::unpacker>());
                }

                // send the request buffer
                async_send(
                    std::move(packer_buf),
                    [this, self = std::move(self), id](
                        boost::system::error_code ec, std::size_t length) mutable {
                        if (ec) {
                            WARN("write error: {}", ec.message());
                            async_call_handler(
                                id,
                                internal::make_msgpack_object(ec.message()),
                                ec);
                        }
                        else {
                            TRACE("write: {}", length);
                            (void)length;
                        }
                    });
            });

        return id;
    }

private:
    void maybe_stop_reading()
    {
        assert(call_strand_.running_in_this_thread());
        if (reading_ && pending_.empty()) {
            DEBUG("stop reading");
            boost::system::error_code ec;
            socket_.cancel(ec);
            if (ec) {
                WARN("cancel failed: {}", ec.message());
            }
        }
    }

    template <typename Buffer, typename WriteHandler>
    void async_send(std::unique_ptr<Buffer> buffer_ptr, WriteHandler&& handler)
    {
        wstrand_.push(internal::make_copyable_function(
            [this,
             self = shared_from_this(),
             buffer_ptr = std::move(buffer_ptr),
             handler = std::forward<WriteHandler>(handler)]() mutable {
                internal::set_no_delay(socket_);
                auto buffer = internal::buffer_to_asio(*buffer_ptr);
                boost::asio::async_write(
                    socket_,
                    buffer,
                    [this,
                     self = std::move(self),
                     buffer_ptr = std::move(buffer_ptr),
                     handler = std::forward<WriteHandler>(handler)](
                        boost::system::error_code ec, size_t length) mutable {
                        wstrand_.next();
                        handler(ec, length);
                    });
            }));
    }

    void async_read(std::unique_ptr<msgpack::unpacker> unpacker)
    {
        unpacker->reserve_buffer(buffer_reserve_size_);
        auto buffer = boost::asio::buffer(
            unpacker->buffer(), unpacker->buffer_capacity());

        assert(call_strand_.running_in_this_thread());
        reading_ = true;
        TRACE("reading ... {} call(s) pending", pending_.size());
        socket_.async_read_some(
            buffer,
            boost::asio::bind_executor(
                call_strand_,
                [this, self = shared_from_this(), unpacker = std::move(unpacker)](
                    boost::system::error_code ec, size_t length) mutable {
                    TRACE("read: {}", length);
                    unpacker->buffer_consumed(length);

                    msgpack::object_handle response;
                    while (unpacker->next(response)) {
                        process_response(std::move(response), ec);
                    }

                    // stop if there is an error or there is no more pending calls
                    assert(call_strand_.running_in_this_thread());

                    if (ec && ec != boost::asio::error::operation_aborted) {
                        WARN("read error: {}", ec.message());
                        reading_ = false;
                        return;
                    }

                    if (pending_.empty()) {
                        TRACE("done reading, no more pending calls");
                        reading_ = false;
                        return;
                    }

                    async_read(std::move(unpacker));
                }));
    }

    void process_response(msgpack::object_handle response, boost::system::error_code ec)
    {
        if (!verify_reponse(response.get())) {
            ERROR("received unexpected response");
            return;
        }

        const auto& call_response = response->via.array.ptr;
        int id = call_response[1].as<int>();
        msgpack::object err = call_response[2];
        msgpack::object result = call_response[3];

        if (err.type != msgpack::type::NIL) {
            ec = make_error_code(error::call_error);
            async_call_handler(id, {err, std::move(response.zone())}, ec);
        }
        else {
            ec = make_error_code(error::success);
            async_call_handler(id, {result, std::move(response.zone())}, ec);
        }
    }

    void async_call_handler(
        id_type id,
        msgpack::object_handle result,
        boost::system::error_code ec)
    {
        boost::asio::dispatch(
            call_strand_, [this, ec, id, result = std::move(result)]() mutable {
                DEBUG("calling handler for id: {}", id);

                assert(call_strand_.running_in_this_thread());
                auto it = pending_.find(id);
                if (it == pending_.end()) {
                    WARN("unexisting id");
                    return;
                }

                auto handler = std::move(it->second);
                pending_.erase(it);

                // handle the response asynchronously (post)
                // to schedule the next read immediately
                // this will allow parallel response handling
                // in multi-threaded environments
                boost::asio::post(
                    socket_.get_executor(),
                    [ec,
                     handler = std::move(handler),
                     result = std::move(result)]() mutable {
                        handler(ec, std::move(result));
                    });
            });
    }

    bool verify_reponse(const msgpack::object& response)
    {
        if (response.type != msgpack::type::ARRAY) {
            ERROR("unexpected message type: {}", response.type);
            return false;
        }
        if (response.via.array.size != 4) {
            ERROR("unexpected message size: {}", response.via.array.size);
            return false;
        }
        int type = response.via.array.ptr[0].as<int>();
        if (type != static_cast<int>(msgpack_rpc_type::response)) {
            ERROR("unexpected type: {}", type);
            return false;
        }
        return true;
    }

    socket_type socket_;
    std::size_t buffer_reserve_size_{kDefaultBufferReserveSize};
    std::atomic<id_type> id_{0};

    internal::manual_strand<executor_type> wstrand_;

    boost::asio::strand<executor_type> call_strand_;
    Map<id_type, async_call_handler_type> pending_;
    bool reading_{false};
};

} // packio

#endif // PACKIO_CLIENT_H