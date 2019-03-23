#ifndef RPCPACK_UTILS_H
#define RPCPACK_UTILS_H

#include <sstream>
#include <string_view>
#include <vector>

#include <boost/asio.hpp>
#include <msgpack.hpp>

#include "log.h"

namespace rpcpack {
namespace internal {

template <typename T>
struct asio_buffer {
    using type = const T&;
};

template <typename Buffer>
typename asio_buffer<Buffer>::type buffer_to_asio(const Buffer& buffer)
{
    return buffer;
}

template <>
struct asio_buffer<msgpack::sbuffer> {
    using type = boost::asio::const_buffer;
};

template <>
asio_buffer<msgpack::sbuffer>::type buffer_to_asio(const msgpack::sbuffer& buf)
{
    return boost::asio::const_buffer(buf.data(), buf.size());
}

template <>
struct asio_buffer<msgpack::vrefbuffer> {
    using type = std::vector<boost::asio::const_buffer>;
};

template <>
asio_buffer<msgpack::vrefbuffer>::type buffer_to_asio(const msgpack::vrefbuffer& buf)
{
    typename asio_buffer<msgpack::vrefbuffer>::type vec;
    vec.reserve(buf.vector_size());
    const struct iovec* iov = buf.vector();
    for (int i = 0; i < buf.vector_size(); ++i) {
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

inline std::tuple<std::string, uint16_t> split_addr_port(
    std::string_view bind_addr_port)
{
    size_t sep_pos = bind_addr_port.find(':');
    if (sep_pos >= bind_addr_port.size() - 1) {
        throw std::invalid_argument("port not found");
    }
    std::string bind_addr{bind_addr_port.substr(0, sep_pos)};
    uint16_t port = std::stoi(std::string{bind_addr_port.substr(sep_pos + 1)});
    return {bind_addr, port};
}

inline boost::asio::ip::tcp::endpoint make_tcp_endpoint(
    std::string_view bind_addr,
    uint16_t port)
{
    return {boost::asio::ip::make_address(bind_addr), port};
}

inline boost::asio::ip::tcp::endpoint make_tcp_endpoint(std::string_view bind_addr_port)
{
    auto [bind_addr, port] = split_addr_port(bind_addr_port);
    return make_tcp_endpoint(bind_addr, port);
}

inline boost::asio::local::stream_protocol::endpoint make_local_endpoint(
    std::string_view path)
{
    return {path};
}

inline auto resolve_ip_address(std::string_view host, uint16_t port)
    -> decltype(static_cast<boost::asio::ip::tcp::resolver*>(0)->resolve("", ""))
{
    boost::asio::io_context io;
    return boost::asio::ip::tcp::resolver(io).resolve(host, std::to_string(port));
}

template <typename protocol, typename T = void>
struct enable_if_tcp {
};

template <typename T>
struct enable_if_tcp<boost::asio::ip::tcp, T> {
    using type = T;
};

template <typename protocol, typename T = void>
struct enable_if_local {
};

template <typename T>
struct enable_if_local<boost::asio::local::stream_protocol, T> {
    using type = T;
};

template <typename T>
struct shift_tuple;

template <typename A, typename... Bs>
struct shift_tuple<std::tuple<A, Bs...>> {
    using type = std::tuple<Bs...>;
};

template <typename T>
using shift_tuple_t = typename shift_tuple<T>::type;

template <typename T>
msgpack::object_handle make_msgpack_object(T&& value)
{
    msgpack::object_handle oh({}, std::make_unique<msgpack::zone>());
    oh.set(msgpack::object(std::forward<T>(value), *oh.zone()));
    return oh;
}

} // internal
} // rpcpack

#endif // RPCPACK_UTILS_H
