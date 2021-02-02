#pragma once

#include <atomic>
#include <chrono>
#include <future>
#include <unordered_map>

#include <gtest/gtest.h>

#include <packio/packio.h>

#include "misc.h"

using BasicImplementations = ::testing::Types<
#if HAS_BEAST
    std::pair<
        default_rpc::client<test_websocket<true>>,
        default_rpc::server<test_websocket_acceptor<true>>>,
#if PACKIO_HAS_NLOHMANN_JSON
    std::pair<
        packio::nl_json_rpc::client<test_websocket<true>>,
        packio::nl_json_rpc::server<test_websocket_acceptor<true>>>,
    std::pair<
        packio::nl_json_rpc::client<test_websocket<false>>,
        packio::nl_json_rpc::server<test_websocket_acceptor<false>>>,
#endif // PACKIO_HAS_NLOHMANN_JSON
#endif // HAS_BEAST

// FIXME: local socket should work on windows for boost >= 1.75
//  but there is problem with bind at the moment
#if defined(PACKIO_HAS_LOCAL_SOCKETS) && !defined(_WIN32)
    std::pair<
        default_rpc::client<packio::net::local::stream_protocol::socket>,
        default_rpc::server<packio::net::local::stream_protocol::acceptor>>,
#endif // defined(PACKIO_HAS_LOCAL_SOCKETS)

#if PACKIO_HAS_MSGPACK
    std::pair<
        packio::msgpack_rpc::client<packio::net::ip::tcp::socket>,
        packio::msgpack_rpc::server<packio::net::ip::tcp::acceptor>>,
    std::pair<
        packio::msgpack_rpc::client<packio::net::ip::tcp::socket>,
        packio::msgpack_rpc::server<
            packio::net::ip::tcp::acceptor,
            packio::msgpack_rpc::dispatcher<std::map, my_spinlock>>>,
    std::pair<
        packio::msgpack_rpc::client<packio::net::ip::tcp::socket, my_unordered_map>,
        packio::msgpack_rpc::server<packio::net::ip::tcp::acceptor>>
#if PACKIO_HAS_NLOHMANN_JSON || PACKIO_HAS_BOOST_JSON
    ,
#endif // PACKIO_HAS_NLOHMANN_JSON || PACKIO_HAS_BOOST_JSON
#endif // PACKIO_HAS_MSGPACK

#if PACKIO_HAS_NLOHMANN_JSON
    std::pair<
        packio::nl_json_rpc::client<packio::net::ip::tcp::socket>,
        packio::nl_json_rpc::server<packio::net::ip::tcp::acceptor>>
#if PACKIO_HAS_BOOST_JSON
    ,
#endif // PACKIO_HAS_BOOST_JSON
#endif // PACKIO_HAS_NLOHMANN_JSON

#if PACKIO_HAS_BOOST_JSON
    std::pair<
        packio::json_rpc::client<packio::net::ip::tcp::socket>,
        packio::json_rpc::server<packio::net::ip::tcp::acceptor>>
#endif // PACKIO_HAS_BOOST_JSON
    >;

template <class Impl>
class BasicTest : public ::testing::Test {
protected:
    using client_type = typename Impl::first_type;
    using server_type = typename Impl::second_type;
    using protocol_type = typename client_type::protocol_type;
    using endpoint_type = typename protocol_type::endpoint;
    using socket_type = typename client_type::socket_type;
    using acceptor_type = typename server_type::acceptor_type;
    using completion_handler =
        packio::completion_handler<typename client_type::rpc_type>;

    BasicTest()
        : server_{std::make_shared<server_type>(
            acceptor_type(io_, get_endpoint<endpoint_type>()))},
          client_{std::make_shared<client_type>(
              socket_type{io_, endpoint_type().protocol()})}
    {
    }

    ~BasicTest()
    {
        io_.stop();
        if (runner_.joinable()) {
            runner_.join();
        }
    }

    void async_run()
    {
        runner_ = std::thread{[this] { EXPECT_NO_THROW(io_.run()); }};
    }

    void connect()
    {
        auto ep = server_->acceptor().local_endpoint();
        client_->socket().connect(ep);
    }

    packio::net::io_context io_;
    std::shared_ptr<server_type> server_;
    std::shared_ptr<client_type> client_;
    std::thread runner_;
};

TYPED_TEST_SUITE(BasicTest, BasicImplementations);
