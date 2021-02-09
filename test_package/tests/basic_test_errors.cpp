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

#define ASSERT_ERROR_MESSAGE(message, procedure, ...)                         \
    {                                                                         \
        std::promise<std::string> p;                                          \
        auto f = p.get_future();                                              \
                                                                              \
        this->client_->async_call(                                            \
            procedure, std::make_tuple(__VA_ARGS__), [&](auto ec, auto res) { \
                ASSERT_FALSE(ec);                                             \
                ASSERT_TRUE(is_error_response(res));                          \
                p.set_value(get_error_message(res.error));                    \
            });                                                               \
                                                                              \
        ASSERT_EQ(message, safe_future_get(std::move(f)));                    \
    };

    ASSERT_ERROR_MESSAGE(kErrorMessage, "error");
    ASSERT_ERROR_MESSAGE("Unknown error", "empty_error");
    ASSERT_ERROR_MESSAGE("Call finished with no result", "no_result");
    ASSERT_ERROR_MESSAGE("Unknown function", "unexisting");
    ASSERT_ERROR_MESSAGE("Incompatible arguments", "add", 1, "two");
    ASSERT_ERROR_MESSAGE("Incompatible arguments", "add");
    ASSERT_ERROR_MESSAGE("Incompatible arguments", "add", 1, 2, 3);
    ASSERT_ERROR_MESSAGE("Incompatible arguments", "add_sync", 1, "two");
    ASSERT_ERROR_MESSAGE("Incompatible arguments", "add_sync");
    ASSERT_ERROR_MESSAGE("Incompatible arguments", "add_sync", 1, 2, 3);

    if constexpr (has_named_args) {
        ASSERT_ERROR_MESSAGE(
            "Incompatible arguments", "add", arg("a") = 1, arg("b") = 2);
        ASSERT_ERROR_MESSAGE(
            "Incompatible arguments", "add_named", arg("c") = 1, arg("d") = 2);
        ASSERT_ERROR_MESSAGE(
            "Incompatible arguments", "add_named", arg("a") = 1, arg("c") = 2);
        ASSERT_ERROR_MESSAGE(
            "Incompatible arguments", "add_named", arg("c") = 1, arg("b") = 2);
        ASSERT_ERROR_MESSAGE("Incompatible arguments", "add_named", arg("a") = 1);
        ASSERT_ERROR_MESSAGE("Incompatible arguments", "add_named", arg("c") = 1);
    }

#undef ASSERT_ERROR_MESSAGE
}
