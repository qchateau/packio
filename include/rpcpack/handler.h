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

    template <typename R>
    void operator()(R&& return_value) const
    {
        raw_handler_(
            make_error_code(error::success),
            internal::make_msgpack_object(std::forward<R>(return_value)));
    }

    void operator()() const
    {
        raw_handler_(make_error_code(error::success), {});
    }

private:
    raw raw_handler_;
};

} // rpcpack

#endif // RPCPACK_HANDLER_H