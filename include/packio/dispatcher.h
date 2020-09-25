// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef PACKIO_DISPATCHER_H
#define PACKIO_DISPATCHER_H

//! @file
//! Class @ref packio::dispatcher "dispatcher"

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <tuple>

#include "handler.h"
#include "internal/config.h"
#include "internal/movable_function.h"
#include "internal/rpc.h"
#include "internal/utils.h"
#include "traits.h"

namespace packio {

//! The dispatcher class, used to store and dispatch procedures
//! @tparam Rpc RPC protocol implementation
//! @tparam Map The container used to associate procedures to their name
//! @tparam Lockable The lockable used to protect accesses to the procedure map
template <typename Rpc, template <class...> class Map = default_map, typename Lockable = default_mutex>
class dispatcher {
public:
    //! The RPC protocol type
    using rpc_type = Rpc;
    //! The mutex type used to protect the procedure map
    using mutex_type = Lockable;
    //! The type of the arguments used by the RPC protocol
    using args_type = decltype(typename rpc_type::request_type{}.args);
    //! The type of function stored in the dispatcher
    using function_type = internal::movable_function<
        void(completion_handler<rpc_type>, args_type&& args)>;
    //! A shared pointer to @ref function_type
    using function_ptr_type = std::shared_ptr<function_type>;

    //! Add a synchronous procedure to the dispatcher
    //! @param name The name of the procedure
    //! @param arguments_names The name of the arguments (optional)
    //! @param fct The procedure itself
    template <
        typename SyncProcedure,
        std::size_t N = internal::func_traits<SyncProcedure>::args_count>
    bool add(
        std::string_view name,
        const std::array<std::string, N>& arguments_names,
        SyncProcedure&& fct)
    {
        PACKIO_STATIC_ASSERT_TRAIT(SyncProcedure);
        std::unique_lock lock{map_mutex_};
        return function_map_
            .emplace(
                name,
                std::make_shared<function_type>(wrap_sync(
                    std::forward<SyncProcedure>(fct), arguments_names)))
            .second;
    }

    //! @overload
    template <typename SyncProcedure>
    bool add(std::string_view name, SyncProcedure&& fct)
    {
        return add<SyncProcedure, 0>(name, {}, std::forward<SyncProcedure>(fct));
    }

    //! Add an asynchronous procedure to the dispatcher
    //! @param name The name of the procedure
    //! @param arguments_names The name of the arguments (optional)
    //! @param fct The procedure itself
    template <
        typename AsyncProcedure,
        std::size_t N = internal::func_traits<AsyncProcedure>::args_count - 1>
    bool add_async(
        std::string_view name,
        const std::array<std::string, N>& arguments_names,
        AsyncProcedure&& fct)
    {
        PACKIO_STATIC_ASSERT_TTRAIT(AsyncProcedure, rpc_type);
        std::unique_lock lock{map_mutex_};
        return function_map_
            .emplace(
                name,
                std::make_shared<function_type>(wrap_async(
                    std::forward<AsyncProcedure>(fct), arguments_names)))
            .second;
    }

    //! @overload
    template <typename AsyncProcedure>
    bool add_async(std::string_view name, AsyncProcedure&& fct)
    {
        return add_async<AsyncProcedure, 0>(
            name, {}, std::forward<AsyncProcedure>(fct));
    }

#if defined(PACKIO_HAS_CO_AWAIT)
    //! Add a coroutine to the dispatcher
    //! @param name The name of the procedure
    //! @param executor The executor used to execute the coroutine
    //! @param arguments_names The name of the arguments (optional)
    //! @param coro The coroutine to use as procedure
    template <
        typename Executor,
        typename CoroProcedure,
        std::size_t N = internal::func_traits<CoroProcedure>::args_count>
    bool add_coro(
        std::string_view name,
        const Executor& executor,
        const std::array<std::string, N>& arguments_names,
        CoroProcedure&& coro)
    {
        PACKIO_STATIC_ASSERT_TRAIT(CoroProcedure);
        std::unique_lock lock{map_mutex_};
        return function_map_
            .emplace(
                name,
                std::make_shared<function_type>(wrap_coro(
                    executor, std::forward<CoroProcedure>(coro), arguments_names)))
            .second;
    }

    //! @overload
    template <typename Executor, typename CoroProcedure>
    bool add_coro(std::string_view name, const Executor& executor, CoroProcedure&& coro)
    {
        return add_coro<Executor, CoroProcedure, 0>(
            name, executor, {}, std::forward<CoroProcedure>(coro));
    }

    //! @overload
    template <
        typename ExecutionContext,
        typename CoroProcedure,
        std::size_t N = internal::func_traits<CoroProcedure>::args_count>
    bool add_coro(
        std::string_view name,
        ExecutionContext& ctx,
        const std::array<std::string, N>& arguments_names,
        CoroProcedure&& coro)
    {
        return add_coro<decltype(ctx.get_executor()), CoroProcedure, N>(
            name,
            ctx.get_executor(),
            arguments_names,
            std::forward<CoroProcedure>(coro));
    }

    //! @overload
    template <typename ExecutionContext, typename CoroProcedure>
    bool add_coro(std::string_view name, ExecutionContext& ctx, CoroProcedure&& coro)
    {
        return add_coro<ExecutionContext, CoroProcedure, 0>(
            name, ctx, {}, std::forward<CoroProcedure>(coro));
    }
#endif // defined(PACKIO_HAS_CO_AWAIT)

