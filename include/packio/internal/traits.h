// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef PACKIO_TRAITS_H
#define PACKIO_TRAITS_H

#include <type_traits>
#include <utility>
#include <boost/asio.hpp>
#include <msgpack.hpp>

#include "utils.h"

#if defined(PACKIO_TRAITS_CHECK_DISABLE) && PACKIO_TRAITS_CHECK_DISABLE
#define ASSERT_TRAIT(Trait) (void)0
#define ASSERT_TTRAIT(Trait, ...) (void)0
#else
#define ASSERT_TRAIT(Trait) \
    static_assert(          \
        ::packio::traits::Trait<Trait>::value, "Trait " #Trait " not satisfied")
#define ASSERT_TTRAIT(Trait, ...)                           \
    static_assert(                                          \
        ::packio::traits::Trait<Trait, __VA_ARGS__>::value, \
        "Trait " #Trait " not satisfied")
#endif

namespace packio {

class completion_handler;

namespace traits {

namespace details {

template <typename, typename = void>
struct AsyncProcedureImpl : std::false_type {
};

template <typename T>
struct AsyncProcedureImpl<T, std::enable_if_t<internal::func_traits_v<T>>> {
    using TArgs = typename internal::func_traits<T>::args_type;
    static constexpr bool value = [] {
        if constexpr (std::tuple_size_v<TArgs> == 0) {
            return false;
        }
        else {
            return std::is_same_v<std::decay_t<std::tuple_element_t<0, TArgs>>, completion_handler>;
        }
    }();
};

template <typename, typename = void>
struct SyncProcedureImpl : std::false_type {
};

template <typename T>
struct SyncProcedureImpl<T, std::enable_if_t<internal::func_traits_v<T>>>
    : std::true_type {
};

} // details

template <bool condition>
struct Trait : std::integral_constant<bool, condition> {
};

template <typename T>
struct NotifyHandler : Trait<std::is_invocable_v<T, boost::system::error_code>> {
};

template <typename T>
struct CallHandler
    : Trait<std::is_invocable_v<T, boost::system::error_code, msgpack::object_handle>> {
};

template <typename T, typename Result>
struct AsCallHandler
    : Trait<std::is_invocable_v<T, boost::system::error_code, std::optional<Result>>> {
};

template <typename T, typename Session>
struct ServerHandler
    : Trait<std::is_invocable_v<T, boost::system::error_code, std::shared_ptr<Session>>> {
};

template <typename T>
struct AsyncProcedure : Trait<details::AsyncProcedureImpl<T>::value> {
};

template <typename T>
struct SyncProcedure : Trait<details::SyncProcedureImpl<T>::value> {
};

} // traits
} // packio

#endif // PACKIO_TRAITS_H
