#ifndef RPCPACK_HANDLER_H
#define RPCPACK_HANDLER_H

#include <functional>

namespace rpcpack {
namespace internal {

template <typename Ret>
struct completion_handler {
    using type = std::function<void(Ret)>;
};

template <>
struct completion_handler<void> {
    using type = std::function<void()>;
};
}

template <typename Ret = void>
using completion_handler = typename internal::completion_handler<Ret>::type;

} // rpcpack

#endif // RPCPACK_HANDLER_H