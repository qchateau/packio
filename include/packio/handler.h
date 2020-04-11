// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef PACKIO_HANDLER_H
#define PACKIO_HANDLER_H

//! @file
//! Class @ref packio::completion_handler "completion_handler"

#include <functional>

#include <msgpack.hpp>

#include "error_code.h"
#include "internal/config.h"
#include "internal/utils.h"

namespace packio {

//! The completion_handler class
//!
//! First argument of @ref traits::AsyncProcedure "AsyncProcedure", the
//! completion_handler is a callable used to notify the completion of an
//! asynchronous procedure. You must only call @ref set_value or
//! @ref set_error once.
class completion_handler {
public:
    using function_type =
        std::function<void(packio::err::error_code, msgpack::object_handle)>;

    template <typename F>
    completion_handler(F&& handler) : handler_{std::forward<F>(handler)}
    {
    }

    //! The destructor will notify an error if the completion_handler has not been used
    ~completion_handler()
    {
        if (handler_) {
            set_error("Call finished with no result");
        }
    }

    completion_handler(const completion_handler&) = delete;
    completion_handler& operator=(const completion_handler&) = delete;

    //! Move constructor
    completion_handler(completion_handler&& other)
        : handler_{std::move(other.handler_)}
    {
        other.handler_ = nullptr;
    }

    //! Move assignment operator
    completion_handler& operator=(completion_handler&& other)
    {
        if (handler_) {
            set_error("Call finished with no result");
        }
        handler_ = std::move(other.handler_);
        other.handler_ = nullptr;
        return *this;
    }

    //! Notify successful completion of the procedure and set the return value
    //! @param return_value The value that the procedure will return to the client
    template <typename T>
    void set_value(T&& return_value)
    {
        complete(
            error::success,
            internal::make_msgpack_object(std::forward<T>(return_value)));
    }

    //! Notify successful completion of the procedure with no return value
    //! @overload
    void set_value() { complete(error::success, {}); }

    //! Notify erroneous completion of the procedure with an associated error
    //! @param error_value Error value
    template <typename T>
    void set_error(T&& error_value)
    {
        complete(
            error::error_during_call,
            internal::make_msgpack_object(std::forward<T>(error_value)));
    }

    //! Notify erroneous completion of the procedure without an error value
    //! @overload
    void set_error() { complete(error::error_during_call, {}); }

    //! Same as @ref set_value
    template <typename T>
    void operator()(T&& return_value)
    {
        set_value(std::forward<T>(return_value));
    }

    //! Same as @ref set_value
    void operator()() { set_value(); }

private:
    void complete(error err, msgpack::object_handle result)
    {
        handler_(make_error_code(err), std::move(result));
        handler_ = nullptr;
    }

    function_type handler_;
};
} // packio

#endif // PACKIO_HANDLER_H