#ifndef PACKIO_AS_H
#define PACKIO_AS_H

//! @file
//! Function @ref packio::as "as"

#include <type_traits>
#include <utility>
#include <boost/asio.hpp>
#include <msgpack.hpp>

#include "error_code.h"
#include "internal/utils.h"
#include "traits.h"

namespace packio {

//! Function used to wrap a typed call handler
//!
//! This function is used to provide a call handler that expects a specific
//! return type. If the procedure call succeeds but the returned type is not
//! what is expected, the handler will be called with @ref
//! error::bad_result_type. The optional given as second argument will have a
//! value only if the error code is @ref error::success.
//!
//! @tparam Result The expected return type for the procedure
//! @param handler Call handler to wrap. Must satisfy @ref traits::AsCallHandler
template <typename Result, typename AsCallHandler>
auto as(
    AsCallHandler&& handler,
    std::enable_if_t<!std::is_void_v<Result>, void*> = nullptr)
{
    ASSERT_TTRAIT(AsCallHandler, Result);
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

//! Function used to wrap a call handler that expects a void result
//!
//! @overload
//! The only point of this wrapper is to verify that the remote procedure
//! returned void. If the remote procedure returns any other type, this wrapper
//! will set the error code to @ref error::bad_result_type.
template <typename Result, typename AsVoidCallHandler>
auto as(
    AsVoidCallHandler&& handler,
    std::enable_if_t<std::is_void_v<Result>, void*> = nullptr)
{
    ASSERT_TRAIT(AsVoidCallHandler);
    return [handler = std::forward<AsVoidCallHandler>(handler)](
               boost::system::error_code ec,
               msgpack::object_handle result) mutable {
        if (!ec && result->type != msgpack::type::NIL) {
            ec = make_error_code(error::bad_result_type);
        }
        handler(ec);
    };
}

} // packio

#endif // PACKIO_AS_H
