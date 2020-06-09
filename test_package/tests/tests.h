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
        packio::client<packio::asio::local::stream_protocol::socket>,
        packio::server<packio::asio::local::stream_protocol::acceptor>>,
#endif // defined(PACKIO_HAS_LOCAL_SOCKETS)
    std::pair<
        packio::client<packio::asio::ip::tcp::socket>,
        packio::server<packio::asio::ip::tcp::acceptor>>,
    std::pair<
        packio::client<packio::asio::ip::tcp::socket>,
        packio::server<packio::asio::ip::tcp::acceptor, packio::dispatcher<std::map, my_spinlock>>>,
    std::pair<
        packio::client<packio::asio::ip::tcp::socket, my_unordered_map>,
        packio::server<packio::asio::ip::tcp::acceptor>>>
    Implementations;

template <class Impl>
class Test : public ::testing::Test {
protected:
    using client_type = typename Impl::first_type;
    using server_type = typename Impl::second_type;
    using protocol_type = typename client_type::protocol_type;
    using endpoint_type = typename protocol_type::endpoint;
    using socket_type = typename protocol_type::socket;
    using acceptor_type = typename protocol_type::acceptor;

    Test()
        : server_{std::make_shared<server_type>(
            acceptor_type(io_, get_endpoint<endpoint_type>()))},
          client_{std::make_shared<client_type>(socket_type{io_})}
    {
    }

    ~Test()
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

    packio::asio::io_context io_;
    std::shared_ptr<server_type> server_;
    std::shared_ptr<client_type> client_;
    std::thread runner_;
};

TYPED_TEST_SUITE(Test, Implementations);
