#include "basic_test.h"

using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace packio::arg_literals;
using namespace packio::net;
using namespace packio;

TYPED_TEST(BasicTest, test_typical_usage)
{
    using completion_handler =
        typename std::decay_t<decltype(*this)>::completion_handler;

    {
        latch connected{1};
        this->server_->async_serve([&](auto ec, auto session) {
            ASSERT_FALSE(ec);
            session->start();
            connected.count_down();
        });
        this->async_run();
        this->connect();

        ASSERT_TRUE(this->client_->socket().is_open());
        ASSERT_TRUE(connected.wait_for(1s));
    }

    std::atomic<int> call_arg_received{0};
    latch call_latch{0};
    this->server_->dispatcher()->add_async(
        "echo", [&](completion_handler handler, int i) {
            call_arg_received = i;
            call_latch.count_down();
            handler(i);
        });

    {
        call_latch.reset(1);

        auto f = this->client_->async_notify("echo", std::tuple{42}, use_future);
        EXPECT_FUTURE_NO_THROW(f);
        ASSERT_TRUE(call_latch.wait_for(1s));
        ASSERT_EQ(42, call_arg_received.load());
    }

    {
        call_latch.reset(1);
        call_arg_received = 0;

        auto f = this->client_->async_call("echo", std::tuple{42}, use_future);
        EXPECT_RESULT_EQ(f, 42);
        ASSERT_EQ(42, call_arg_received.load());
    }
}
