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
    void operator()(T&& return_value)
    {
        handler_(
            make_error_code(error::success),
            internal::make_msgpack_object(std::forward<T>(return_value)));
        handler_ = function_type{};
    }

    void operator()()
    {
        handler_(make_error_code(error::success), {});
        handler_ = function_type{};
    }

    template <typename T>
    void set_error(T&& error_value)
    {
        handler_(
            make_error_code(error::error_during_call),
            internal::make_msgpack_object(std::forward<T>(error_value)));
        handler_ = function_type{};
    }

    void set_error()
    {
        handler_(make_error_code(error::error_during_call), {});
        handler_ = function_type{};
    }

private:
    function_type handler_;
};
} // packio

#endif // PACKIO_HANDLER_H