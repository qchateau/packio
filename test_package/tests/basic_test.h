#pragma once

#include <atomic>
#include <chrono>
#include <future>
#include <unordered_map>

#include <gtest/gtest.h>

#include <packio/packio.h>

#include "misc.h"

typedef ::testing::Types<
#if defined(PACKIO_HAS_LOCAL_SOCKETS)
    std::pair<
        packio::msgpack_rpc::client<packio::net::local::stream_protocol::socket>,
        packio::msgpack_rpc::server<packio::net::local::stream_protocol::acceptor>>,
#endif // defined(PACKIO_HAS_LOCAL_SOCKETS)
    std::pair<
        packio::msgpack_rpc::client<packio::net::ip::tcp::socket>,
        packio::msgpack_rpc::server<packio::net::ip::tcp::acceptor>>,
    std::pair<
        packio::nl_json_rpc::client<packio::net::ip::tcp::socket>,
        packio::nl_json_rpc::server<packio::net::ip::tcp::acceptor>>,
    std::pair<
        packio::msgpack_rpc::client<packio::net::ip::tcp::socket>,
        packio::msgpack_rpc::server<
            packio::net::ip::tcp::acceptor,
            packio::msgpack_rpc::dispatcher<std::map, my_spinlock>>>,
    std::pair<
        packio::msgpack_rpc::client<packio::net::ip::tcp::socket, my_unordered_map>,
        packio::msgpack_rpc::server<packio::net::ip::tcp::acceptor>>>
    Implementations;

template <class Impl>
class BasicTest : public ::testing::Test {
protected:
    using client_type = typename Impl::first_type;
    using server_type = typename Impl::second_type;
    using protocol_type = typename client_type::protocol_type;
    using endpoint_type = typename protocol_type::endpoint;
    using socket_type = typename protocol_type::socket;
    using acceptor_type = typename protocol_type::acceptor;
    using completion_handler =
        packio::completion_handler<typename client_type::rpc_type>;

    BasicTest()
        : server_{std::make_shared<server_type>(
            acceptor_type(io_, get_endpoint<endpoint_type>()))},
          client_{std::make_shared<client_type>(socket_type{io_})}
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
        runner_ = std::thread{[this] { io_.run(); }};
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

TYPED_TEST_SUITE(BasicTest, Implementations);
