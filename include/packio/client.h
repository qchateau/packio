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
#include <map>
#include <memory>
#include <queue>
#include <string_view>
#include <type_traits>

#include <boost/asio.hpp>
#include <msgpack.hpp>

#include "error_code.h"
#include "internal/config.h"
#include "internal/manual_strand.h"
#include "internal/msgpack_rpc.h"
#include "internal/unique_function.h"
#include "internal/utils.h"
#include "traits.h"

namespace packio {

//! The client class
//! @tparam Socket Socket type to use for this client
//! @tparam Map Container used to associate call IDs and handlers
template <typename Socket, template <class...> class Map = std::map>
class client : public std::enable_shared_from_this<client<Socket, Map>> {
public:
    using socket_type = Socket; //!< The socket type
    using protocol_type =
        typename socket_type::protocol_type; //!< The protocol type
    using executor_type =
        typename socket_type::executor_type; //!< The executor type
    using std::enable_shared_from_this<client<Socket, Map>>::shared_from_this;

    //! The default size reserved by the reception buffer
    static constexpr size_t kDefaultBufferReserveSize = 4096;

    //! The constructor
    //! @param socket The socket which the client will use. Can be connected or not
    explicit client(socket_type socket)
        : socket_{std::move(socket)},
          wstrand_{socket_.get_executor()},
          call_strand_{socket_.get_executor()}
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
    //! The associated handler will be called with @ref error::cancelled
    //! @param id The call ID of the call to cancel
    void cancel(id_type id)
    {
        boost::asio::dispatch(call_strand_, [self = shared_from_this(), id] {
            auto ec = make_error_code(error::cancelled);
            self->async_call_handler(
                id, internal::make_msgpack_object(ec.message()), ec);
            self->maybe_stop_reading();
        });
    }

    //! Cancel all pending calls
    //!
    //! The associated handlers will be called with @ref error::cancelled
    void cancel()
    {
        boost::asio::dispatch(call_strand_, [self = shared_from_this()] {
            auto ec = make_error_code(error::cancelled);
            while (!self->pending_.empty()) {
                self->async_call_handler(
                    self->pending_.begin()->first,
                    internal::make_msgpack_object(ec.message()),
                    ec);
            }
            self->maybe_stop_reading();
        });
    }

    //! Send a notify request to the server with argument
    //!
    //! A notify request will call the remote procedure but
    //! does not expect a response
    //! @tparam Buffer Buffer used to serialize the arguments. Recommended:
    //! - msgpack::sbuffer is an owning buffer \n
    //! - msgpack::vrefbuffer is a non-owning buffer
    //! @param name Remote procedure name to call
    //! @param args Tuple of arguments to pass to the remote procedure
    //! @param handler Handler called after the notify request is sent.
    //! Must satisfy the @ref traits::NotifyHandler trait
    template <
        typename Buffer = msgpack::sbuffer,
        typename NotifyHandler =
            typename boost::asio::default_completion_token<executor_type>::type,
        typename... Args>
    auto async_notify(
        std::string_view name,
        const std::tuple<Args...>& args,
        NotifyHandler&& handler =
            typename boost::asio::default_completion_token<executor_type>::type())
    {
        return boost::asio::async_initiate<NotifyHandler, void(boost::system::error_code)>(
            initiate_async_notify<Buffer>(this), handler, name, args);
    }

    //! Send a notify request to the server with no argument
    //! @overload
    template <
        typename Buffer = msgpack::sbuffer,
        typename NotifyHandler =
            typename boost::asio::default_completion_token<executor_type>::type>
    auto async_notify(
        std::string_view name,
        NotifyHandler&& handler =
            typename boost::asio::default_completion_token<executor_type>::type())
    {
        return async_notify<Buffer>(
            name, std::tuple{}, std::forward<NotifyHandler>(handler));
    }

    //! Call a remote procedure
    //!
    //! @tparam Buffer Buffer used to serialize the arguments. Recommended:
    //! - msgpack::sbuffer is an owning buffer \n
    //! - msgpack::vrefbuffer is a non-owning buffer
    //! @param name Remote procedure name to call
    //! @param args Tuple of arguments to pass to the remote procedure
    //! @param handler Handler called with the return value
    //! @param call_id Output parameter that will receive the call ID
    //! Must satisfy the @ref traits::CallHandler trait
    template <
        typename Buffer = msgpack::sbuffer,
        typename CallHandler =
            typename boost::asio::default_completion_token<executor_type>::type,
        typename... Args>
    auto async_call(
        std::string_view name,
        const std::tuple<Args...>& args,
        CallHandler&& handler =
            typename boost::asio::default_completion_token<executor_type>::type(),
        std::optional<std::reference_wrapper<id_type>> call_id = std::nullopt)
    {
        return boost::asio::async_initiate<
            CallHandler,
            void(boost::system::error_code, msgpack::object_handle)>(
            initiate_async_call<Buffer>(this), handler, name, args, call_id);
    }

