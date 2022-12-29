#pragma once

#include <atomic>
#include <chrono>
#include <future>
#include <unordered_map>

#include <gtest/gtest.h>

#include <packio/packio.h>

#include "misc.h"

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
        runner_ = std::thread{[this] {
            try {
                io_.run();
            }
            catch (const std::exception& exc) {
                ASSERT_TRUE(false)
                    << "io_context threw an exception: " << exc.what();
            }
        }};
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

TYPED_TEST_SUITE(BasicTest, test_implementations);
