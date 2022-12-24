// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef PACKIO_CLIENT_H
#define PACKIO_CLIENT_H

//! @file
//! Class @ref packio::client "client"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <queue>
#include <string_view>
#include <type_traits>

#include "internal/config.h"
#include "internal/manual_strand.h"
#include "internal/movable_function.h"
#include "internal/rpc.h"
#include "internal/utils.h"
#include "traits.h"

namespace packio {

//! The client class
//! @tparam Rpc RPC protocol implementation
//! @tparam Socket Socket type to use for this client
//! @tparam Map Container used to associate call IDs and handlers
template <typename Rpc, typename Socket, template <class...> class Map = default_map>
class client : public std::enable_shared_from_this<client<Rpc, Socket, Map>> {
public:
    //! The RPC protocol type
    using rpc_type = Rpc;
    //! The call ID type
    using id_type = typename rpc_type::id_type;
    //! The response of a RPC call
    using response_type = typename rpc_type::response_type;
    //! The socket type
    using socket_type = Socket;
    //! The protocol type
    using protocol_type = typename socket_type::protocol_type;
    //! The executor type
    using executor_type = typename socket_type::executor_type;

    using std::enable_shared_from_this<client<Rpc, Socket, Map>>::shared_from_this;

    //! The default size reserved by the reception buffer
    static constexpr size_t kDefaultBufferReserveSize = 4096;

    //! The constructor
    //! @param socket The socket which the client will use. Can be connected or not
    explicit client(socket_type socket)
        : socket_{std::move(socket)}, strand_{socket_.get_executor()}, wstrand_{strand_}
    {
    }

    //! Get the underlying socket
    socket_type& socket() noexcept { return socket_; }

    //! Get the underlying socket, const
    const socket_type& socket() const noexcept { return socket_; }

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

    //! Get the executor associated with the object
    executor_type get_executor() { return socket().get_executor(); }

    //! Cancel a pending call
    //!
    //! The associated handler will be called with net::error::operation_aborted
    //! @param id The call ID of the call to cancel
    void cancel(id_type id)
    {
        PACKIO_TRACE("cancel {}", rpc_type::format_id(id));
        net::dispatch(strand_, [self = shared_from_this(), id] {
            auto ec = make_error_code(net::error::operation_aborted);
            self->async_call_handler(id, ec, {});
            self->maybe_cancel_reading();
        });
    }

    //! Cancel all pending calls
    //!
    //! The associated handlers will be called with net::error::operation_aborted
    void cancel()
    {
        PACKIO_TRACE("cancel all");
        net::dispatch(strand_, [self = shared_from_this()] {
            auto ec = make_error_code(net::error::operation_aborted);
            while (!self->pending_.empty()) {
                self->async_call_handler(self->pending_.begin()->first, ec, {});
            }
            self->maybe_cancel_reading();
        });
    }

    //! Send a notify request to the server with argument
    //!
    //! A notify request will call the remote procedure but
    //! does not expect a response
    //! @param name Remote procedure name to call
    //! @param args Tuple of arguments to pass to the remote procedure
    //! @param handler Handler called after the notify request is sent.
    //! Must satisfy the @ref traits::NotifyHandler trait
    template <
        PACKIO_COMPLETION_TOKEN_FOR(void(error_code))
            NotifyHandler PACKIO_DEFAULT_COMPLETION_TOKEN_TYPE(executor_type),
        typename ArgsTuple,
        typename = std::enable_if_t<internal::is_tuple_v<ArgsTuple>>>
    auto async_notify(
        std::string_view name,
        ArgsTuple&& args,
        NotifyHandler&& handler PACKIO_DEFAULT_COMPLETION_TOKEN(executor_type))
    {
        return net::async_initiate<NotifyHandler, void(error_code)>(
            initiate_async_notify(this),
            handler,
            name,
            std::forward<ArgsTuple>(args));
    }

    //! @overload
    template <
        PACKIO_COMPLETION_TOKEN_FOR(void(error_code))
            NotifyHandler PACKIO_DEFAULT_COMPLETION_TOKEN_TYPE(executor_type),
        typename = std::enable_if_t<!internal::is_tuple_v<NotifyHandler>>>
    auto async_notify(
        std::string_view name,
        NotifyHandler&& handler PACKIO_DEFAULT_COMPLETION_TOKEN(executor_type))
    {
        return async_notify(
            name, std::tuple{}, std::forward<NotifyHandler>(handler));
    }

