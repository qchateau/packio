#ifndef PACKIO_CLIENT_H
#define PACKIO_CLIENT_H

#include <atomic>
#include <chrono>
#include <mutex>
#include <string_view>

#include <boost/asio.hpp>
#include <msgpack.hpp>

#include "error_code.h"
#include "internal/msgpack_rpc.h"
#include "internal/utils.h"

namespace packio {

template <typename Protocol>
class client {
public:
    using protocol_type = Protocol;
    using socket_type = typename protocol_type::socket;
    using endpoint_type = typename protocol_type::endpoint;
    using executor_type = typename socket_type::executor_type;
    using id_type = uint32_t;
    using async_call_handler_type =
        std::function<void(boost::system::error_code, const msgpack::object&)>;

    static constexpr size_t kBufferReserveSize = 4096;

    explicit client(socket_type socket) : socket_{std::move(socket)}
    {
        reading_.clear();
    }

    ~client()
    {
        boost::system::error_code ec;
        socket_.cancel(ec);
        if (ec) {
            INFO("cancel failed: {}", ec.message());
        }
        DEBUG("stopped client");
    }

    socket_type& socket() { return socket_; }
    const socket_type& socket() const { return socket_; }

    executor_type get_executor() { return socket().get_executor(); }

    std::size_t cancel(id_type id)
    {
        std::unique_lock lock{pending_mutex_};
        auto it = pending_.find(id);
        if (it == pending_.end()) {
            return 0;
        }

        auto handler = std::move(it->second);
        pending_.erase(it);
        lock.unlock();

        auto ec = make_error_code(error::cancelled);
        msgpack::zone zone;
        handler(ec, msgpack::object(ec.message(), zone));
        return 1;
    }

    std::size_t cancel()
    {
        decltype(pending_) pending;
        {
            std::unique_lock lock{pending_mutex_};
            std::swap(pending, pending_);
        }

        auto ec = make_error_code(error::cancelled);
        msgpack::zone zone;
        for (auto& pair : pending) {
            pair.second(ec, msgpack::object(ec.message(), zone));
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
        TRACE("async_notify: {}", name);

        auto packer_buf = std::make_shared<Buffer>();
        msgpack::pack(
            *packer_buf,
            std::forward_as_tuple(
                static_cast<int>(msgpack_rpc_type::notification), name, args));

        maybe_start_reading();
        boost::asio::async_write(
            socket_,
            internal::buffer_to_asio(*packer_buf),
            [packer_buf, handler = std::forward<NotifyHandler>(handler)](
                boost::system::error_code ec, size_t length) mutable {
                if (ec) {
                    DEBUG("write error: {}", ec.message());
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
        TRACE("async_call: {}", name);

        auto id = id_.fetch_add(1, std::memory_order_acq_rel);
        auto packer_buf = std::make_shared<Buffer>();
        msgpack::pack(
            *packer_buf,
            std::forward_as_tuple(
                static_cast<int>(msgpack_rpc_type::request), id, name, args));

        {
            std::unique_lock lock{pending_mutex_};
            pending_.try_emplace(
                id,
                internal::make_copyable_function(
                    std::forward<CallHandler>(handler)));
        }

        maybe_start_reading();
        boost::asio::async_write(
            socket_,
            internal::buffer_to_asio(*packer_buf),
            [this, id, packer_buf](boost::system::error_code ec, size_t length) {
                if (ec) {
                    DEBUG("write error: {}", ec.message());
                    msgpack::zone zone;
                    maybe_call_handler(
                        id, msgpack::object(ec.message(), zone), ec);
                }
                else {
                    TRACE("write: {}", length);
                    (void)length;
                }
            });

        return id;
    }

private:
    void maybe_start_reading()
    {
        if (!reading_.test_and_set(std::memory_order_acq_rel)) {
            internal::set_no_delay(socket_);
            async_read();
        }
    }

    void async_read()
    {
        unpacker_.reserve_buffer(kBufferReserveSize);

        socket_.async_read_some(
            boost::asio::buffer(unpacker_.buffer(), unpacker_.buffer_capacity()),
            [this](boost::system::error_code ec, size_t length) {
                if (ec) {
                    DEBUG("read error: {}", ec.message());
                }
                else {
                    TRACE("read: {}", length);
                    unpacker_.buffer_consumed(length);

                    for (msgpack::object_handle response;
                         unpacker_.next(response);) {
                        TRACE("dispatching");
                        async_dispatch(std::move(response), ec);
                    }

                    async_read();
                }
            });
    }

    void async_dispatch(msgpack::object_handle response, boost::system::error_code ec)
    {
        auto response_ptr = std::make_shared<msgpack::object_handle>(
            std::move(response));
        boost::asio::dispatch(socket_.get_executor(), [this, response_ptr, ec] {
            dispatch(response_ptr->get(), ec);
        });
    }

    void dispatch(const msgpack::object& response, boost::system::error_code ec)
    {
        if (!verify_reponse(response)) {
            DEBUG("received unexpected response");
            close_connection();
            return;
        }

        int id = response.via.array.ptr[1].as<int>();
        const msgpack::object& err = response.via.array.ptr[2];
        const msgpack::object& result = response.via.array.ptr[3];

        if (err.type != msgpack::type::NIL) {
            ec = make_error_code(error::call_error);
            maybe_call_handler(id, err, ec);
        }
        else {
            ec = make_error_code(error::success);
            maybe_call_handler(id, result, ec);
        }
    }

    void maybe_call_handler(
        int id,
        const msgpack::object& result,
        boost::system::error_code& ec)
    {
        TRACE("processing response to id: {}", id);

        std::unique_lock lock{pending_mutex_};
        auto it = pending_.find(id);
        if (it == pending_.end()) {
            DEBUG("received response for unexisting id");
            return;
        }

        auto handler = std::move(it->second);
        pending_.erase(it);
        lock.unlock();

        handler(ec, result);
    }

    bool verify_reponse(const msgpack::object& response)
    {
        if (response.type != msgpack::type::ARRAY) {
            WARN("unexpected message type: {}", response.type);
            return false;
        }
        if (response.via.array.size != 4) {
            WARN("unexpected message size: {}", response.via.array.size);
            return false;
        }
        int type = response.via.array.ptr[0].as<int>();
        if (type != static_cast<int>(msgpack_rpc_type::response)) {
            WARN("unexpected type: {}", type);
            return false;
        }
        return true;
    }

    void timeout_handler(const boost::system::error_code& ec)
    {
        if (ec == boost::asio::error::operation_aborted) {
            return;
        }

        DEBUG("timeout");
        close_connection();
    }

    void close_connection()
    {
        boost::system::error_code ec;
        socket_.cancel(ec);
        if (ec) {
            INFO("cancel failed: {}", ec.message());
        }
        socket_.shutdown(socket_type::shutdown_type::shutdown_both, ec);
        if (ec) {
            INFO("shutdown failed: {}", ec.message());
        }
        socket_.close(ec);
        if (ec) {
            INFO("close failed: {}", ec.message());
        }
    }

    socket_type socket_;
    msgpack::unpacker unpacker_;
    std::mutex pending_mutex_;
    std::map<id_type, async_call_handler_type> pending_;
    std::atomic<id_type> id_{0};
    std::atomic_flag reading_;
};

} // packio

#endif // PACKIO_CLIENT_H