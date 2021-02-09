#pragma once

#include <atomic>
#include <chrono>
#include <future>

#include <gtest/gtest.h>

#include <packio/packio.h>

#include "misc.h"

using MtImplementations = ::testing::Types<
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
class MtTest : public ::testing::Test {
protected:
    using client_type = typename Impl::first_type;
    using server_type = typename Impl::second_type;
    using protocol_type = typename client_type::protocol_type;
    using endpoint_type = typename protocol_type::endpoint;
    using socket_type = typename client_type::socket_type;
    using acceptor_type = typename server_type::acceptor_type;

    MtTest()
        : server_{std::make_shared<server_type>(
            acceptor_type(io_, get_endpoint<endpoint_type>()))}
    {
        server_->async_serve_forever();
    }

    ~MtTest()
    {
        io_.stop();
        for (auto& runner : runners_) {
            runner.join();
        }
    }

    void run(int threads)
    {
        for (int i = 0; i < threads; ++i) {
            runners_.emplace_back([this] { EXPECT_NO_THROW(io_.run()); });
        }
    }

    endpoint_type local_endpoint()
    {
        return server_->acceptor().local_endpoint();
    }

    std::vector<std::shared_ptr<client_type>> create_clients(int n)
    {
        std::vector<std::shared_ptr<client_type>> clients;
        for (int i = 0; i < n; ++i) {
            clients.emplace_back(std::make_shared<client_type>(socket_type{io_}));
        }
        return clients;
    }

    std::vector<std::shared_ptr<client_type>> create_connected_clients(int n)
    {
        std::vector<std::shared_ptr<client_type>> clients = create_clients(n);
        for (auto& client : clients) {
            client->socket().connect(local_endpoint());
        }
        return clients;
    }

    packio::net::io_context io_;
    std::shared_ptr<server_type> server_;
    std::vector<std::thread> runners_;
};

TYPED_TEST_SUITE(MtTest, MtImplementations);
