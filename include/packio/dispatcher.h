#ifndef PACKIO_DISPATCHER_H
#define PACKIO_DISPATCHER_H

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <tuple>

#include <msgpack.hpp>

#include "error_code.h"
#include "handler.h"
#include "internal/traits.h"
#include "internal/utils.h"

namespace packio {
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

template <typename mutex = std::mutex>
class dispatcher {
public:
    using function_type = internal::function<
        void(completion_handler::raw::function_type, const msgpack::object&)>;
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

        return std::make_shared<function_type>(
            [fct = std::forward<F>(fct)](
                completion_handler::raw::function_type handler_function,
                const msgpack::object& args) {
                completion_handler::raw handler{std::move(handler_function)};

                if (internal::args_count(args) != std::tuple_size_v<value_args>) {
                    // keep this check otherwise msgpack unpacker
                    // may silently drop arguments
                    DEBUG("incompatible argument count");
                    handler.set_error("Incompatible arguments");
                    return;
                }

                try {
                    if constexpr (std::is_void_v<result_type>) {
                        std::apply(fct, args.as<value_args>());
                        handler();
                    }
                    else {
                        handler(std::apply(fct, args.as<value_args>()));
                    }
                }
                catch (std::bad_cast& exc) {
                    DEBUG("incompatible arguments");
                    handler.set_error("Incompatible arguments");
                }
                catch (std::exception& exc) {
                    DEBUG("exception: {}", exc.what());
                    handler.set_error(exc.what());
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

        using value_args = internal::shift_tuple_t<args>;

        return std::make_shared<function_type>(
            [fct = std::forward<F>(fct)](
                completion_handler::raw::function_type handler_function,
                const msgpack::object& args) {
                auto handler = std::make_shared<completion_handler::raw>(
                    std::move(handler_function));

                if (internal::args_count(args) != std::tuple_size_v<value_args>) {
                    // keep this check otherwise msgpack unpacker
                    // may silently drop arguments
                    DEBUG("incompatible argument count");
                    handler->set_error("Incompatible arguments");
                    return;
                }

                const auto bound_fct = [&](auto&&... args) -> void {
                    fct(completion_handler{handler},
                        std::forward<decltype(args)>(args)...);
                };

                try {
                    std::apply(bound_fct, args.as<value_args>());
                }
                catch (std::bad_cast&) {
                    DEBUG("incompatible arguments");
                    handler->set_error("Incompatible arguments");
                }
                catch (std::exception& exc) {
                    DEBUG("exception: {}", exc.what());
                    handler->set_error(exc.what());
                }
            });
    }

    mutable mutex map_mutex_;
    std::unordered_map<std::string, function_ptr_type> function_map_;
};

} // packio

#endif // PACKIO_DISPATCHER_H
