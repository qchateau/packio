#ifndef PACKIO_AS_H
#define PACKIO_AS_H

#include <utility>
#include <boost/asio.hpp>
#include <msgpack.hpp>

#include "error_code.h"
#include "internal/utils.h"

namespace packio {

template <typename Result, typename AsCallHandler>
auto as(AsCallHandler&& handler)
{
    return [handler = std::forward<AsCallHandler>(handler)](
               boost::system::error_code ec,
               msgpack::object_handle result) mutable {
        if (ec) {
            handler(ec, std::nullopt);
        }
        else {
            try {
                handler(ec, std::optional<Result>{result->as<Result>()});
            }
            catch (msgpack::type_error&) {
                ec = make_error_code(error::bad_result_type);
                handler(ec, std::nullopt);
            }
        }
    };
}

} // packio

#endif // PACKIO_AS_H
