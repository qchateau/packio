#include <atomic>
#include <chrono>
#include <future>

#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

#include <packio/client.h>
#include <packio/server.h>

#include "misc.h"

using namespace std::chrono;
using namespace boost::asio;
using namespace packio;
using std::this_thread::sleep_for;

typedef ::testing::Types<
#if defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)
    boost::asio::local::stream_protocol,
#endif // defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)
    boost::asio::ip::tcp>
    Protocols;

template <class Protocol>
class Server : public ::testing::Test {
protected:
    using server_type = server<Protocol>;
    using client_type = client<Protocol>;
    using endpoint_type = typename Protocol::endpoint;
    using socket_type = typename Protocol::socket;
    using acceptor_type = typename Protocol::acceptor;

    Server() : server_{acceptor_type(io_, get_endpoint<endpoint_type>())}
    {
        server_.async_serve_forever();
    }

    ~Server()
    {
        io_.stop();
        for (auto& runner : runners_) {
            runner.join();
        }
    }

    void run(int threads)
    {
        for (int i = 0; i < threads; ++i) {
            runners_.emplace_back([this] { io_.run(); });
        }
    }

    endpoint_type local_endpoint()
    {
        return server_.acceptor().local_endpoint();
    }

    std::list<client_type> create_clients(int n)
    {
        std::list<client_type> clients;
        for (int i = 0; i < n; ++i) {
            clients.emplace_back(socket_type{io_});
        }
        return clients;
    }

    std::list<client_type> create_connected_clients(int n)
    {
        std::list<client_type> clients = create_clients(n);
        for (auto& client : clients) {
            client.socket().connect(local_endpoint());
        }
        return clients;
    }

    boost::asio::io_context io_;
    server_type server_;
    std::vector<std::thread> runners_;
};

TYPED_TEST_CASE(Server, Protocols);

TYPED_TEST(Server, test_same_func)
{
    constexpr int kNCalls{100};
    const int kNThreads = 10 * std::thread::hardware_concurrency();
    const int kNClients = 2 * std::thread::hardware_concurrency();

    latch done{kNCalls * kNClients};
    latch calls{kNCalls * kNClients};
    this->server_.dispatcher()->add("double", [&](int i) {
        calls.count_down();
        return 2 * i;
    });
    this->run(kNThreads);
    auto clients = this->create_connected_clients(kNClients);
    for (int i = 0; i < kNCalls; ++i) {
        for (auto& client : clients) {
            client.async_call(
                "double", std::make_tuple(42), [&](auto ec, const auto& result) {
                    ASSERT_FALSE(ec);
                    ASSERT_EQ(84, result.template as<int>());
                    done.count_down();
                });
        }
    }

    ASSERT_TRUE(done.wait_for(std::chrono::seconds{10}));
    ASSERT_TRUE(calls.wait_for(std::chrono::seconds{10}));
}

TYPED_TEST(Server, test_many_func)
{
    constexpr int kNCalls{100};
    const int kNThreads = 10 * std::thread::hardware_concurrency();
    const int kNClients = 2 * std::thread::hardware_concurrency();

    latch done{kNCalls * kNClients};
    latch calls{kNCalls * kNClients};
    for (int i = 0; i < kNClients; ++i) {
        this->server_.dispatcher()->add(std::to_string(i), [&](int n) {
            calls.count_down();
            return n;
        });
    }

    this->run(kNThreads);

    auto clients = this->create_connected_clients(kNClients);
    for (int i = 0; i < kNCalls; ++i) {
        int j = 0;
        for (auto& client : clients) {
            client.async_call(
                std::to_string(j++),
                std::make_tuple(42),
                [&](auto ec, const auto& result) {
                    ASSERT_FALSE(ec);
                    ASSERT_EQ(42, result.template as<int>());
                    done.count_down();
                });
        }
    }

    ASSERT_TRUE(done.wait_for(std::chrono::seconds{10}));
    ASSERT_TRUE(calls.wait_for(std::chrono::seconds{10}));
}

int main(int argc, char** argv)
{
    ::spdlog::default_logger()->set_level(::spdlog::level::trace);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
