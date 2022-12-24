#include "basic_test.h"

using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace packio::arg_literals;
using namespace packio::net;
using namespace packio;

TYPED_TEST(BasicTest, test_args_types)
{
    this->server_->async_serve_forever();
    this->async_run();
    this->connect();

    this->server_->dispatcher()->add("add", [](int a, int b) { return a + b; });

    // rvalue tuple
    EXPECT_RESULT_EQ(
        this->client_->async_call("add", std::tuple{12, 23}, use_future), 35);

    // lvalue tuple
    std::tuple tup{12, 23};
    EXPECT_RESULT_EQ(this->client_->async_call("add", tup, use_future), 35);

    // rvalue array
    EXPECT_RESULT_EQ(
        this->client_->async_call("add", std::array{12, 23}, use_future), 35);

    // lvalue array
    std::array arr{12, 23};
    EXPECT_RESULT_EQ(this->client_->async_call("add", arr, use_future), 35);

    // rvalue pair
    EXPECT_RESULT_EQ(
        this->client_->async_call("add", std::pair{12, 23}, use_future), 35);

    // lvalue pair
    std::pair pair{12, 23};
    EXPECT_RESULT_EQ(this->client_->async_call("add", pair, use_future), 35);
}
