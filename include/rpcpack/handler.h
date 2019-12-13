#ifndef RPCPACK_HANDLER_H
#define RPCPACK_HANDLER_H

#include <functional>

#include <boost/asio.hpp>
#include <msgpack.hpp>

#include "error_code.h"
#include "internal/utils.h"

namespace rpcpack {

class completion_handler {
public:
    using raw =
        std::function<void(boost::system::error_code, msgpack::object_handle)>;

    completion_handler(raw raw_handler) : raw_handler_{std::move(raw_handler)}
    {
    }

    completion_handler(const completion_handler&) = delete;
    completion_handler& operator=(const completion_handler&) = delete;

    completion_handler(completion_handler&&) = default;
    completion_handler& operator=(completion_handler&&) = default;

    template <typename T>
    void operator()(T&& return_value)
    {
        raw_handler_(
            make_error_code(error::success),
            internal::make_msgpack_object(std::forward<T>(return_value)));
        raw_handler_ = raw{};
    }

    void operator()()
    {
        raw_handler_(make_error_code(error::success), {});
        raw_handler_ = raw{};
    }

    template <typename T>
    void set_error(T&& error_value)
    {
        raw_handler_(
            make_error_code(error::error_during_call),
            internal::make_msgpack_object(std::forward<T>(error_value)));
        raw_handler_ = raw{};
    }

    void set_error()
    {
        raw_handler_(make_error_code(error::error_during_call), {});
        raw_handler_ = raw{};
    }

private:
    raw raw_handler_;
};

} // rpcpack

#endif // RPCPACK_HANDLER_H