    //! Call a remote procedure
    //!
    //! @param name Remote procedure name to call
    //! @param args Tuple of arguments to pass to the remote procedure
    //! @param handler Handler called with the return value
    //! Must satisfy the @ref traits::CallHandler trait
    //! @param call_id Output parameter that will receive the call ID
    template <
        PACKIO_COMPLETION_TOKEN_FOR(void(error_code, response_type))
            CallHandler PACKIO_DEFAULT_COMPLETION_TOKEN_TYPE(executor_type),
        typename ArgsTuple,
        typename = std::enable_if_t<internal::is_tuple_v<ArgsTuple>>>
    auto async_call(
        std::string_view name,
        ArgsTuple&& args,
        CallHandler&& handler PACKIO_DEFAULT_COMPLETION_TOKEN(executor_type),
        std::optional<std::reference_wrapper<id_type>> call_id = std::nullopt)
    {
        return net::async_initiate<CallHandler, void(error_code, response_type)>(
            initiate_async_call(this),
            handler,
            name,
            std::forward<ArgsTuple>(args),
            call_id);
    }

    //! @overload
    template <
        PACKIO_COMPLETION_TOKEN_FOR(void(error_code, response_type))
            CallHandler PACKIO_DEFAULT_COMPLETION_TOKEN_TYPE(executor_type),
        typename = std::enable_if_t<!internal::is_tuple_v<CallHandler>>>
    auto async_call(
        std::string_view name,
        CallHandler&& handler PACKIO_DEFAULT_COMPLETION_TOKEN(executor_type),
        std::optional<std::reference_wrapper<id_type>> call_id = std::nullopt)
    {
        return async_call(
            name, std::tuple{}, std::forward<CallHandler>(handler), call_id);
    }

private:
    using parser_type = typename rpc_type::incremental_parser_type;
    using async_call_handler_type =
        internal::movable_function<void(error_code, response_type)>;

    void close()
    {
        net::dispatch(strand_, [self = shared_from_this()] {
            auto ec = make_error_code(net::error::operation_aborted);
            while (!self->pending_.empty()) {
                self->async_call_handler(self->pending_.begin()->first, ec, {});
            }
            self->socket_.close(ec);
            if (ec) {
                PACKIO_WARN("close failed: {}", ec.message());
            }
        });
    }

    void maybe_cancel_reading()
    {
        assert(strand_.running_in_this_thread());
        if (reading_ && pending_.empty()) {
            PACKIO_DEBUG("stop reading");
            error_code ec;
            socket_.cancel(ec);
            if (ec) {
                PACKIO_WARN("cancel failed: {}", ec.message());
            }
        }
    }

    template <typename Buffer, typename WriteHandler>
    void async_send(std::unique_ptr<Buffer>&& buffer_ptr, WriteHandler&& handler)
    {
        wstrand_.push([self = shared_from_this(),
                       buffer_ptr = std::move(buffer_ptr),
                       handler = std::forward<WriteHandler>(handler)]() mutable {
            assert(self->strand_.running_in_this_thread());
            internal::set_no_delay(self->socket_);

            auto buf = rpc_type::buffer(*buffer_ptr);
            net::async_write(
                self->socket_,
                buf,
                internal::bind_executor(
                    self->strand_,
                    [self,
                     buffer_ptr = std::move(buffer_ptr),
                     handler = std::forward<WriteHandler>(handler)](
                        error_code ec, size_t length) mutable {
                        self->wstrand_.next();
                        handler(ec, length);
                    }));
        });
    }

    void async_read(parser_type&& parser)
    {
        parser.reserve_buffer(buffer_reserve_size_);
        auto buffer = net::buffer(parser.buffer(), parser.buffer_capacity());

        assert(strand_.running_in_this_thread());
        reading_ = true;
        PACKIO_TRACE("reading ... {} call(s) pending", pending_.size());
        socket_.async_read_some(
            buffer,
            internal::bind_executor(
                strand_,
                [this, self = shared_from_this(), parser = std::move(parser)](

                    error_code ec, size_t length) mutable {
                    // stop if there is an error or there is no more pending calls
                    assert(self->strand_.running_in_this_thread());

                    if (ec) {
                        PACKIO_WARN("read error: {}", ec.message());
                        self->reading_ = false;
                        if (ec != net::error::operation_aborted)
                            self->close();
                        return;
                    }

                    PACKIO_TRACE("read: {}", length);
                    parser.buffer_consumed(length);

                    while (true) {
                        auto response = parser.get_response();
                        if (!response) {
                            PACKIO_INFO("stop reading: {}", response.error());
                            break;
                        }
                        self->async_call_handler(std::move(*response));
                    }

                    if (self->pending_.empty()) {
                        PACKIO_TRACE("done reading, no more pending calls");
                        self->reading_ = false;
                        return;
                    }

                    self->async_read(std::move(parser));
                }));
    }

    void async_call_handler(response_type&& response)
    {
        auto id = response.id;
        return async_call_handler(id, {}, std::move(response));
    }

