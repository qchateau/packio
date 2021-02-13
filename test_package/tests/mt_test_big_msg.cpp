#include "mt_test.h"

using namespace std::chrono_literals;
using namespace packio::net;
using namespace packio;

TYPED_TEST(MtTest, test_big_msg)
{
    constexpr int kNCalls{100};
    const int kNThreads = 10 * std::thread::hardware_concurrency();
    const int kNClients = 2 * std::thread::hardware_concurrency();
    const std::string big_msg(100'000, '0');

    latch done{kNCalls * kNClients};
    latch calls{kNCalls * kNClients};
    this->server_->dispatcher()->add("echo", [&](std::string s) {
        EXPECT_EQ(big_msg, s);
        calls.count_down();
        return s;
    });
    this->run(kNThreads);
    auto clients = this->create_connected_clients(kNClients);

    for (int i = 0; i < kNCalls; ++i) {
        for (auto& client : clients) {
            client->async_call(
                "echo", std::make_tuple(big_msg), [&](auto ec, auto res) {
                    EXPECT_FALSE(ec);
                    EXPECT_EQ(big_msg, get<std::string>(res.result));
                    done.count_down();
                });
        }
    }

    ASSERT_TRUE(done.wait_for(30s));
    ASSERT_TRUE(calls.wait_for(30s));
}