    //! Call a remote procedure
    //! @overload
    template <
        typename Buffer = msgpack::sbuffer,
        typename CallHandler =
            typename boost::asio::default_completion_token<executor_type>::type>
    auto async_call(
        std::string_view name,
        CallHandler&& handler =
            typename boost::asio::default_completion_token<executor_type>::type(),
        std::optional<std::reference_wrapper<id_type>> call_id = std::nullopt)
    {
        return async_call<Buffer>(
            name, std::tuple{}, std::forward<CallHandler>(handler), call_id);
    }

private:
    using async_call_handler_type = internal::unique_function<
        void(boost::system::error_code, msgpack::object_handle)>;

    void maybe_stop_reading()
    {
        assert(call_strand_.running_in_this_thread());
        if (reading_ && pending_.empty()) {
            PACKIO_DEBUG("stop reading");
            boost::system::error_code ec;
            socket_.cancel(ec);
            if (ec) {
                PACKIO_WARN("cancel failed: {}", ec.message());
            }
        }
    }

    template <typename Buffer, typename WriteHandler>
    void async_send(std::unique_ptr<Buffer> buffer_ptr, WriteHandler&& handler)
    {
        wstrand_.push([this,
                       self = shared_from_this(),
                       buffer_ptr = std::move(buffer_ptr),
                       handler = std::forward<WriteHandler>(handler)]() mutable {
            using internal::buffer;

            internal::set_no_delay(socket_);

            auto buf = buffer(*buffer_ptr);
            boost::asio::async_write(
                socket_,
                buf,
                [self = std::move(self),
                 buffer_ptr = std::move(buffer_ptr),
                 handler = std::forward<WriteHandler>(handler)](
                    boost::system::error_code ec, size_t length) mutable {
                    self->wstrand_.next();
                    handler(ec, length);
                });
        });
    }

    void async_read(std::unique_ptr<msgpack::unpacker> unpacker)
    {
        unpacker->reserve_buffer(buffer_reserve_size_);
        auto buffer = boost::asio::buffer(
            unpacker->buffer(), unpacker->buffer_capacity());

        assert(call_strand_.running_in_this_thread());
        reading_ = true;
        PACKIO_TRACE("reading ... {} call(s) pending", pending_.size());
        socket_.async_read_some(
            buffer,
            boost::asio::bind_executor(
                call_strand_,
                [self = shared_from_this(), unpacker = std::move(unpacker)](
                    boost::system::error_code ec, size_t length) mutable {
                    PACKIO_TRACE("read: {}", length);
                    unpacker->buffer_consumed(length);

                    msgpack::object_handle response;
                    while (unpacker->next(response)) {
                        self->process_response(std::move(response), ec);
                    }

                    // stop if there is an error or there is no more pending calls
                    assert(self->call_strand_.running_in_this_thread());

                    if (ec && ec != boost::asio::error::operation_aborted) {
                        PACKIO_WARN("read error: {}", ec.message());
                        self->reading_ = false;
                        return;
                    }

                    if (self->pending_.empty()) {
                        PACKIO_TRACE("done reading, no more pending calls");
                        self->reading_ = false;
                        return;
                    }

                    self->async_read(std::move(unpacker));
                }));
    }

