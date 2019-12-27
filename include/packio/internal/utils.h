// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef PACKIO_UTILS_H
#define PACKIO_UTILS_H

#include <sstream>
#include <string_view>
#include <vector>

#include <boost/asio.hpp>
#include <msgpack.hpp>

#include "log.h"

namespace packio {
namespace internal {

template <typename T>
struct shift_tuple;

template <typename A, typename... Bs>
struct shift_tuple<std::tuple<A, Bs...>> {
    using type = std::tuple<Bs...>;
};

template <typename T>
using shift_tuple_t = typename shift_tuple<T>::type;

template <typename T>
struct asio_buffer {
    using type = const T&;
};

template <>
struct asio_buffer<msgpack::sbuffer> {
    using type = boost::asio::const_buffer;
};

template <>
struct asio_buffer<msgpack::vrefbuffer> {
    using type = std::vector<boost::asio::const_buffer>;
};

template <typename Buffer>
inline typename asio_buffer<Buffer>::type buffer_to_asio(const Buffer& buffer)
{
    return buffer;
}

template <>
inline asio_buffer<msgpack::sbuffer>::type buffer_to_asio(const msgpack::sbuffer& buf)
{
    return boost::asio::const_buffer(buf.data(), buf.size());
}

template <>
inline asio_buffer<msgpack::vrefbuffer>::type buffer_to_asio(
    const msgpack::vrefbuffer& buf)
{
    typename asio_buffer<msgpack::vrefbuffer>::type vec;
    vec.reserve(buf.vector_size());
    const struct iovec* iov = buf.vector();
    for (std::size_t i = 0; i < buf.vector_size(); ++i) {
        vec.push_back(boost::asio::const_buffer(iov->iov_base, iov->iov_len));
        ++iov;
    }
    return vec;
}

inline ssize_t args_count(const msgpack::object& args)
{
    if (args.type != msgpack::type::ARRAY) {
        return -1;
    }
    return args.via.array.size;
}

template <typename T>
msgpack::object_handle make_msgpack_object(T&& value)
{
    msgpack::object_handle oh({}, std::make_unique<msgpack::zone>());
    oh.set(msgpack::object(std::forward<T>(value), *oh.zone()));
    return oh;
}

template <typename T>
void set_no_delay(T&)
{
}

template <>
inline void set_no_delay(boost::asio::ip::tcp::socket& socket)
{
    socket.set_option(boost::asio::ip::tcp::no_delay{true});
}

template <typename F>
auto make_copyable_function(F&& movable)
{
    // We handle a lot of move-only lambda function and we sometimes
    // need to wrap them into std::function, which requires the functor to
    // be copyable. To solve the problem, wrap the lambda in a shared pointer
    auto fct_ptr = std::make_shared<std::decay_t<F>>(std::forward<F>(movable));
    return [fct_ptr = std::move(fct_ptr)](auto&&... args) {
        return (*fct_ptr)(std::forward<decltype(args)>(args)...);
    };
}

} // internal
} // packio

#endif // PACKIO_UTILS_H
