// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef PACKIO_UNIQUE_FUNCTION_H
#define PACKIO_UNIQUE_FUNCTION_H

#include <functional>
#include <type_traits>
#include <utility>

namespace packio {
namespace internal {

template <typename T>
class movable_function : public std::function<T> {
    template <typename Fn, typename En = void>
    struct wrapper {
    };

    // specialization for CopyConstructible Fn
    template <typename Fn>
    struct wrapper<Fn, std::enable_if_t<std::is_copy_constructible<Fn>::value>> {
        Fn fn;

        template <typename... Args>
        auto operator()(Args&&... args)
        {
            return fn(std::forward<Args>(args)...);
        }
    };

    // specialization for MoveConstructible-only Fn
    template <typename Fn>
    struct wrapper<
        Fn,
        std::enable_if_t<
            !std::is_copy_constructible<Fn>::value
            && std::is_move_constructible<Fn>::value>> {
        Fn fn;

        wrapper(Fn&& fn) : fn(std::forward<Fn>(fn)) {}

        wrapper(wrapper&&) = default;
        wrapper& operator=(wrapper&&) = default;

        // these two functions are instantiated by std::function and are never called
        wrapper(const wrapper& rhs) : fn(const_cast<Fn&&>(rhs.fn))
        {
            // hack to initialize fn for non-DefaultContructible types
            std::abort();
        }
        wrapper& operator=(wrapper&) { std::abort(); }

        template <typename... Args>
        auto operator()(Args&&... args)
        {
            return fn(std::forward<Args>(args)...);
        }
    };

    using base = std::function<T>;

public:
    movable_function() noexcept = default;
    movable_function(std::nullptr_t) noexcept : base(nullptr) {}

    template <typename Fn>
    movable_function(Fn&& f) : base(wrapper<Fn>{std::forward<Fn>(f)})
    {
    }

    movable_function(movable_function&&) = default;
    movable_function& operator=(movable_function&&) = default;

    movable_function& operator=(std::nullptr_t)
    {
        base::operator=(nullptr);
        return *this;
    }

    template <typename Fn>
    movable_function& operator=(Fn&& f)
    {
        base::operator=(wrapper<Fn>{std::forward<Fn>(f)});
        return *this;
    }

    using base::operator();
};

} // internal
} // packio

#endif // PACKIO_UNIQUE_FUNCTION_H
