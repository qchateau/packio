// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef PACKIO_CLIENT_H
#define PACKIO_CLIENT_H

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <queue>
#include <string_view>

#include <boost/asio.hpp>
#include <msgpack.hpp>

#include "error_code.h"
#include "internal/manual_strand.h"
#include "internal/msgpack_rpc.h"
#include "internal/utils.h"

namespace packio {

template <typename Protocol, template <class...> class Map = std::map, typename Mutex = std::mutex>
class client {
public:
    using protocol_type = Protocol;
    using socket_type = typename protocol_type::socket;
    using endpoint_type = typename protocol_type::endpoint;
    using executor_type = typename socket_type::executor_type;
    using async_call_handler_type =
        std::function<void(boost::system::error_code, msgpack::object_handle)>;

    static constexpr size_t kDefaultBufferReserveSize = 4096;

    explicit client(socket_type socket)
        : socket_{std::move(socket)}, wstrand_{socket_.get_executor()}
    {
    }

    ~client()
    {
        boost::system::error_code ec;
        socket_.cancel(ec);
        if (ec) {
            WARN("cancel failed: {}", ec.message());
        }
        INFO("stopped client");
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

    std::size_t cancel(id_type id)
    {
        std::unique_lock lock{mutex_};
        auto it = pending_.find(id);
        if (it == pending_.end()) {
            return 0;
        }

        auto handler = std::move(it->second);
        pending_.erase(it);
        lock.unlock();

        boost::asio::post(socket_.get_executor(), [handler = std::move(handler)] {
            auto ec = make_error_code(error::cancelled);
            handler(ec, internal::make_msgpack_object(ec.message()));
        });
        return 1;
    }

    std::size_t cancel()
    {
        decltype(pending_) pending;
        {
            std::unique_lock lock{mutex_};
            std::swap(pending, pending_);
        }

        for (auto& pair : pending) {
            boost::asio::post(
                socket_.get_executor(), [handler = std::move(pair.second)] {
                    auto ec = make_error_code(error::cancelled);
                    handler(ec, internal::make_msgpack_object(ec.message()));
                });
        }

        return pending.size();
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

        {
            std::unique_lock lock{mutex_};
            pending_.try_emplace(
                id,
                internal::make_copyable_function(
                    std::forward<CallHandler>(handler)));
            start_reading();
        }

        async_send(
            std::move(packer_buf),
            [this, id](boost::system::error_code ec, std::size_t length) {
                if (ec) {
                    WARN("write error: {}", ec.message());
                    call_handler(
                        id, internal::make_msgpack_object(ec.message()), ec);
                }
                else {
                    TRACE("write: {}", length);
                    (void)length;
                }
            });

        return id;
    }

private:
    template <typename Buffer, typename WriteHandler>
    void async_send(std::unique_ptr<Buffer> buffer_ptr, WriteHandler&& handler)
    {
        wstrand_.push(internal::make_copyable_function(
            [this,
             buffer_ptr = std::move(buffer_ptr),
             handler = std::forward<WriteHandler>(handler)]() mutable {
                auto buffer = internal::buffer_to_asio(*buffer_ptr);
                boost::asio::async_write(
                    socket_,
                    buffer,
                    [this,
                     buffer_ptr = std::move(buffer_ptr),
                     handler = std::forward<WriteHandler>(handler)](
                        boost::system::error_code ec, size_t length) mutable {
                        wstrand_.next();
                        handler(ec, length);
                    });
            }));
    }

    void start_reading()
    {
        if (reading_) {
            return;
        }
        reading_ = true;

        internal::set_no_delay(socket_);
        async_read();
    }

    void async_read()
    {
        unpacker_.reserve_buffer(buffer_reserve_size_);

        socket_.async_read_some(
            boost::asio::buffer(unpacker_.buffer(), unpacker_.buffer_capacity()),
            [this](boost::system::error_code ec, size_t length) {
                if (ec) {
                    WARN("read error: {}", ec.message());
                    return;
                }

                TRACE("read: {}", length);
                unpacker_.buffer_consumed(length);

                for (msgpack::object_handle response; unpacker_.next(response);) {
                    TRACE("dispatching");
                    // handle the response asynchronously (post)
                    // to schedule the next read immediately
                    // this will allow parallel response handling
                    // in multi-threaded environments
                    async_dispatch(std::move(response), ec);
                }

                async_read();
            });
    }

    void async_dispatch(msgpack::object_handle response, boost::system::error_code ec)
    {
        boost::asio::post(
            socket_.get_executor(),
            [this, ec, response = std::move(response)]() mutable {
                dispatch(std::move(response), ec);
            });
    }

    void dispatch(msgpack::object_handle response, boost::system::error_code ec)
    {
        if (!verify_reponse(response.get())) {
            ERROR("received unexpected response");
            close_connection();
            return;
        }

        const auto& call_response = response->via.array.ptr;
        int id = call_response[1].as<int>();
        msgpack::object err = call_response[2];
        msgpack::object result = call_response[3];

        if (err.type != msgpack::type::NIL) {
            ec = make_error_code(error::call_error);
            call_handler(id, {err, std::move(response.zone())}, ec);
        }
        else {
            ec = make_error_code(error::success);
            call_handler(id, {result, std::move(response.zone())}, ec);
        }
    }

    void call_handler(
        id_type id,
        msgpack::object_handle result,
        boost::system::error_code ec)
    {
        DEBUG("processing response to id: {}", id);

        std::unique_lock lock{mutex_};
        auto it = pending_.find(id);
        if (it == pending_.end()) {
            WARN("received response for unexisting id");
            return;
        }

        auto handler = std::move(it->second);
        pending_.erase(it);
        lock.unlock();

        handler(ec, std::move(result));
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

    void close_connection()
    {
        boost::system::error_code ec;
        socket_.cancel(ec);
        if (ec) {
            WARN("cancel failed: {}", ec.message());
        }
        socket_.shutdown(socket_type::shutdown_type::shutdown_both, ec);
        if (ec) {
            WARN("shutdown failed: {}", ec.message());
        }
        socket_.close(ec);
        if (ec) {
            WARN("close failed: {}", ec.message());
        }
    }

    socket_type socket_;
    msgpack::unpacker unpacker_;
    std::size_t buffer_reserve_size_{kDefaultBufferReserveSize};
    std::atomic<id_type> id_{0};

    internal::manual_strand<executor_type> wstrand_;

    Mutex mutex_;
    Map<id_type, async_call_handler_type> pending_;
    bool reading_{false};
};

} // packio

#endif // PACKIO_CLIENT_H