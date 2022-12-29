#include "basic_test.h"

using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace packio::arg_literals;
using namespace packio::net;
using namespace packio;

TYPED_TEST(BasicTest, test_named_arguments)
{
    using completion_handler =
        typename std::decay_t<decltype(*this)>::completion_handler;
    using rpc_type = typename std::decay_t<decltype(*this)>::client_type::rpc_type;
    constexpr bool has_named_args =
        !std::is_same_v<rpc_type, packio::msgpack_rpc::rpc>;

    this->server_->async_serve_forever();
    this->async_run();
    this->connect();

    this->server_->dispatcher()->add(
        "echo", {"a"}, [](std::string a) { return a; });
    this->server_->dispatcher()->add(
        "concat", {"a", "b"}, [](std::string a, std::string b) { return a + b; });

    this->server_->dispatcher()->add_async(
        "aecho", {"a"}, [](completion_handler c, std::string a) { c(a); });
    this->server_->dispatcher()->add_async(
        "aconcat",
        {"a", "b"},
        [](completion_handler c, std::string a, std::string b) {
            return c(a + b);
        });

#if defined(PACKIO_HAS_CO_AWAIT) || defined(PACKIO_FORCE_COROUTINES)
    this->server_->dispatcher()->add_coro(
        "cecho",
        this->io_,
        {"a"},
        [](std::string a) -> net::awaitable<std::string> { co_return a; });
    this->server_->dispatcher()->add_coro(
        "cconcat",
        this->io_,
        {"a", "b"},
        [](std::string a, std::string b) -> net::awaitable<std::string> {
            co_return a + b;
        });
#endif // defined(PACKIO_HAS_CO_AWAIT) || defined(PACKIO_FORCE_COROUTINES)

    EXPECT_RESULT_EQ(
        this->client_->async_call("echo", std::make_tuple("toto"), use_future),
        "toto"s);
    if constexpr (has_named_args) {
        EXPECT_RESULT_EQ(
            this->client_->async_call(
                "echo", std::tuple{arg("a") = "toto"}, use_future),
            "toto"s);
    }

    EXPECT_RESULT_EQ(
        this->client_->async_call(
            "concat", std::make_tuple("toto", "titi"), use_future),
        "tototiti"s);
    if constexpr (has_named_args) {
        EXPECT_RESULT_EQ(
            this->client_->async_call(
                "concat",
                std::tuple{arg("b") = "titi", "a"_arg = "toto"},
                use_future),
            "tototiti"s);
    }

    EXPECT_RESULT_EQ(
        this->client_->async_call("aecho", std::make_tuple("toto"), use_future),
        "toto"s);
    if constexpr (has_named_args) {
        EXPECT_RESULT_EQ(
            this->client_->async_call(
                "aecho", std::tuple{arg("a") = "toto"}, use_future),
            "toto"s);
    }
    EXPECT_RESULT_EQ(
        this->client_->async_call(
            "aconcat", std::make_tuple("toto", "titi"), use_future),
        "tototiti"s);
    if constexpr (has_named_args) {
        EXPECT_RESULT_EQ(
            this->client_->async_call(
                "aconcat",
                std::tuple{"b"_arg = "titi", arg("a") = "toto"},
                use_future),
            "tototiti"s);
    }

#if defined(PACKIO_HAS_CO_AWAIT) || defined(PACKIO_FORCE_COROUTINES)
    EXPECT_RESULT_EQ(
        this->client_->async_call("cecho", std::make_tuple("toto"), use_future),
        "toto"s);
    if constexpr (has_named_args) {
        EXPECT_RESULT_EQ(
            this->client_->async_call(
                "cecho", std::tuple{"a"_arg = "toto"}, use_future),
            "toto"s);
    }
    EXPECT_RESULT_EQ(
        this->client_->async_call(
            "cconcat", std::make_tuple("toto", "titi"), use_future),
        "tototiti"s);
    if constexpr (has_named_args) {
        EXPECT_RESULT_EQ(
            this->client_->async_call(
                "cconcat",
                std::tuple{"b"_arg = "titi", "a"_arg = "toto"},
                use_future),
            "tototiti"s);
    }
#endif // defined(PACKIO_HAS_CO_AWAIT) || defined(PACKIO_FORCE_COROUTINES)
}
