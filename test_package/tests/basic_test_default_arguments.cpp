#include "basic_test.h"
#include <packio/arg_specs.h>

using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace packio::arg_literals;
using namespace packio::net;
using namespace packio;

TYPED_TEST(BasicTest, test_default_arguments)
{
    using completion_handler =
        typename std::decay_t<decltype(*this)>::completion_handler;
    using rpc_type = typename std::decay_t<decltype(*this)>::client_type::rpc_type;
    constexpr bool has_named_args =
        std::is_same_v<rpc_type, packio::nl_json_rpc::rpc>;

    this->server_->async_serve_forever();
    this->async_run();
    this->connect();

    auto const adder = [](int a, int b) { return a + b; };
    this->server_->dispatcher()->add(
        "add_first_default",
        {
            arg_spec<int>("a") = 10,
            arg_spec<int>("b"),
        },
        adder);

    this->server_->dispatcher()->add(
        "add_second_default",
        {
            arg_spec<int>("a"),
            arg_spec<int>("b") = 100,
        },
        adder);

    this->server_->dispatcher()->add(
        "add_all_default",
        {
            arg_spec<int>("a") = 1000,
            arg_spec<int>("b") = 10000,
        },
        adder);

    auto const async_adder = [](completion_handler c, int a, int b) { c(a + b); };
    this->server_->dispatcher()->add_async(
        "async_add_first_default",
        {
            arg_spec<int>("a") = 10,
            arg_spec<int>("b"),
        },
        async_adder);

    this->server_->dispatcher()->add_async(
        "async_add_second_default",
        {
            arg_spec<int>("a"),
            arg_spec<int>("b") = 100,
        },
        async_adder);

    this->server_->dispatcher()->add_async(
        "async_add_all_default",
        {
            arg_spec<int>("a") = 1000,
            arg_spec<int>("b") = 10000,
        },
        async_adder);

#if defined(PACKIO_HAS_CO_AWAIT) || defined(PACKIO_FORCE_COROUTINES)
    auto const coro_adder = [](int a, int b) -> net::awaitable<int> {
        co_return a + b;
    };
    this->server_->dispatcher()->add_coro(
        "coro_add_first_default",
        this->io_,
        {
            arg_spec<int>("a") = 10,
            arg_spec<int>("b"),
        },
        coro_adder);

    this->server_->dispatcher()->add_coro(
        "coro_add_second_default",
        this->io_,
        {
            arg_spec<int>("a"),
            arg_spec<int>("b") = 100,
        },
        coro_adder);

    this->server_->dispatcher()->add_coro(
        "coro_add_all_default",
        this->io_,
        {
            arg_spec<int>("a") = 1000,
            arg_spec<int>("b") = 10000,
        },
        coro_adder);
#endif // defined(PACKIO_HAS_CO_AWAIT) || defined(PACKIO_FORCE_COROUTINES)

    std::vector<std::string> prefixes = {"", "async_"};
#if defined(PACKIO_HAS_CO_AWAIT) || defined(PACKIO_FORCE_COROUTINES)
    prefixes.push_back("coro_");
#endif // defined(PACKIO_HAS_CO_AWAIT) || defined(PACKIO_FORCE_COROUTINES)

    for (const auto& prefix : prefixes) {
        // -- add_all_default --
        EXPECT_RESULT_EQ(
            this->client_->async_call(
                prefix + "add_all_default", std::make_tuple(), use_future),
            11000);
        EXPECT_RESULT_EQ(
            this->client_->async_call(
                prefix + "add_all_default", std::make_tuple(12), use_future),
            10012);
        EXPECT_RESULT_EQ(
            this->client_->async_call(
                prefix + "add_all_default", std::make_tuple(12, 13), use_future),
            25);
        EXPECT_ERROR_MESSAGE(
            this->client_,
            "cannot convert arguments: too many arguments",
            prefix + "add_all_default",
            1,
            2,
            3);

        // -- add_first_default --
        EXPECT_ERROR_MESSAGE(
            this->client_,
            "Incompatible arguments",
            prefix + "add_first_default");
        EXPECT_ERROR_MESSAGE(
            this->client_,
            "Incompatible arguments",
            prefix + "add_first_default",
            1);
        EXPECT_RESULT_EQ(
            this->client_->async_call(
                prefix + "add_first_default", std::make_tuple(12, 13), use_future),
            25);
        EXPECT_ERROR_MESSAGE(
            this->client_,
            "cannot convert arguments: too many arguments",
            prefix + "add_first_default",
            1,
            2,
            3);

        // -- add_second_default --
        EXPECT_ERROR_MESSAGE(
            this->client_,
            "Incompatible arguments",
            prefix + "add_second_default");
        EXPECT_RESULT_EQ(
            this->client_->async_call(
                prefix + "add_second_default", std::make_tuple(12), use_future),
            112);
        EXPECT_RESULT_EQ(
            this->client_->async_call(
                prefix + "add_second_default", std::make_tuple(12, 13), use_future),
            25);
        EXPECT_ERROR_MESSAGE(
            this->client_,
            "cannot convert arguments: too many arguments",
            prefix + "add_second_default",
            1,
            2,
            3);

        if constexpr (has_named_args) {
            // -- add_all_default --
            EXPECT_RESULT_EQ(
                this->client_->async_call(
                    prefix + "add_all_default",
                    std::make_tuple(arg("a") = 12),
                    use_future),
                10012);
            EXPECT_RESULT_EQ(
                this->client_->async_call(
                    prefix + "add_all_default",
                    std::make_tuple(arg("b") = 13),
                    use_future),
                1013);
            EXPECT_RESULT_EQ(
                this->client_->async_call(
                    prefix + "add_all_default",
                    std::make_tuple(arg("a") = 12, arg("b") = 13),
                    use_future),
                25);
            EXPECT_ERROR_MESSAGE(
                this->client_,
                "cannot convert arguments: too many arguments",
                prefix + "add_all_default",
                arg("a") = 1,
                arg("b") = 2,
                arg("c") = 3);

            // -- add_first_default --
            EXPECT_ERROR_MESSAGE(
                this->client_,
                "Incompatible arguments",
                prefix + "add_first_default",
                arg("a") = 12);
            EXPECT_RESULT_EQ(
                this->client_->async_call(
                    prefix + "add_first_default",
                    std::make_tuple(arg("b") = 13),
                    use_future),
                23);
            EXPECT_RESULT_EQ(
                this->client_->async_call(
                    prefix + "add_first_default",
                    std::make_tuple(arg("a") = 12, arg("b") = 13),
                    use_future),
                25);
            EXPECT_ERROR_MESSAGE(
                this->client_,
                "cannot convert arguments: too many arguments",
                prefix + "add_first_default",
                arg("a") = 1,
                arg("b") = 2,
                arg("c") = 3);

            // -- add_second_default --
            EXPECT_RESULT_EQ(
                this->client_->async_call(
                    prefix + "add_second_default",
                    std::make_tuple(arg("a") = 12),
                    use_future),
                112);
            EXPECT_ERROR_MESSAGE(
                this->client_,
                "Incompatible arguments",
                prefix + "add_second_default",
                arg("b") = 13);
            EXPECT_RESULT_EQ(
                this->client_->async_call(
                    prefix + "add_second_default",
                    std::make_tuple(arg("a") = 12, arg("b") = 13),
                    use_future),
                25);
            EXPECT_ERROR_MESSAGE(
                this->client_,
                "cannot convert arguments: too many arguments",
                prefix + "add_second_default",
                arg("a") = 1,
                arg("b") = 2,
                arg("c") = 3);
        }
    }
}
