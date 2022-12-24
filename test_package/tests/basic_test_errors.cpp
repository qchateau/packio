#include "basic_test.h"

using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace packio::arg_literals;
using namespace packio::net;
using namespace packio;

TYPED_TEST(BasicTest, test_errors)
{
    using completion_handler =
        typename std::decay_t<decltype(*this)>::completion_handler;
    using rpc_type = typename std::decay_t<decltype(*this)>::client_type::rpc_type;
    constexpr bool has_named_args =
        std::is_same_v<rpc_type, packio::nl_json_rpc::rpc>;
    const std::string kErrorMessage{"error message"};

    this->server_->async_serve_forever();
    this->async_run();
    this->connect();

    ASSERT_TRUE(this->server_->dispatcher()->add_async(
        "error",
        [&](completion_handler handler) { handler.set_error(kErrorMessage); }));
    ASSERT_TRUE(this->server_->dispatcher()->add_async(
        "empty_error", [](completion_handler handler) { handler.set_error(); }));
    ASSERT_TRUE(this->server_->dispatcher()->add_async(
        "no_result", [&](completion_handler) {}));
    ASSERT_TRUE(this->server_->dispatcher()->add_async(
        "add", [](completion_handler handler, int a, int b) { handler(a + b); }));
    ASSERT_TRUE(this->server_->dispatcher()->add(
        "add_sync", [](int a, int b) { return a + b; }));
    ASSERT_TRUE(this->server_->dispatcher()->add(
        "add_named", {"a", "b"}, [](int a, int b) { return a + b; }));

    // clang-format off
    EXPECT_ERROR_MESSAGE(this->client_, kErrorMessage, "error");
    EXPECT_ERROR_MESSAGE(this->client_, "unknown error", "empty_error");
    EXPECT_ERROR_MESSAGE(this->client_, "call finished with no result", "no_result");
    EXPECT_ERROR_MESSAGE(this->client_, "unknown function", "unexisting");
    EXPECT_ERROR_MESSAGE(this->client_, "Incompatible arguments", "add", 1, "two");
    EXPECT_ERROR_MESSAGE(this->client_, "Incompatible arguments", "add");
    EXPECT_ERROR_MESSAGE(this->client_, "Incompatible arguments", "add", 1, 2, 3);
    EXPECT_ERROR_MESSAGE(this->client_, "Incompatible arguments", "add_sync", 1, "two");
    EXPECT_ERROR_MESSAGE(this->client_, "Incompatible arguments", "add_sync");
    EXPECT_ERROR_MESSAGE(this->client_, "Incompatible arguments", "add_sync", 1, 2, 3);

    if constexpr (has_named_args) {
        EXPECT_ERROR_MESSAGE(this->client_, "Incompatible arguments", "add", arg("a") = 1, arg("b") = 2);
        EXPECT_ERROR_MESSAGE(this->client_, "Incompatible arguments", "add_named", arg("c") = 1, arg("d") = 2);
        EXPECT_ERROR_MESSAGE(this->client_, "Incompatible arguments", "add_named", arg("a") = 1, arg("c") = 2);
        EXPECT_ERROR_MESSAGE(this->client_, "Incompatible arguments", "add_named", arg("c") = 1, arg("b") = 2);
        EXPECT_ERROR_MESSAGE(this->client_, "Incompatible arguments", "add_named", arg("a") = 1);
        EXPECT_ERROR_MESSAGE(this->client_, "Incompatible arguments", "add_named", arg("c") = 1);
    }
    // clang-format on
}
