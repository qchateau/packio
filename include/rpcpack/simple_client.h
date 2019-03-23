#ifndef RPCPACK_SIMPLE_CLIENT_H
#define RPCPACK_SIMPLE_CLIENT_H

#include <future>
#include <optional>
#include <string_view>

#include <msgpack.hpp>

#include "basic_client.h"
#include "internal/utils.h"

namespace rpcpack {

template <typename Protocol, typename Clock>
class simple_client {
public:
    using protocol_type = Protocol;
    using clock_type = Clock;
    using duration_type = typename clock_type::duration;
    using socket_type = typename protocol_type::socket;
    using endpoint_type = typename protocol_type::endpoint;
    using impl_type = basic_client<protocol_type, clock_type>;

    simple_client() : client_(socket_type{io_}) {}

    socket_type& socket() { return client_.socket(); }
    const socket_type& socket() const { return client_.socket(); }

    auto connect(endpoint_type ep)
    {
        return boost::asio::connect(socket(), std::array<decltype(ep), 1>{ep});
    }

    template <typename T = protocol_type>
    auto connect(
        std::string_view host,
        uint16_t port,
        typename internal::enable_if_tcp<T>::type* = 0)
        -> decltype(boost::asio::connect(
            *static_cast<socket_type*>(0),
            internal::resolve_ip_address("", 0)))
    {
        return boost::asio::connect(
            socket(), internal::resolve_ip_address(host, port));
    }

    template <typename T = protocol_type>
    auto connect(
        std::string_view host_port,
        typename internal::enable_if_tcp<T>::type* = 0)
        -> decltype(boost::asio::connect(
            *static_cast<socket_type*>(0),
            internal::resolve_ip_address("", 0)))
    {
        auto [host, port] = internal::split_addr_port(host_port);
        return connect(host, port);
    }

    template <typename T = protocol_type>
    auto connect(
        std::string_view path,
        typename internal::enable_if_local<T>::type* = 0)
        -> decltype(boost::asio::connect(
            *static_cast<socket_type*>(0),
            std::array<typename T::endpoint, 1>()))
    {
        typename T::endpoint ep(path);
        return boost::asio::connect(socket(), std::array<decltype(ep), 1>{ep});
    }

    void set_timeout(duration_type timeout) { client_.set_timeout(timeout); }
    duration_type get_timeout() const { return client_.get_timeout(); }

    template <typename... Args>
    msgpack::object call(
        boost::system::error_code& ec_res,
        std::string_view name,
        Args&&... args)
    {
        std::optional<msgpack::object> result;
        client_.template async_call<msgpack::vrefbuffer>(
            [&](boost::system::error_code ec, const msgpack::object& res) {
                ec_res = ec;
                result = res;
            },
            name,
            std::forward<Args>(args)...);
        while (!result) {
            io_.run_one();
        }
        return *result;
    }

    template <typename... Args>
    msgpack::object call(std::string_view name, Args&&... args)
    {
        boost::system::error_code ec;
        auto result = call(ec, name, std::forward<Args>(args)...);
        if (ec) {
            throw boost::system::system_error{ec, "call"};
        }
        return result;
    }

    template <typename... Args>
    void notify(boost::system::error_code& ec_res, std::string_view name, Args&&... args)
    {
        client_.template async_notify<msgpack::vrefbuffer>(
            [&](boost::system::error_code ec) { ec_res = ec; },
            name,
            std::forward<Args>(args)...);
        io_.run();
        io_.restart();
    }

    template <typename... Args>
    void notify(std::string_view name, Args&&... args)
    {
        boost::system::error_code ec;
        notify(ec, name, std::forward<Args>(args)...);
        if (ec) {
            throw boost::system::system_error{ec, "notify"};
        }
    }

private:
    boost::asio::io_context io_;
    impl_type client_;
};

using simple_ip_client =
    simple_client<boost::asio::ip::tcp, std::chrono::steady_clock>;

#if defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)
using simple_local_client =
    simple_client<boost::asio::local::stream_protocol, std::chrono::steady_clock>;
#endif // defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)

} // rpcpack

#endif // RPCPACK_SIMPLE_CLIENT_H