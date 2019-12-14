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

} // internal
} // packio

#endif // PACKIO_UTILS_H
