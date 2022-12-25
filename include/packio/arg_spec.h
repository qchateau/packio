// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef PACKIO_ARG_SPECS_H
#define PACKIO_ARG_SPECS_H

//! @file
//! Class @ref packio::arg_spec "arg_spec"

#include <optional>
#include <stdexcept>
#include <string>

#include "internal/utils.h"

namespace packio {

template <typename DefaultType>
class arg_spec {
public:
    arg_spec(std::string name) : name_(std::move(name)) {}
    arg_spec(std::string name, DefaultType default_value)
        : name_(std::move(name)), default_value_(std::move(default_value))
    {
    }

    arg_spec& operator=(DefaultType default_value)
    {
        default_value_ = std::move(default_value);
    }

    const std::string& name() const { return name_; }
    const std::optional<DefaultType>& default_value() const
    {
        return default_value_;
    }

private:
    std::string name_;
    std::optional<DefaultType> default_value_;
};

template <typename F>
using arg_specs_for_t =
    std::conditional_t<internal::is_coroutine_v<F>, typename Iftrue, typename Iffalse>

    template <typename... ArgSpecs>
    class arg_specs : public std::tuple<ArgSpecs...> {
public:
    // default arg specs are arguments called "0", "1", ... and no default value
    arg_specs() : std::tuple<ArgSpecs...>(iota()) {}

private:
    template <std::size_t... Idxs>
    static std::tuple<ArgSpecs...> iota(std::index_sequence<Idxs...>)
    {
        return {std::to_string(Idxs)...};
    }

    static std::tuple<ArgSpecs...> iota()
    {
        return iota(std::make_index_sequence<sizeof...(ArgSpecs)>());
    }
};

template <typename F>
using arg_specs_for_t = internal::map_tuple_t<
    arg_spec,
    internal::decay_tuple_t<typename internal::func_traits<F>::args_type>>;

template <typename F>
using arg_specs_for_async_t = internal::left_shift_tuple_t<arg_specs_for_t<F>>;

template <typename F>
using arg_specs_for_coro_t = arg_specs_for_t<F>;

} // packio

#endif // PACKIO_ARG_SPECS_H
