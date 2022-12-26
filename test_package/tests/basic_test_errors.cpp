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
        !std::is_same_v<rpc_type, packio::msgpack_rpc::rpc>;
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

#define EXPECT_ERROR_MESSAGE(message, procedure, ...)             \
    EXPECT_ERROR_EQ(                                              \
        this->client_->async_call(                                \
            procedure, std::make_tuple(__VA_ARGS__), use_future), \
        message);

    // clang-format off
    EXPECT_ERROR_MESSAGE(kErrorMessage, "error");
    EXPECT_ERROR_MESSAGE("unknown error", "empty_error");
    EXPECT_ERROR_MESSAGE("call finished with no result", "no_result");
    EXPECT_ERROR_MESSAGE("unknown function", "unexisting");
    EXPECT_ERROR_MESSAGE("cannot convert arguments: invalid type for argument 1", "add", 1, "two");
    EXPECT_ERROR_MESSAGE("cannot convert arguments: no value for argument 0", "add");
    EXPECT_ERROR_MESSAGE("cannot convert arguments: too many arguments", "add", 1, 2, 3);
    EXPECT_ERROR_MESSAGE("cannot convert arguments: invalid type for argument 1", "add_sync", 1, "two");
    EXPECT_ERROR_MESSAGE("cannot convert arguments: no value for argument 0", "add_sync");
    EXPECT_ERROR_MESSAGE("cannot convert arguments: too many arguments", "add_sync", 1, 2, 3);

    if constexpr (has_named_args) {
        EXPECT_ERROR_MESSAGE("cannot convert arguments: unexpected argument a", "add", arg("a") = 1, arg("b") = 2);
        EXPECT_ERROR_MESSAGE("cannot convert arguments: unexpected argument c", "add_named", arg("c") = 1, arg("d") = 2);
        EXPECT_ERROR_MESSAGE("cannot convert arguments: unexpected argument c", "add_named", arg("a") = 1, arg("c") = 2);
        EXPECT_ERROR_MESSAGE("cannot convert arguments: unexpected argument c", "add_named", arg("c") = 1, arg("b") = 2);
        EXPECT_ERROR_MESSAGE("cannot convert arguments: no value for argument b", "add_named", arg("a") = 1);
        EXPECT_ERROR_MESSAGE("cannot convert arguments: unexpected argument c", "add_named", arg("c") = 1);
    }
    // clang-format on
#undef EXPECT_ERROR_MESSAGE
}
