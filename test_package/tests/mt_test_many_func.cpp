#include "mt_test.h"

using namespace std::chrono_literals;
using namespace packio::net;
using namespace packio;

TYPED_TEST(MtTest, test_many_func)
{
    constexpr int kNCalls{100};
    const int kNThreads = 10 * std::thread::hardware_concurrency();
    const int kNClients = 2 * std::thread::hardware_concurrency();

    latch done{kNCalls * kNClients * 2};
    latch calls{kNCalls * kNClients * 2};
    for (int i = 0; i < kNClients; ++i) {
        this->server_->dispatcher()->add(std::to_string(i), [&](int n) {
            calls.count_down();
            return n;
        });
    }

    this->run(kNThreads);

    auto clients = this->create_connected_clients(kNClients);
    for (int i = 0; i < kNCalls; ++i) {
        int j = 0;
        for (auto& client : clients) {
            std::string name = std::to_string(j++);
            client->async_call(name, std::make_tuple(42), [&](auto ec, auto res) {
                ASSERT_FALSE(ec);
                ASSERT_EQ(42, get<int>(res.result));
                done.count_down();
            });
            client->async_notify(name, std::make_tuple(42), [&](auto ec) {
                ASSERT_FALSE(ec);
                done.count_down();
            });
        }
    }

    ASSERT_TRUE(done.wait_for(10s));
    ASSERT_TRUE(calls.wait_for(10s));
}
