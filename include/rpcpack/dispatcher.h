#ifndef RPCPACK_DISPATCHER_H
#define RPCPACK_DISPATCHER_H

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <tuple>

#include <msgpack.hpp>

#include "error_code.h"
#include "handler.h"
#include "internal/noop_mutex.h"
#include "internal/traits.h"
#include "internal/utils.h"

namespace rpcpack {
namespace internal {

template <typename F, typename Tuple>
decltype(auto) apply_msgpack_result(F&& fct, Tuple&& args_tuple)
{
    const auto bound = [&] {
        return std::apply(std::forward<F>(fct), std::forward<Tuple>(args_tuple));
    };

    if constexpr (std::is_void_v<decltype(bound())>) {
        bound();
        return msgpack::type::nil_t{};
    }
    else {
        return bound();
    }
}

} // internal

template <typename mutex>
class dispatcher {
public:
    using completion_handler_type =
        std::function<void(boost::system::error_code, msgpack::object_handle)>;
    using function_type =
        std::function<void(completion_handler_type, const msgpack::object&)>;
    using function_ptr_type = std::shared_ptr<function_type>;

    template <typename F>
    bool add(std::string_view name, F&& fct)
    {
        std::unique_lock lock{map_mutex_};
        return function_map_.emplace(name, wrap_sync(std::forward<F>(fct))).second;
    }

    template <typename F>
    bool add_async(std::string_view name, F&& fct)
    {
        std::unique_lock lock{map_mutex_};
        return function_map_.emplace(name, wrap_async(std::forward<F>(fct))).second;
    }

    bool remove(const std::string& name)
    {
        std::unique_lock lock{map_mutex_};
        return function_map_.erase(name);
    }

    bool has(const std::string& name) const
    {
        std::unique_lock lock{map_mutex_};
        return function_map_.find(name) != function_map_.end();
    }

    size_t clear()
    {
        std::unique_lock lock{map_mutex_};
        size_t size = function_map_.size();
        function_map_.clear();
        return size;
    }

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
    template <typename F>
    function_ptr_type wrap_sync(F&& fct)
    {
        using value_args = typename internal::func_traits<F>::args_type;
        using result_type = typename internal::func_traits<F>::result_type;
        constexpr std::size_t n_value_args = std::tuple_size_v<value_args>;

        return std::make_shared<function_type>(
            [fct = std::forward<F>(fct)](
                completion_handler_type handler, const msgpack::object& args) {
                if (internal::args_count(args) != n_value_args) {
                    DEBUG("incompatible argument count");
                    handler(make_error_code(error::incompatible_arguments), {});
                    return;
                }

                try {
                    if constexpr (std::is_void_v<result_type>) {
                        std::apply(fct, args.as<value_args>());
                        handler(make_error_code(error::success), {});
                    }
                    else {
                        auto result = std::apply(fct, args.as<value_args>());
                        handler(
                            make_error_code(error::success),
                            internal::make_msgpack_object(std::move(result)));
                    }
                }
                catch (std::bad_cast&) {
                    DEBUG("incompatible arguments");
                    handler(make_error_code(error::incompatible_arguments), {});
                }
                catch (std::exception& exc) {
                    DEBUG("exception: {}", exc.what());
                    handler(
                        make_error_code(error::exception_during_call),
                        internal::make_msgpack_object(exc.what()));
                }
            });
    }

    template <typename F>
    function_ptr_type wrap_async(F&& fct)
    {
        using args = typename internal::func_traits<F>::args_type;
        constexpr std::size_t n_args = std::tuple_size_v<args>;

        static_assert(
            n_args >= 1,
            "Callbacks take at least a completion handler as argument");

        using handler_type = std::tuple_element_t<0, args>;
        using handler_args =
            typename internal::func_traits<handler_type>::args_type;

        static_assert(
            std::tuple_size_v<handler_args> == 0
                || std::tuple_size_v<handler_args> == 1,
            "Completion handlers take 0 or 1 arguments");

        using value_args = internal::shift_tuple_t<args>;
        constexpr std::size_t n_value_args = std::tuple_size_v<value_args>;

        return std::make_shared<function_type>(
            [fct = std::forward<F>(fct)](
                completion_handler_type handler, const msgpack::object& args) {
                if (internal::args_count(args) != n_value_args) {
                    DEBUG("incompatible argument count");
                    handler(make_error_code(error::incompatible_arguments), {});
                    return;
                }

                auto user_completion_handler = [&] {
                    if constexpr (std::tuple_size_v<handler_args>) {
                        using result_type = std::tuple_element_t<0, handler_args>;
                        return [handler](result_type result) {
                            handler(
                                make_error_code(error::success),
                                internal::make_msgpack_object(std::move(result)));
                        };
                    }
                    else {
                        return [handler]() {
                            handler(make_error_code(error::success), {});
                        };
                    }
                }();

                const auto bound_fct = [&](auto&&... args) -> void {
                    fct(std::move(user_completion_handler),
                        std::forward<decltype(args)>(args)...);
                };

                try {
                    std::apply(bound_fct, args.as<value_args>());
                }
                catch (std::bad_cast&) {
                    DEBUG("incompatible arguments");
                    handler(make_error_code(error::incompatible_arguments), {});
                }
                catch (std::exception& exc) {
                    DEBUG("exception: {}", exc.what());
                    handler(
                        make_error_code(error::exception_during_call),
                        internal::make_msgpack_object(exc.what()));
                }
            });
    }

    mutable mutex map_mutex_;
    std::unordered_map<std::string, function_ptr_type> function_map_;
};

using default_dispatcher = dispatcher<std::mutex>;
using unsafe_dispatcher = dispatcher<internal::noop_mutex>;

} // rpcpack

#endif // RPCPACK_DISPATCHER_H
