// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef PACKIO_ARG_H
#define PACKIO_ARG_H

//! @file
//! Class @ref packio::arg "arg"

#include <string>
#include <string_view>

namespace packio {

//! A named argument
class arg {
public:
    //! A named argument with a value
    template <typename T>
    struct with_value {
        std::string name;
        T value;
    };

    explicit constexpr arg(std::string_view name) : name_{name} {}
    std::string_view name() const { return name_; }

    template <typename T>
    constexpr with_value<T> operator=(T&& value)
    {
        return {std::string{name_}, std::forward<T>(value)};
    }

private:
    std::string_view name_;
};

template <typename T>
struct is_arg_impl : std::false_type {
};

template <typename T>
struct is_arg_impl<arg::with_value<T>> : std::true_type {
};

template <typename T>
struct is_arg : is_arg_impl<std::decay_t<T>> {
};

template <typename T>
constexpr bool is_arg_v = is_arg<T>::value;

//! @namespace arg_literals
//! Namespace containing string literals to define arguments
namespace arg_literals {

constexpr arg operator"" _arg(const char* str, std::size_t)
{
    return arg{str};
}

} // arg_literals

} // packio

#endif // PACKIO_INTERNAL_ARG_H