    void async_call_handler(id_type id, error_code ec, response_type&& response)
    {
        net::dispatch(
            strand_,
            [ec,
             id,
             self = shared_from_this(),
             response = std::move(response)]() mutable {
                PACKIO_DEBUG(
                    "calling handler for id: {}", rpc_type::format_id(id));

                assert(self->strand_.running_in_this_thread());
                auto it = self->pending_.find(id);
                if (it == self->pending_.end()) {
                    PACKIO_WARN("unexisting id: {}", rpc_type::format_id(id));
                    return;
                }

                auto handler = std::move(it->second);
                self->pending_.erase(it);

                // handle the response asynchronously (post)
                // to schedule the next read immediately
                // this will allow parallel response handling
                // in multi-threaded environments
                net::post(
                    self->socket_.get_executor(),
                    [ec,
                     handler = std::move(handler),
                     response = std::move(response)]() mutable {
                        handler(ec, std::move(response));
                    });
            });
    }

    class initiate_async_notify {
    public:
        using executor_type = typename client::executor_type;

        explicit initiate_async_notify(client* self) : self_(self) {}

        executor_type get_executor() const noexcept
        {
            return self_->get_executor();
        }

        template <typename NotifyHandler, typename ArgsTuple>
        void operator()(
            NotifyHandler&& handler,
            std::string_view name,
            ArgsTuple&& args) const
        {
            PACKIO_STATIC_ASSERT_TRAIT(NotifyHandler);
            PACKIO_DEBUG("async_notify: {}", name);

            auto packer_buf = internal::to_unique_ptr(std::apply(
                [&name](auto&&... args) {
                    return rpc_type::serialize_notification(
                        name, std::forward<decltype(args)>(args)...);
                },
                std::forward<ArgsTuple>(args))

            );
            self_->async_send(
                std::move(packer_buf),
                [handler = std::forward<NotifyHandler>(handler),
                 self = self_->shared_from_this()](
                    error_code ec, std::size_t length) mutable {
                    if (ec) {
                        PACKIO_WARN("write error: {}", ec.message());
                        handler(ec);
                        if (ec != net::error::operation_aborted)
                            self->close();
                        return;
                    }

                    PACKIO_TRACE("write: {}", length);
                    (void)length;
                    handler(ec);
                });
        }

    private:
        client* self_;
    };

    class initiate_async_call {
    public:
        using executor_type = typename client::executor_type;

        explicit initiate_async_call(client* self) : self_(self) {}

        executor_type get_executor() const noexcept
        {
            return self_->get_executor();
        }

        template <typename CallHandler, typename ArgsTuple>
        void operator()(
            CallHandler&& handler,
            std::string_view name,
            ArgsTuple&& args,
            std::optional<std::reference_wrapper<id_type>> opt_call_id) const
        {
            PACKIO_STATIC_ASSERT_TTRAIT(CallHandler, rpc_type);
            PACKIO_DEBUG("async_call: {}", name);

            id_type call_id = self_->id_.fetch_add(1, std::memory_order_acq_rel);
            if (opt_call_id) {
                opt_call_id->get() = call_id;
            }

            auto packer_buf = internal::to_unique_ptr(std::apply(
                [&name, &call_id](auto&&... args) {
                    return rpc_type::serialize_request(
                        call_id, name, std::forward<decltype(args)>(args)...);
                },
                std::forward<ArgsTuple>(args)));

            net::dispatch(
                self_->strand_,
                [self = self_->shared_from_this(),
                 call_id,
                 handler = std::forward<CallHandler>(handler),
                 packer_buf = std::move(packer_buf)]() mutable {
                    // we must emplace the id and handler before sending data
                    // otherwise we might drop a fast response
                    assert(self->strand_.running_in_this_thread());
                    self->pending_.try_emplace(call_id, std::move(handler));

                    // if we are not reading, start the read operation
                    if (!self->reading_) {
                        PACKIO_DEBUG("start reading");
                        self->async_read(parser_type{});
                    }

                    // send the request buffer
                    self->async_send(
                        std::move(packer_buf),
                        [self = std::move(self), call_id](
                            error_code ec, std::size_t length) mutable {
                            if (ec) {
                                PACKIO_WARN("write error: {}", ec.message());
                                if (ec != net::error::operation_aborted)
                                    self->close();
                                return;
                            }

                            PACKIO_TRACE("write: {}", length);
                            (void)length;
                        });
                });
        }

    private:
        client* self_;
    };

    socket_type socket_;
    std::size_t buffer_reserve_size_{kDefaultBufferReserveSize};
    std::atomic<uint64_t> id_{0};

    net::strand<executor_type> strand_;
    internal::manual_strand<executor_type> wstrand_;

    Map<id_type, async_call_handler_type> pending_;
    bool reading_{false};
};

//! Create a client from a socket
//! @tparam Rpc RPC protocol implementation
//! @tparam Socket Socket type to use for this client
//! @tparam Map Container used to associate call IDs and handlers
template <typename Rpc, typename Socket, template <class...> class Map = default_map>
auto make_client(Socket&& socket)
{
    return std::make_shared<client<Rpc, Socket, Map>>(
        std::forward<Socket>(socket));
}

} // packio

#endif // PACKIO_CLIENT_H
