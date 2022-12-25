// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef PACKIO_HANDLER_H
#define PACKIO_HANDLER_H

//! @file
//! Class @ref packio::completion_handler "completion_handler"

#include <functional>

#include "internal/config.h"
#include "internal/rpc.h"
#include "internal/utils.h"

namespace packio {

//! The completion_handler class
//! @tparam Rpc RPC protocol implementation
//!
//! First argument of @ref traits::AsyncProcedure "AsyncProcedure", the
//! completion_handler is a callable used to notify the completion of an
//! asynchronous procedure. You must only call @ref set_value or
//! @ref set_error once.
template <typename Rpc>
class completion_handler {
public:
    using id_type = typename Rpc::id_type;
    using response_buffer_type =
        decltype(Rpc::serialize_response(std::declval<id_type>()));
    using function_type = std::function<void(response_buffer_type&&)>;

    template <typename F>
    completion_handler(const id_type& id, F&& handler)
        : id_(id), handler_(std::forward<F>(handler))
    {
    }

    //! The destructor will notify an error if the completion_handler has not been used
    ~completion_handler()
    {
        if (handler_) {
            set_error("call finished with no result");
        }
    }

    completion_handler(const completion_handler&) = delete;
    completion_handler& operator=(const completion_handler&) = delete;

    //! Move constructor
    completion_handler(completion_handler&& other)
        : id_(other.id_), handler_(std::move(other.handler_))
    {
        other.handler_ = nullptr;
    }

    //! Move assignment operator
    completion_handler& operator=(completion_handler&& other)
    {
        if (handler_) {
            set_error("call finished with no result");
        }
        id_ = other.id_;
        handler_ = std::move(other.handler_);
        other.handler_ = nullptr;
        return *this;
    }

    //! Notify successful completion of the procedure and set the return value
    //! @param return_value The value that the procedure will return to the client
    template <typename T>
    void set_value(T&& return_value)
    {
        complete(Rpc::serialize_response(id_, std::forward<T>(return_value)));
    }

    //! @overload
    void set_value() { complete(Rpc::serialize_response(id_)); }

    //! Notify erroneous completion of the procedure with an associated error
    //! @param error_value Error value
    template <typename T>
    void set_error(T&& error_value)
    {
        complete(Rpc::serialize_error_response(id_, std::forward<T>(error_value)));
    }

    //! @overload
    void set_error()
    {
        complete(Rpc::serialize_error_response(id_, "unknown error"));
    }

    //! Same as @ref set_value
    template <typename T>
    void operator()(T&& return_value)
    {
        set_value(std::forward<T>(return_value));
    }

    //! Same as @ref set_value
    void operator()() { set_value(); }

private:
    void complete(response_buffer_type&& buffer)
    {
        handler_(std::move(buffer));
        handler_ = nullptr;
    }

    id_type id_;
    function_type handler_;
};

template <typename>
struct is_completion_handler : std::false_type {
};

template <typename T>
struct is_completion_handler<completion_handler<T>> : std::true_type {
};

template <typename T>
constexpr auto is_completion_handler_v = is_completion_handler<T>::value;

template <typename...>
struct is_async_procedure_args;

template <>
struct is_async_procedure_args<std::tuple<>> : std::false_type {
};

template <typename T0, typename... Ts>
struct is_async_procedure_args<std::tuple<T0, Ts...>>
    : is_completion_handler<T0> {
};

template <typename F>
struct is_async_procedure
    : is_async_procedure_args<typename internal::func_traits<F>::args_type> {
};

template <typename T>
constexpr auto is_async_procedure_v = is_async_procedure<T>::value;

} // packio

#endif // PACKIO_HANDLER_H
