// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef PACKIO_TRAITS_H
#define PACKIO_TRAITS_H

#include <tuple>

namespace packio {
namespace internal {

template <typename T>
struct func_traits : func_traits<decltype(&std::decay<T>::type::operator())> {
};

template <typename C, typename R, typename... Args>
struct func_traits<R (C::*)(Args...)> : func_traits<R (*)(Args...)> {
};

template <typename C, typename R, typename... Args>
struct func_traits<R (C::*)(Args...) const> : func_traits<R (*)(Args...)> {
};

template <typename R, typename... Args>
struct func_traits<R (*)(Args...)> {
    using result_type = R;
    using args_type = std::tuple<Args...>;
};

} // internal
} // packio

#endif // PACKIO_TRAITS_H