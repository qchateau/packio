#ifndef RPCPACK_NOOP_MUTEX_H
#define RPCPACK_NOOP_MUTEX_H

namespace rpcpack {
namespace internal {

class noop_mutex {
public:
    void lock() noexcept {}
    void unlock() noexcept {}
};

} // internal
} // rpcpack

#endif // RPCPACK_NOOP_MUTEX_H