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

template <typename Protocol, typename Clock = std::chrono::steady_clock>
class client {
public:
    using protocol_type = Protocol;
    using clock_type = Clock;
    using duration_type = typename clock_type::duration;
    using timer_type = boost::asio::basic_waitable_timer<clock_type>;
    using socket_type = typename protocol_type::socket;
    using endpoint_type = typename protocol_type::endpoint;
    using executor_type = typename socket_type::executor_type;
    using async_call_handler_type =
        internal::function<void(boost::system::error_code, const msgpack::object&)>;
    using async_notify_handler_type =
        internal::function<void(boost::system::error_code)>;

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

    void set_timeout(duration_type timeout) { timeout_ = timeout; }
    duration_type get_timeout() const { return timeout_; }

    template <typename Buffer = msgpack::sbuffer, typename... Args>
    void async_notify(
        async_notify_handler_type handler,
        std::string_view name,
        Args&&... args)
    {
        TRACE("async_notify: {}", name);

        auto packer_buf = std::make_shared<Buffer>();
        msgpack::pack(
            *packer_buf,
            std::forward_as_tuple(
                static_cast<int>(msgpack_rpc_type::notification),
                name,
                std::forward_as_tuple(args...)));

        maybe_start_reading();
        boost::asio::async_write(
            socket_,
            internal::buffer_to_asio(*packer_buf),
            [packer_buf, handler = std::move(handler)](
                boost::system::error_code ec, size_t length) {
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

    template <typename Buffer = msgpack::sbuffer, typename... Args>
    void async_call(
        async_call_handler_type handler,
        std::string_view name,
        Args&&... args)
    {
        TRACE("async_call: {}", name);

        auto id = id_.fetch_add(1, std::memory_order_acq_rel);
        auto packer_buf = std::make_shared<Buffer>();
        msgpack::pack(
            *packer_buf,
            std::forward_as_tuple(
                static_cast<int>(msgpack_rpc_type::request),
                id,
                name,
                std::forward_as_tuple(args...)));

        {
            std::unique_lock lock{pending_mutex_};
            timer_type& timer = std::get<timer_type>(
                pending_
                    .emplace(
                        id,
                        std::make_tuple(
                            std::move(handler),
                            timer_type{socket_.get_executor()}))
                    .first->second);

            if (timeout_ > duration_type{0}) {
                TRACE(
                    "timeout in {}us",
                    std::chrono::duration_cast<std::chrono::microseconds>(timeout_)
                        .count());
                async_timeout(timer, id);
            }
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

    void async_timeout(timer_type& timer, uint32_t id)
    {
        timer.expires_after(timeout_);
        timer.async_wait([this, id](boost::system::error_code ec) {
            if (ec == boost::asio::error::operation_aborted) {
                return;
            }

            DEBUG("timeout");

            std::unique_lock lock{pending_mutex_};
            auto pending_it = pending_.find(id);
            if (pending_it == pending_.end()) {
                DEBUG("timeout for unexisting id {}", id);
            }
            else {
                auto tuple = std::move(pending_it->second);
                pending_.erase(pending_it);
                lock.unlock();

                auto& handler = std::get<async_call_handler_type>(tuple);
                ec = make_error_code(error::timeout);
                msgpack::zone zone;
                handler(ec, msgpack::object(ec.message(), zone));
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
        auto pending_it = pending_.find(id);
        if (pending_it == pending_.end()) {
            DEBUG("received response for unexisting id");
        }
        else {
            auto [handler, timer] = std::move(pending_it->second);
            pending_.erase(pending_it);
            lock.unlock();

            timer.cancel();
            handler(ec, result);
        }
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
    std::map<uint32_t, std::tuple<async_call_handler_type, timer_type>> pending_;
    duration_type timeout_{0};
    std::atomic<uint32_t> id_{0};
    std::atomic_flag reading_;
};

} // packio

#endif // PACKIO_CLIENT_H