#include "basic_test.h"

using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace packio::arg_literals;
using namespace packio::net;
using namespace packio;

TYPED_TEST(BasicTest, test_dispatcher)
{
    using completion_handler =
        typename std::decay_t<decltype(*this)>::completion_handler;

    this->server_->async_serve_forever();
    this->async_run();
    this->connect();

    ASSERT_TRUE(this->server_->dispatcher()->add_async(
        "f001", [](completion_handler handler) { handler(); }));
    ASSERT_TRUE(this->server_->dispatcher()->add("f002", []() {}));

    ASSERT_FALSE(this->server_->dispatcher()->add_async(
        "f001", [](completion_handler handler) { handler(); }));
    ASSERT_FALSE(this->server_->dispatcher()->add_async(
        "f002", [](completion_handler handler) { handler(); }));
    ASSERT_FALSE(this->server_->dispatcher()->add("f001", []() {}));
    ASSERT_FALSE(this->server_->dispatcher()->add("f002", []() {}));

    EXPECT_RESULT_IS_OK(this->client_->async_call("f001", use_future));
    EXPECT_RESULT_IS_OK(this->client_->async_call("f002", use_future));

    ASSERT_TRUE(this->server_->dispatcher()->has("f001"));
    ASSERT_TRUE(this->server_->dispatcher()->has("f002"));
    ASSERT_FALSE(this->server_->dispatcher()->has("f003"));
    auto known = this->server_->dispatcher()->known();
    ASSERT_EQ(
        (std::set<std::string>{"f001", "f002"}),
        std::set<std::string>(begin(known), end(known)));

    this->server_->dispatcher()->remove("f001");
    EXPECT_RESULT_IS_ERROR(this->client_->async_call("f001", use_future));

    ASSERT_FALSE(this->server_->dispatcher()->has("f001"));
    ASSERT_TRUE(this->server_->dispatcher()->has("f002"));
    ASSERT_FALSE(this->server_->dispatcher()->has("f003"));

    ASSERT_EQ(1u, this->server_->dispatcher()->clear());

    ASSERT_FALSE(this->server_->dispatcher()->has("f001"));
    ASSERT_FALSE(this->server_->dispatcher()->has("f002"));
    ASSERT_FALSE(this->server_->dispatcher()->has("f003"));
}
