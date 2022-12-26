// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef PACKIO_ARGS_SPECS_H
#define PACKIO_ARGS_SPECS_H

//! @file
//! Class @ref packio::args_specs "args_specs"

#include <optional>
#include <stdexcept>
#include <string>

#include "arg.h"
#include "handler.h"
#include "internal/utils.h"

namespace packio {
namespace internal {

template <typename DefaultType>
class arg_spec {
public:
    explicit arg_spec(std::string name) : name_(std::move(name)) {}
    explicit arg_spec(const arg& arg) : name_(arg.name()) {}
    explicit arg_spec(arg::with_value<DefaultType> arg)
        : name_(std::move(arg.name)), default_value_(std::move(arg.value))
    {
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
using arg_specs_tuple_for_sync_procedure_t =
    map_tuple_t<arg_spec, decay_tuple_t<typename func_traits<F>::args_type>>;

template <typename F>
using arg_specs_tuple_for_async_procedure_t =
    left_shift_tuple_t<arg_specs_tuple_for_sync_procedure_t<F>>;

template <typename, bool>
struct arg_specs_tuple_for;

template <typename F>
struct arg_specs_tuple_for<F, true> {
    using type = arg_specs_tuple_for_async_procedure_t<F>;
};

template <typename F>
struct arg_specs_tuple_for<F, false> {
    using type = arg_specs_tuple_for_sync_procedure_t<F>;
};

template <typename F>
using arg_specs_tuple_for_t =
    typename arg_specs_tuple_for<F, is_async_procedure_v<F>>::type;

template <typename T>
struct args_specs_maker;

template <typename... Specs>
struct args_specs_maker<std::tuple<Specs...>> {
    template <typename... Args>
    static std::tuple<Specs...> make(Args&&... args)
    {
        if constexpr (sizeof...(Args) == 0) {
            // default arg specs are arguments called "0", "1", ... and no default value
            return iota(std::make_index_sequence<sizeof...(Specs)>());
        }
        else {
            static_assert(
                sizeof...(Args) == sizeof...(Specs),
                "arguments specification must either match the number of "
                "arguments or be empty");
            return {Specs{std::forward<Args>(args)}...};
        }
    }

    template <std::size_t... Idxs>
    static std::tuple<Specs...> iota(std::index_sequence<Idxs...>)
    {
        return {Specs{std::to_string(Idxs)}...};
    }
};

//! Options available for the argument specifications
//!
//! You are never expected to construct this structure yourself
//! but rather construct them by combining options such as
//! @ref packio::allow_extra_arguments "allow_extra_arguments"
//! with operator|
struct args_specs_options {
    bool allow_extra_arguments = false;

    constexpr args_specs_options operator|(const args_specs_options& other)
    {
        auto ret = *this;
        ret.allow_extra_arguments |= other.allow_extra_arguments;
        return ret;
    }
};

template <typename SpecsTuple>
class args_specs {
public:
    args_specs() : args_specs(args_specs_options{}){};

    template <
        typename T,
        typename... Args,
        typename = std::enable_if_t<!std::is_same_v<std::decay_t<T>, args_specs_options>>>
    args_specs(T&& arg0, Args&&... args)
        : args_specs(
            args_specs_options{},
            std::forward<T>(arg0),
            std::forward<Args>(args)...)
    {
    }

    template <typename... Args>
    args_specs(args_specs_options opts, Args&&... args)
        : specs_{args_specs_maker<SpecsTuple>::make(std::forward<Args>(args)...)},
          opts_{std::move(opts)}
    {
    }

    constexpr const args_specs_options& options() const { return opts_; }

    template <std::size_t I>
    constexpr decltype(auto) get() const
    {
        return std::get<I>(specs_);
    }

    static constexpr std::size_t size()
    {
        return std::tuple_size_v<SpecsTuple>;
    }

private:
    SpecsTuple specs_;
    args_specs_options opts_{};
};
} // internal

//! Procedure arguments specifications
//! @tparam The procedure
//!
//! CLass that describes the arguments of the procedure
//! They each have a name and optionally a default value
//! This is a tuple-like class where each element can be
//! constructured from:
//! - a string, defining the name of the argument
//! - a @ref arg, defining the name of the argument
//! - a @ref arg::with_value, defining the name of the argyment
//!   and its default value
//! Optionally, accepts
//! @ref packio::internal::args_specs_options "args_specs_options"
//! as first argument to customize the behavior of argument handling
template <typename Procedure>
class args_specs
    // Using the real implementation as the base class reduces
    // the number of templates instanciation
    : public internal::args_specs<internal::arg_specs_tuple_for_t<Procedure>> {
public:
    using base = internal::args_specs<internal::arg_specs_tuple_for_t<Procedure>>;
    using base::base;
};

//! Option to allo extra arguments, ignoring them
constexpr auto allow_extra_arguments = internal::args_specs_options{true};

} // packio

#endif // PACKIO_ARGS_SPECS_H
