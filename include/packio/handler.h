#ifndef PACKIO_HANDLER_H
#define PACKIO_HANDLER_H

#include <functional>

#include <boost/asio.hpp>
#include <msgpack.hpp>

#include "error_code.h"
#include "internal/utils.h"

namespace packio {
namespace internal {

class completion_handler_raw {
public:
    using function_type =
        std::function<void(boost::system::error_code, msgpack::object_handle)>;

    completion_handler_raw(function_type handler) : handler_{std::move(handler)}
    {
    }

    completion_handler_raw(const completion_handler_raw&) = delete;
    completion_handler_raw& operator=(const completion_handler_raw&) = delete;
    completion_handler_raw(completion_handler_raw&&) = delete;
    completion_handler_raw& operator=(completion_handler_raw&&) = delete;

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

    ~completion_handler_raw()
    {
        if (handler_) {
            set_error("Call finished with no result");
        }
    }

private:
    function_type handler_;
};

} // internal

class completion_handler {
public:
    using raw = internal::completion_handler_raw;

    completion_handler(const std::shared_ptr<raw>& raw_handler)
        : raw_handler_{raw_handler}
    {
    }

    completion_handler(const completion_handler&) = delete;
    completion_handler& operator=(const completion_handler&) = delete;

    completion_handler(completion_handler&&) = default;
    completion_handler& operator=(completion_handler&&) = default;

    template <typename... Args>
    void operator()(Args&&... args)
    {
        (*raw_handler_)(std::forward<Args>(args)...);
    }

    template <typename... Args>
    void set_error(Args&&... args)
    {
        raw_handler_->set_error(std::forward<Args>(args)...);
    }

private:
    std::shared_ptr<raw> raw_handler_;
};

} // packio

#endif // PACKIO_HANDLER_H