    //! Remove a procedure from the dispatcher
    //! @param name The name of the procedure to remove
    //! @return True if the procedure was removed, False if it was not found
    bool remove(const std::string& name)
    {
        std::unique_lock lock{map_mutex_};
        return function_map_.erase(name);
    }

    //! Check if a procedure is registered
    //! @param name The name of the procedure to check
    //! @return True if the procedure is known
    bool has(const std::string& name) const
    {
        std::unique_lock lock{map_mutex_};
        return function_map_.find(name) != function_map_.end();
    }

    //! Remove all procedures
    //! @return The number of procedures removed
    size_t clear()
    {
        std::unique_lock lock{map_mutex_};
        size_t size = function_map_.size();
        function_map_.clear();
        return size;
    }

    //! Get the name of all known procedures
    //! @return Vector containing the name of all known procedures
    std::vector<std::string> known() const
    {
        std::unique_lock lock{map_mutex_};
        std::vector<std::string> names;
        names.reserve(function_map_.size());
        std::transform(
            function_map_.begin(),
            function_map_.end(),
            std::back_inserter(names),
            [](const typename decltype(function_map_)::value_type& pair) {
                return pair.first;
            });
        return names;
    }

    function_ptr_type get(const std::string& name) const
    {
        std::unique_lock lock{map_mutex_};
        auto it = function_map_.find(name);
        if (it == function_map_.end()) {
            return {};
        }
        else {
            return it->second;
        }
    }

private:
    using function_map_type = Map<std::string, function_ptr_type>;

    template <typename TArgs, std::size_t NNamedArgs>
    static void static_assert_arguments_name_and_count()
    {
        static_assert(
            NNamedArgs == 0 || std::tuple_size_v<TArgs> == NNamedArgs,
            "incompatible arguments count and names");
    }

    template <typename F, std::size_t N>
    auto wrap_sync(F&& fct, const std::array<std::string, N>& args_names)
    {
        using value_args =
            internal::decay_tuple_t<typename internal::func_traits<F>::args_type>;
        using result_type = typename internal::func_traits<F>::result_type;
        static_assert_arguments_name_and_count<value_args, N>();

        return
            [fct = std::forward<F>(fct), args_names](
                completion_handler<rpc_type> handler, args_type&& args) mutable {
                auto typed_args = rpc_type::template extract_args<value_args>(
                    std::move(args), args_names);
                if (!typed_args) {
                    PACKIO_DEBUG("incompatible arguments");
                    handler.set_error("Incompatible arguments");
                    return;
                }

                if constexpr (std::is_void_v<result_type>) {
                    std::apply(fct, std::move(*typed_args));
                    handler();
                }
                else {
                    handler(std::apply(fct, std::move(*typed_args)));
                }
            };
    }

    template <typename F, std::size_t N>
    auto wrap_async(F&& fct, const std::array<std::string, N>& args_names)
    {
        using args = typename internal::func_traits<F>::args_type;
        using value_args = internal::decay_tuple_t<internal::shift_tuple_t<args>>;
        static_assert_arguments_name_and_count<value_args, N>();

        return
            [fct = std::forward<F>(fct), args_names](
                completion_handler<rpc_type> handler, args_type&& args) mutable {
                auto typed_args = rpc_type::template extract_args<value_args>(
                    std::move(args), args_names);
                if (!typed_args) {
                    PACKIO_DEBUG("incompatible arguments");
                    handler.set_error("Incompatible arguments");
                    return;
                }

                std::apply(
                    [&](auto&&... args) {
                        fct(std::move(handler),
                            std::forward<decltype(args)>(args)...);
                    },
                    std::move(*typed_args));
            };
    }

#if defined(PACKIO_HAS_CO_AWAIT)
    template <typename E, typename C, std::size_t N>
    auto wrap_coro(
        const E& executor,
        C&& coro,
        const std::array<std::string, N>& args_names)
    {
        using value_args =
            internal::decay_tuple_t<typename internal::func_traits<C>::args_type>;
        using result_type =
            typename internal::func_traits<C>::result_type::value_type;
        static_assert_arguments_name_and_count<value_args, N>();

        return [executor, coro = std::forward<C>(coro), args_names](
                   completion_handler<rpc_type> handler,
                   args_type&& args) mutable {
            auto typed_args = rpc_type::template extract_args<value_args>(
                std::move(args), args_names);
            if (!typed_args) {
                PACKIO_DEBUG("incompatible arguments");
                handler.set_error("Incompatible arguments");
                return;
            }

            net::co_spawn(
                executor,
                [typed_args = std::move(*typed_args),
                 handler = std::move(handler),
                 coro = std::forward<C>(coro)]() mutable -> net::awaitable<void> {
                    if constexpr (std::is_void_v<result_type>) {
                        co_await std::apply(coro, std::move(typed_args));
                        handler();
                    }
                    else {
                        handler(co_await std::apply(coro, std::move(typed_args)));
                    }
                },
                [](std::exception_ptr exc) {
                    if (exc) {
                        std::rethrow_exception(exc);
                    }
                });
        };
    }
#endif // defined(PACKIO_HAS_CO_AWAIT)

    mutable mutex_type map_mutex_;
    function_map_type function_map_;
};

} // packio

#endif // PACKIO_DISPATCHER_H
