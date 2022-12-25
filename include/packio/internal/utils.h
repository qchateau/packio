// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef PACKIO_UTILS_H
#define PACKIO_UTILS_H

#include <sstream>
#include <string_view>
#include <type_traits>
#include <vector>

#include "config.h"
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
    static constexpr auto args_count = std::tuple_size_v<args_type>;
};

template <typename T>
constexpr bool func_traits_v = func_traits<T>::value;

template <typename>
struct is_awaitable : std::false_type {
};

#if defined(PACKIO_HAS_CO_AWAIT)
template <typename... Args>
struct is_awaitable<net::awaitable<Args...>> : std::true_type {
};
#endif // defined(PACKIO_HAS_CO_AWAIT)

template <typename, typename = void>
struct is_coroutine : std::false_type {
};

template <typename T>
struct is_coroutine<T, std::enable_if_t<func_traits_v<T>>>
    : is_awaitable<typename func_traits<T>::result_type> {
};

template <typename T>
constexpr bool is_coroutine_v = is_coroutine<T>::value;

template <typename T, typename = void>
struct is_tuple : std::false_type {
};

template <typename T>
struct is_tuple<T, std::void_t<decltype(std::tuple_size<std::decay_t<T>>::value)>>
    : std::true_type {
};

template <typename T>
constexpr auto is_tuple_v = is_tuple<T>::value;

template <typename T>
struct left_shift_tuple;

template <typename A, typename... Bs>
struct left_shift_tuple<std::tuple<A, Bs...>> {
    using type = std::tuple<Bs...>;
};

template <typename T>
using left_shift_tuple_t = typename left_shift_tuple<T>::type;

template <template <typename...> class Map, typename T>
struct map_tuple;

template <template <typename...> class Map, typename... Args>
struct map_tuple<Map, std::tuple<Args...>> {
    using type = std::tuple<Map<Args>...>;
};

template <template <typename...> class Map, typename T>
using map_tuple_t = typename map_tuple<Map, T>::type;

template <typename T>
using decay_tuple_t = map_tuple_t<std::decay_t, T>;

template <typename... Args>
struct always_false : std::false_type {
};

template <typename... Args>
constexpr auto always_false_v = always_false<Args...>::value;

template <typename T>
void set_no_delay(T&)
{
}

template <>
inline void set_no_delay(net::ip::tcp::socket& socket)
{
    error_code ec;
    socket.set_option(net::ip::tcp::no_delay{true}, ec);
    if (ec) {
        PACKIO_WARN("error setting tcp nodelay option: {}", ec.message());
    }
}

template <typename T>
std::unique_ptr<std::decay_t<T>> to_unique_ptr(T&& value)
{
    return std::make_unique<std::decay_t<T>>(std::forward<T>(value));
}

template <typename Executor, typename Obj>
auto bind_executor(Executor&& executor, Obj&& obj)
{
    return net::bind_executor(
        any_io_executor(std::forward<Executor>(executor)),
        std::forward<Obj>(obj));
}

} // internal
} // packio

#endif // PACKIO_UTILS_H
