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

#include "args_specs.h"
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
    //! @param arg_specs The argument specifications (optional)
    //! @param fct The procedure itself
    template <typename SyncProcedure>
    bool add(
        std::string_view name,
        args_specs<SyncProcedure> arg_specs,
        SyncProcedure&& fct)
    {
        PACKIO_STATIC_ASSERT_TRAIT(SyncProcedure);
        std::unique_lock lock{map_mutex_};
        return function_map_
            .emplace(
                name,
                std::make_shared<function_type>(wrap_sync(
                    std::forward<SyncProcedure>(fct), std::move(arg_specs))))
            .second;
    }

    //! @overload
    template <typename SyncProcedure>
    bool add(std::string_view name, SyncProcedure&& fct)
    {
        return add<SyncProcedure>(name, {}, std::forward<SyncProcedure>(fct));
    }

    //! Add an asynchronous procedure to the dispatcher
    //! @param name The name of the procedure
    //! @param arg_specs The argument specifications (optional)
    //! @param fct The procedure itself
    template <typename AsyncProcedure>
    bool add_async(
        std::string_view name,
        args_specs<AsyncProcedure> arg_specs,
        AsyncProcedure&& fct)
    {
        PACKIO_STATIC_ASSERT_TTRAIT(AsyncProcedure, rpc_type);
        std::unique_lock lock{map_mutex_};
        return function_map_
            .emplace(
                name,
                std::make_shared<function_type>(wrap_async(
                    std::forward<AsyncProcedure>(fct), std::move(arg_specs))))
            .second;
    }

    //! @overload
    template <typename AsyncProcedure>
    bool add_async(std::string_view name, AsyncProcedure&& fct)
    {
        return add_async<AsyncProcedure>(
            name, {}, std::forward<AsyncProcedure>(fct));
    }

#if defined(PACKIO_HAS_CO_AWAIT)
    //! Add a coroutine to the dispatcher
    //! @param name The name of the procedure
    //! @param executor The executor used to execute the coroutine
    //! @param arg_specs The argument specifications (optional)
    //! @param coro The coroutine to use as procedure
    template <
        typename Executor,
        typename CoroProcedure,
        std::size_t N = internal::func_traits<CoroProcedure>::args_count>
    bool add_coro(
        std::string_view name,
        const Executor& executor,
        args_specs<CoroProcedure> arg_specs,
        CoroProcedure&& coro)
    {
        PACKIO_STATIC_ASSERT_TRAIT(CoroProcedure);
        std::unique_lock lock{map_mutex_};
        return function_map_
            .emplace(
                name,
                std::make_shared<function_type>(wrap_coro(
                    executor,
                    std::forward<CoroProcedure>(coro),
                    std::move(arg_specs))))
            .second;
    }

    //! @overload
    template <typename Executor, typename CoroProcedure>
    bool add_coro(std::string_view name, const Executor& executor, CoroProcedure&& coro)
    {
        return add_coro<Executor, CoroProcedure>(
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
        args_specs<CoroProcedure> arg_specs,
        CoroProcedure&& coro)
    {
        return add_coro<decltype(ctx.get_executor()), CoroProcedure, N>(
            name,
            ctx.get_executor(),
            std::move(arg_specs),
            std::forward<CoroProcedure>(coro));
    }

    //! @overload
    template <typename ExecutionContext, typename CoroProcedure>
    bool add_coro(std::string_view name, ExecutionContext& ctx, CoroProcedure&& coro)
    {
        return add_coro<ExecutionContext, CoroProcedure>(
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

    template <typename F>
    auto wrap_sync(F&& fct, args_specs<F> args_specs)
    {
        using value_args =
            internal::decay_tuple_t<typename internal::func_traits<F>::args_type>;
        using result_type = typename internal::func_traits<F>::result_type;

        return
            [fct = std::forward<F>(fct), args_specs = std::move(args_specs)](
                completion_handler<rpc_type> handler, args_type&& args) mutable {
                auto typed_args = rpc_type::template extract_args<value_args>(
                    std::move(args), args_specs);
                if (!typed_args) {
                    PACKIO_DEBUG(typed_args.error());
                    handler.set_error(typed_args.error());
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

    template <typename F>
    auto wrap_async(F&& fct, args_specs<F> args_specs)
    {
        using args = typename internal::func_traits<F>::args_type;
        using value_args =
            internal::decay_tuple_t<internal::left_shift_tuple_t<args>>;

        return
            [fct = std::forward<F>(fct), args_specs = std::move(args_specs)](
                completion_handler<rpc_type> handler, args_type&& args) mutable {
                auto typed_args = rpc_type::template extract_args<value_args>(
                    std::move(args), args_specs);
                if (!typed_args) {
                    PACKIO_DEBUG(typed_args.error());
                    handler.set_error(typed_args.error());
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
    template <typename E, typename C>
    auto wrap_coro(const E& executor, C&& coro, args_specs<C> args_specs)
    {
        using value_args =
            internal::decay_tuple_t<typename internal::func_traits<C>::args_type>;
        using result_type =
            typename internal::func_traits<C>::result_type::value_type;

        return [executor,
                coro = std::forward<C>(coro),
                args_specs = std::move(args_specs)](
                   completion_handler<rpc_type> handler,
                   args_type&& args) mutable {
            auto typed_args = rpc_type::template extract_args<value_args>(
                std::move(args), args_specs);
            if (!typed_args) {
                PACKIO_DEBUG(typed_args.error());
                handler.set_error(typed_args.error());
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
