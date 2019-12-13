#ifndef RPCPACK_TRAITS_H
#define RPCPACK_TRAITS_H

#include <tuple>

namespace rpcpack {
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
} // rpcpack

#endif // RPCPACK_TRAITS_H