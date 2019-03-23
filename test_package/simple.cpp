#include <atomic>
#include <chrono>
#include <future>

#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

#include <rpcpack/simple_client.h>
#include <rpcpack/simple_server.h>

#include "misc.h"

using namespace std::chrono;
using namespace boost::asio;
using namespace rpcpack;
using std::this_thread::sleep_for;

typedef ::testing::Types<
    simple_client<ip::tcp, std::chrono::steady_clock>,
    simple_client<local::stream_protocol, std::chrono::steady_clock>>
    ClientImplementations;

template <class T>
class Client : public ::testing::Test {
protected:
    using client_type = T;
    using protocol_type = typename T::protocol_type;
    using endpoint_type = typename T::endpoint_type;
    using server_type = simple_server<protocol_type, default_dispatcher>;

    Client() : server_{get_endpoint<endpoint_type>()}, client_{}
    {
        server_.async_run();
    }

    void connect()
    {
        auto ep = server_.acceptor().local_endpoint();
        client_.connect(ep);
    }

    server_type server_;
    client_type client_;
};

TYPED_TEST_CASE(Client, ClientImplementations);

TYPED_TEST(Client, test_connect)
{
    this->connect();
    ASSERT_TRUE(this->client_.socket().is_open());
}

TYPED_TEST(Client, test_typical_usage)
{
    this->connect();

    std::atomic<int> call_arg_received{0};
    latch call_latch{0};
    this->server_.add("echo", [&](int i) {
        call_arg_received = i;
        call_latch.count_down();
        return i;
    });

    {
        call_latch.reset(1);

        this->client_.notify("echo", 42);
        ASSERT_TRUE(call_latch.wait_for(std::chrono::seconds{1}));
        ASSERT_EQ(42, call_arg_received.load());
    }

    {
        call_latch.reset(1);
        call_arg_received = 0;

        ASSERT_EQ(42, this->client_.call("echo", 42).template as<int>());
        ASSERT_EQ(42, call_arg_received.load());
    }
}

TYPED_TEST(Client, test_timeout)
{
    this->connect();

    std::mutex mtx;
    std::condition_variable cv;
    bool blocked{true};

    this->server_.add("block", [&] {
        std::unique_lock l{mtx};
        cv.wait(l, [&] { return !blocked; });
    });

    this->client_.set_timeout(std::chrono::milliseconds{1});
    ASSERT_THROW(this->client_.call("block"), boost::system::system_error);
}

TYPED_TEST(Client, test_dispatcher)
{
    this->connect();

    ASSERT_TRUE(this->server_.add("f001", []() -> void {}));
    ASSERT_TRUE(this->server_.add("f002", []() -> void {}));
    ASSERT_FALSE(this->server_.add("f001", []() -> void {}));
    this->client_.call("f001");
    this->client_.call("f002");
    ASSERT_TRUE(this->server_.has("f001"));
    ASSERT_TRUE(this->server_.has("f002"));
    ASSERT_FALSE(this->server_.has("f003"));
    auto known = this->server_.known();
    ASSERT_EQ(
        (std::set<std::string>{"f001", "f002"}),
        std::set<std::string>(begin(known), end(known)));

    this->server_.remove("f001");
    ASSERT_THROW(this->client_.call("f001"), boost::system::system_error);

    ASSERT_FALSE(this->server_.has("f001"));
    ASSERT_TRUE(this->server_.has("f002"));
    ASSERT_FALSE(this->server_.has("f003"));

    ASSERT_EQ(1, this->server_.clear());

    ASSERT_FALSE(this->server_.has("f001"));
    ASSERT_FALSE(this->server_.has("f002"));
    ASSERT_FALSE(this->server_.has("f003"));
}

int main(int argc, char** argv)
{
    ::spdlog::default_logger()->set_level(::spdlog::level::trace);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