    void process_response(msgpack::object_handle response, boost::system::error_code ec)
    {
        if (!verify_reponse(response.get())) {
            PACKIO_ERROR("received unexpected response");
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
                PACKIO_DEBUG("calling handler for id: {}", id);

                assert(call_strand_.running_in_this_thread());
                auto it = pending_.find(id);
                if (it == pending_.end()) {
                    PACKIO_WARN("unexisting id");
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
            PACKIO_ERROR("unexpected message type: {}", response.type);
            return false;
        }
        if (response.via.array.size != 4) {
            PACKIO_ERROR("unexpected message size: {}", response.via.array.size);
            return false;
        }
        int type = response.via.array.ptr[0].as<int>();
        if (type != static_cast<int>(msgpack_rpc_type::response)) {
            PACKIO_ERROR("unexpected type: {}", type);
            return false;
        }
        return true;
    }

    template <typename Buffer>
    class initiate_async_notify {
    public:
        using executor_type = typename client::executor_type;

        explicit initiate_async_notify(client* self) : self_(self) {}

        executor_type get_executor() const noexcept
        {
            return self_->get_executor();
        }

        template <typename NotifyHandler, typename... Args>
        void operator()(
            NotifyHandler&& handler,
            std::string_view name,
            const std::tuple<Args...>& args) const
        {
            PACKIO_STATIC_ASSERT_TRAIT(NotifyHandler);
            PACKIO_DEBUG("async_notify: {}", name);

            auto packer_buf = std::make_unique<Buffer>();
            msgpack::pack(
                *packer_buf,
                std::forward_as_tuple(
                    static_cast<int>(msgpack_rpc_type::notification), name, args));

            self_->async_send(
                std::move(packer_buf),
                [handler = std::forward<NotifyHandler>(handler)](
                    boost::system::error_code ec, std::size_t length) mutable {
                    if (ec) {
                        PACKIO_WARN("write error: {}", ec.message());
                    }
                    else {
                        PACKIO_TRACE("write: {}", length);
                        (void)length;
                    }

                    handler(ec);
                });
        }

    private:
        client* self_;
    };

    template <typename Buffer>
    class initiate_async_call {
    public:
        using executor_type = typename client::executor_type;

        explicit initiate_async_call(client* self) : self_(self) {}

        executor_type get_executor() const noexcept
        {
            return self_->get_executor();
        }

        template <typename CallHandler, typename... Args>
        void operator()(
            CallHandler&& handler,
            std::string_view name,
            const std::tuple<Args...>& args,
            std::optional<std::reference_wrapper<id_type>> opt_call_id) const
        {
            PACKIO_STATIC_ASSERT_TRAIT(CallHandler);
            PACKIO_DEBUG("async_call: {}", name);

            id_type call_id = self_->id_.fetch_add(1, std::memory_order_acq_rel);
            if (opt_call_id) {
                opt_call_id->get() = call_id;
            }

            auto packer_buf = std::make_unique<Buffer>();
            msgpack::pack(
                *packer_buf,
                std::forward_as_tuple(
                    static_cast<int>(msgpack_rpc_type::request),
                    call_id,
                    name,
                    args));

            boost::asio::dispatch(
                self_->call_strand_,
                [self = self_->shared_from_this(),
                 call_id,
                 handler = internal::wrap_call_handler(
                     std::forward<CallHandler>(handler)),
                 packer_buf = std::move(packer_buf)]() mutable {
                    // we must emplace the id and handler before sending data
                    // otherwise we might drop a fast response
                    assert(self->call_strand_.running_in_this_thread());
                    self->pending_.try_emplace(call_id, std::move(handler));

                    // if we are not reading, start the read operation
                    if (!self->reading_) {
                        PACKIO_DEBUG("start reading");
                        self->async_read(std::make_unique<msgpack::unpacker>());
                    }

                    // send the request buffer
                    self->async_send(
                        std::move(packer_buf),
                        [self = std::move(self), call_id](
                            boost::system::error_code ec,
                            std::size_t length) mutable {
                            if (ec) {
                                PACKIO_WARN("write error: {}", ec.message());
                                self->async_call_handler(
                                    call_id,
                                    internal::make_msgpack_object(ec.message()),
                                    ec);
                            }
                            else {
                                PACKIO_TRACE("write: {}", length);
                                (void)length;
                            }
                        });
                });
        }

    private:
        client* self_;
    };

    socket_type socket_;
    std::size_t buffer_reserve_size_{kDefaultBufferReserveSize};
    std::atomic<id_type> id_{0};

    internal::manual_strand<executor_type> wstrand_;

    boost::asio::strand<executor_type> call_strand_;
    Map<id_type, async_call_handler_type> pending_;
    bool reading_{false};
};

//! Create a client from a socket
//! @tparam Socket Socket type to use for this client
//! @tparam Map Container used to associate call IDs and handlers
template <typename Socket, template <class...> class Map = std::map>
auto make_client(Socket&& socket)
{
    return std::make_shared<client<Socket, Map>>(std::forward<Socket>(socket));
}

} // packio

#endif // PACKIO_CLIENT_H