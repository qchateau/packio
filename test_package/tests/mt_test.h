#pragma once

#include <atomic>
#include <chrono>
#include <future>

#include <gtest/gtest.h>

#include <packio/packio.h>

#include "misc.h"

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

TYPED_TEST_SUITE(MtTest, test_implementations);
