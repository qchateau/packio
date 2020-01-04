// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef PACKIO_UTILS_H
#define PACKIO_UTILS_H

#include <sstream>
#include <string_view>
#include <type_traits>
#include <vector>

#include <boost/asio.hpp>
#include <msgpack.hpp>

#include "log.h"

namespace packio {
namespace internal {

template <typename, typename = void>
struct func_traits : std::false_type {
};

template <typename T>
struct func_traits<T, std::void_t<decltype(&std::decay_t<T>::operator())>>
    : func_traits<decltype(&std::decay_t<T>::operator())> {
};

template <typename C, typename R, typename... Args>
struct func_traits<R (C::*)(Args...)> : func_traits<R (*)(Args...)> {
};

template <typename C, typename R, typename... Args>
struct func_traits<R (C::*)(Args...) const> : func_traits<R (*)(Args...)> {
};

template <typename R, typename... Args>
struct func_traits<R (*)(Args...)> : std::true_type {
    using result_type = R;
    using args_type = std::tuple<Args...>;
};

template <typename T>
constexpr bool func_traits_v = func_traits<T>::value;

template <typename T>
struct shift_tuple;

template <typename A, typename... Bs>
struct shift_tuple<std::tuple<A, Bs...>> {
    using type = std::tuple<Bs...>;
};

template <typename T>
using shift_tuple_t = typename shift_tuple<T>::type;

template <typename T>
struct decay_tuple;

template <typename... Args>
struct decay_tuple<std::tuple<Args...>> {
    using type = std::tuple<std::decay_t<Args>...>;
};

template <typename T>
using decay_tuple_t = typename decay_tuple<T>::type;

inline boost::asio::const_buffer buffer(const msgpack::sbuffer& buf)
{
    return boost::asio::const_buffer(buf.data(), buf.size());
}

inline std::vector<boost::asio::const_buffer> buffer(const msgpack::vrefbuffer& buf)
{
    std::vector<boost::asio::const_buffer> vec;
    vec.reserve(buf.vector_size());
    const struct iovec* iov = buf.vector();
    for (std::size_t i = 0; i < buf.vector_size(); ++i) {
        vec.push_back(boost::asio::const_buffer(iov->iov_base, iov->iov_len));
        ++iov;
    }
    return vec;
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

} // internal
} // packio

#endif // PACKIO_UTILS_H
