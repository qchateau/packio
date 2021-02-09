#include "mt_test.h"

using namespace std::chrono_literals;
using namespace packio::net;
using namespace packio;

TYPED_TEST(MtTest, test_same_func)
{
    constexpr int kNCalls{100};
    const int kNThreads = 10 * std::thread::hardware_concurrency();
    const int kNClients = 2 * std::thread::hardware_concurrency();

    latch done{kNCalls * kNClients};
    latch calls{kNCalls * kNClients};
    this->server_->dispatcher()->add("double", [&](int i) {
        calls.count_down();
        return 2 * i;
    });
    this->run(kNThreads);
    auto clients = this->create_connected_clients(kNClients);
    for (int i = 0; i < kNCalls; ++i) {
        for (auto& client : clients) {
            client->async_call(
                "double", std::make_tuple(42), [&](auto ec, auto res) {
                    EXPECT_FALSE(ec);
                    EXPECT_EQ(84, get<int>(res.result));
                    done.count_down();
                });
        }
    }

    ASSERT_TRUE(done.wait_for(10s));
    ASSERT_TRUE(calls.wait_for(10s));
}
