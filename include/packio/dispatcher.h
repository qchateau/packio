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

template <typename mutex = std::mutex>
class dispatcher {
public:
    using function_type =
        std::function<void(completion_handler, const msgpack::object&)>;
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

        auto fct_ptr = std::make_shared<std::decay_t<F>>(std::forward<F>(fct));
        return std::make_shared<function_type>([fct_ptr = std::move(fct_ptr)](
                                                   completion_handler handler,
                                                   const msgpack::object& args) {
            if (internal::args_count(args) != std::tuple_size_v<value_args>) {
                // keep this check otherwise msgpack unpacker
                // may silently drop arguments
                DEBUG("incompatible argument count");
                handler.set_error("Incompatible arguments");
                return;
            }

            try {
                if constexpr (std::is_void_v<result_type>) {
                    std::apply(*fct_ptr, args.as<value_args>());
                    handler();
                }
                else {
                    handler(std::apply(*fct_ptr, args.as<value_args>()));
                }
            }
            catch (msgpack::type_error&) {
                DEBUG("incompatible arguments");
                handler.set_error("Incompatible arguments");
            }
        });
    }

    template <typename F>
    function_ptr_type wrap_async(F&& fct)
    {
        using args = typename internal::func_traits<F>::args_type;
        using value_args = internal::shift_tuple_t<args>;

        auto fct_ptr = std::make_shared<std::decay_t<F>>(std::forward<F>(fct));
        return std::make_shared<function_type>([fct_ptr = std::move(fct_ptr)](
                                                   completion_handler handler,
                                                   const msgpack::object& args) {
            if (internal::args_count(args) != std::tuple_size_v<value_args>) {
                // keep this check otherwise msgpack unpacker
                // may silently drop arguments
                DEBUG("incompatible argument count");
                handler.set_error("Incompatible arguments");
                return;
            }

            const auto bound_fct = [&](auto&&... args) -> void {
                (*fct_ptr)(
                    std::move(handler), std::forward<decltype(args)>(args)...);
            };

            try {
                std::apply(bound_fct, args.as<value_args>());
            }
            catch (msgpack::type_error&) {
                DEBUG("incompatible arguments");
                handler.set_error("Incompatible arguments");
            }
        });
    }

    mutable mutex map_mutex_;
    std::unordered_map<std::string, function_ptr_type> function_map_;
};

} // packio

#endif // PACKIO_DISPATCHER_H
