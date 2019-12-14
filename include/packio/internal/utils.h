#ifndef PACKIO_UTILS_H
#define PACKIO_UTILS_H

#include <sstream>
#include <string_view>
#include <vector>

#include <boost/asio.hpp>
#include <msgpack.hpp>

#include "log.h"

namespace packio {
namespace internal {

template <typename T>
struct shift_tuple;

template <typename A, typename... Bs>
struct shift_tuple<std::tuple<A, Bs...>> {
    using type = std::tuple<Bs...>;
};

template <typename T>
using shift_tuple_t = typename shift_tuple<T>::type;

template <typename T>
struct asio_buffer {
    using type = const T&;
};

template <>
struct asio_buffer<msgpack::sbuffer> {
    using type = boost::asio::const_buffer;
};

template <>
struct asio_buffer<msgpack::vrefbuffer> {
    using type = std::vector<boost::asio::const_buffer>;
};

template <typename Buffer>
inline typename asio_buffer<Buffer>::type buffer_to_asio(const Buffer& buffer)
{
    return buffer;
}

template <>
inline asio_buffer<msgpack::sbuffer>::type buffer_to_asio(const msgpack::sbuffer& buf)
{
    return boost::asio::const_buffer(buf.data(), buf.size());
}

template <>
inline asio_buffer<msgpack::vrefbuffer>::type buffer_to_asio(
    const msgpack::vrefbuffer& buf)
{
    typename asio_buffer<msgpack::vrefbuffer>::type vec;
    vec.reserve(buf.vector_size());
    const struct iovec* iov = buf.vector();
    for (std::size_t i = 0; i < buf.vector_size(); ++i) {
        vec.push_back(boost::asio::const_buffer(iov->iov_base, iov->iov_len));
        ++iov;
    }
    return vec;
}

inline ssize_t args_count(const msgpack::object& args)
{
    if (args.type != msgpack::type::ARRAY) {
        return -1;
    }
    return args.via.array.size;
}

template <typename T>
msgpack::object_handle make_msgpack_object(T&& value)
{
    msgpack::object_handle oh({}, std::make_unique<msgpack::zone>());
    oh.set(msgpack::object(std::forward<T>(value), *oh.zone()));
    return oh;
}

template <typename T>
void set_no_delay(T& socket)
{
    if constexpr (std::is_same_v<typename T::protocol_type, boost::asio::ip::tcp>) {
        socket.set_option(boost::asio::ip::tcp::no_delay{true});
    }
}

template <typename T>
class movable_function : public std::function<T> {
    template <typename Fn, typename En = void>
    struct wrapper;

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

        // these two functions are instantiated by std::function
        // and are never called
        wrapper(const wrapper& rhs) : fn(const_cast<Fn&&>(rhs.fn)) { throw 0; }
        // hack to initialize fn for non-DefaultContructible types
        wrapper& operator=(wrapper&) { throw 0; }

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

template <typename T>
using function = movable_function<T>;

} // internal
} // packio

#endif // PACKIO_UTILS_H
