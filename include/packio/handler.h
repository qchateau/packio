// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef PACKIO_HANDLER_H
#define PACKIO_HANDLER_H

#include <functional>

#include <boost/asio.hpp>
#include <msgpack.hpp>

#include "error_code.h"
#include "internal/utils.h"

namespace packio {

class completion_handler {
public:
    using function_type =
        std::function<void(boost::system::error_code, msgpack::object_handle)>;

    template <typename F>
    completion_handler(F&& handler) : handler_{std::forward<F>(handler)}
    {
    }

    ~completion_handler()
    {
        if (handler_) {
            set_error("Call finished with no result");
        }
    }

    completion_handler(const completion_handler&) = delete;
    completion_handler& operator=(const completion_handler&) = delete;

    completion_handler(completion_handler&&) = default;
    completion_handler& operator=(completion_handler&&) = default;

    template <typename T>
    void set_value(T&& return_value)
    {
        complete(
            error::success,
            internal::make_msgpack_object(std::forward<T>(return_value)));
    }

    void set_value() { complete(error::success, {}); }

    template <typename T>
    void set_error(T&& error_value)
    {
        complete(
            error::error_during_call,
            internal::make_msgpack_object(std::forward<T>(error_value)));
    }

    void set_error() { complete(error::error_during_call, {}); }

    template <typename T>
    void operator()(T&& return_value)
    {
        set_value(std::forward<T>(return_value));
    }

    void operator()() { set_value(); }

private:
    void complete(error err, msgpack::object_handle result)
    {
        handler_(make_error_code(err), std::move(result));
        handler_ = function_type{};
    }
    function_type handler_;
};
} // packio

#endif // PACKIO_HANDLER_H