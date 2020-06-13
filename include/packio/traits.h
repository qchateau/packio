// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef PACKIO_TRAITS_H
#define PACKIO_TRAITS_H

//! @file
//! Traits definition

#include <type_traits>
#include <utility>

#include <msgpack.hpp>

#include "internal/config.h"
#include "internal/utils.h"

#if defined(PACKIO_TRAITS_CHECK_DISABLE)
#define PACKIO_STATIC_ASSERT_TRAIT(Trait) (void)0
#define PACKIO_STATIC_ASSERT_TTRAIT(Trait, ...) (void)0
#else
#define PACKIO_STATIC_ASSERT_TRAIT(Trait) \
    static_assert(                        \
        ::packio::traits::Trait<Trait>::value, "Trait " #Trait " not satisfied")
#define PACKIO_STATIC_ASSERT_TTRAIT(Trait, ...)             \
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

template <typename T>
constexpr bool is_async_procedure_v = AsyncProcedureImpl<T>::value;

} // details

template <bool condition>
struct Trait : std::integral_constant<bool, condition> {
};

//! NotifyHandler trait
//!
//! Handler used by @ref client::async_notify
//! - Must be callable with an error_code
template <typename T>
struct NotifyHandler : Trait<std::is_invocable_v<T, error_code>> {
};

//! CallHandler trait
//!
//! Handler used by @ref client::async_call
//! - Must be callable with error_code, msgpack::object_handle
template <typename T>
struct CallHandler
    : Trait<std::is_invocable_v<T, error_code, msgpack::object_handle>> {
};

//! AsCallHandler
//!
//! Handler wrapped by @ref as<T>
//! - Must be callable with error_code, std::optional<T>
template <typename T, typename Result>
struct AsCallHandler
    : Trait<std::is_invocable_v<T, error_code, std::optional<Result>>> {
};

//! AsVoidCallHandler
//!
//! Handler wrapped by @ref as<void>
//! - Must be callable with error_code
template <typename T>
struct AsVoidCallHandler : Trait<std::is_invocable_v<T, error_code>> {
};

//! ServeHandler trait
//!
//! Handler used by @ref server::async_serve
//! - Must be callable with
//! error_code, std::shared_ptr<@ref server::session_type "session_type">
template <typename T, typename Session>
struct ServeHandler
    : Trait<std::is_invocable_v<T, error_code, std::shared_ptr<Session>>> {
};

//! AsyncProcedure trait
//!
//! Procedure registered with @ref dispatcher::add_async
//! - Must be callable with @ref completion_handler as first argument.
//! - No overload resolution can be performed.
//! - The other arguments must be msgpack-able and will be used as the procedure's arguments.
template <typename T>
struct AsyncProcedure : Trait<details::is_async_procedure_v<T>> {
};

//! SyncProcedure trait
//!
//! Procedure registered with @ref dispatcher::add
//! - Must be callable.
//! - No overload resolution can be performed.
//! - The arguments must be msgpack-able and will be used as the procedure's arguments.
template <typename T>
struct SyncProcedure : Trait<internal::func_traits_v<T>> {
};

#if defined(PACKIO_HAS_CO_AWAIT) || defined(PACKIO_DOCUMENTATION)
//! CoroProcedure trait
//!
//! Procedure registered with @ref dispatcher::add_coro
//! - Must be coroutine.
//! - No overload resolution can be performed.
//! - The arguments must be msgpack-able and will be used as the procedure's arguments.
template <typename T>
struct CoroProcedure
    : Trait<internal::is_coroutine_v<T> && internal::func_traits_v<T>> {
};
#endif // defined(PACKIO_HAS_CO_AWAIT) || defined(PACKIO_DOCUMENTATION)

} // traits
} // packio

#endif // PACKIO_TRAITS_H
