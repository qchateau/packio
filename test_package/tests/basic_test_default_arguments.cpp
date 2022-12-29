#include "basic_test.h"

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
        !std::is_same_v<rpc_type, packio::msgpack_rpc::rpc>;

    this->server_->async_serve_forever();
    this->async_run();
    this->connect();

    auto const adder = [](int a, int b) { return a + b; };
    this->server_->dispatcher()->add(
        "add_first_default",
        {
            "a"_arg = 10,
            "b"_arg,
        },
        adder);

    this->server_->dispatcher()->add(
        "add_second_default",
        {
            "a"_arg,
            "b"_arg = 100,
        },
        adder);

    this->server_->dispatcher()->add(
        "add_all_default",
        {
            "a"_arg = 1000,
            "b"_arg = 10000,
        },
        adder);

    auto const async_adder = [](completion_handler c, int a, int b) { c(a + b); };
    this->server_->dispatcher()->add_async(
        "async_add_first_default",
        {
            "a"_arg = 10,
            "b"_arg,
        },
        async_adder);

    this->server_->dispatcher()->add_async(
        "async_add_second_default",
        {
            "a"_arg,
            "b"_arg = 100,
        },
        async_adder);

    this->server_->dispatcher()->add_async(
        "async_add_all_default",
        {
            "a"_arg = 1000,
            "b"_arg = 10000,
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
            "a"_arg = 10,
            "b"_arg,
        },
        coro_adder);

    this->server_->dispatcher()->add_coro(
        "coro_add_second_default",
        this->io_,
        {
            "a"_arg,
            "b"_arg = 100,
        },
        coro_adder);

    this->server_->dispatcher()->add_coro(
        "coro_add_all_default",
        this->io_,
        {
            "a"_arg = 1000,
            "b"_arg = 10000,
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
        EXPECT_ERROR_EQ(
            this->client_->async_call(
                prefix + "add_all_default", std::make_tuple(1, 2, 3), use_future),
            "cannot convert arguments: too many arguments");

        // -- add_first_default --
        EXPECT_ERROR_EQ(
            this->client_->async_call(
                prefix + "add_first_default", std::make_tuple(), use_future),
            "cannot convert arguments: no value for argument b");
        EXPECT_ERROR_EQ(
            this->client_->async_call(
                prefix + "add_first_default", std::make_tuple(1), use_future),
            "cannot convert arguments: no value for argument b");
        EXPECT_RESULT_EQ(
            this->client_->async_call(
                prefix + "add_first_default", std::make_tuple(12, 13), use_future),
            25);
        EXPECT_ERROR_EQ(
            this->client_->async_call(
                prefix + "add_first_default", std::make_tuple(1, 2, 3), use_future),
            "cannot convert arguments: too many arguments");

        // -- add_second_default --
        EXPECT_ERROR_EQ(
            this->client_->async_call(
                prefix + "add_second_default", std::make_tuple(), use_future),
            "cannot convert arguments: no value for argument a");
        EXPECT_RESULT_EQ(
            this->client_->async_call(
                prefix + "add_second_default", std::make_tuple(12), use_future),
            112);
        EXPECT_RESULT_EQ(
            this->client_->async_call(
                prefix + "add_second_default", std::make_tuple(12, 13), use_future),
            25);
        EXPECT_ERROR_EQ(
            this->client_->async_call(
                prefix + "add_second_default", std::make_tuple(1, 2, 3), use_future),
            "cannot convert arguments: too many arguments");

        if constexpr (has_named_args) {
            // -- add_all_default --
            EXPECT_RESULT_EQ(
                this->client_->async_call(
                    prefix + "add_all_default",
                    std::make_tuple("a"_arg = 12),
                    use_future),
                10012);
            EXPECT_RESULT_EQ(
                this->client_->async_call(
                    prefix + "add_all_default",
                    std::make_tuple("b"_arg = 13),
                    use_future),
                1013);
            EXPECT_RESULT_EQ(
                this->client_->async_call(
                    prefix + "add_all_default",
                    std::make_tuple("a"_arg = 12, "b"_arg = 13),
                    use_future),
                25);
            EXPECT_ERROR_EQ(
                this->client_->async_call(
                    prefix + "add_all_default",
                    std::make_tuple("c"_arg = 3),
                    use_future),
                "cannot convert arguments: unexpected argument c");
            EXPECT_ERROR_EQ(
                this->client_->async_call(
                    prefix + "add_all_default",
                    std::make_tuple("a"_arg = 1, "b"_arg = 2, "c"_arg = 3),
                    use_future),
                "cannot convert arguments: unexpected argument c");

            // -- add_first_default --
            EXPECT_ERROR_EQ(
                this->client_->async_call(
                    prefix + "add_first_default",
                    std::make_tuple("a"_arg = 12),
                    use_future),
                "cannot convert arguments: no value for argument b");
            EXPECT_RESULT_EQ(
                this->client_->async_call(
                    prefix + "add_first_default",
                    std::make_tuple("b"_arg = 13),
                    use_future),
                23);
            EXPECT_RESULT_EQ(
                this->client_->async_call(
                    prefix + "add_first_default",
                    std::make_tuple("a"_arg = 12, "b"_arg = 13),
                    use_future),
                25);
            EXPECT_ERROR_EQ(
                this->client_->async_call(
                    prefix + "add_first_default",
                    std::make_tuple("a"_arg = 1, "b"_arg = 2, "c"_arg = 3),
                    use_future),
                "cannot convert arguments: unexpected argument c");

            // -- add_second_default --
            EXPECT_RESULT_EQ(
                this->client_->async_call(
                    prefix + "add_second_default",
                    std::make_tuple("a"_arg = 12),
                    use_future),
                112);
            EXPECT_ERROR_EQ(
                this->client_->async_call(
                    prefix + "add_second_default",
                    std::make_tuple("b"_arg = 13),
                    use_future),
                "cannot convert arguments: no value for argument a");
            EXPECT_RESULT_EQ(
                this->client_->async_call(
                    prefix + "add_second_default",
                    std::make_tuple("a"_arg = 12, "b"_arg = 13),
                    use_future),
                25);
            EXPECT_ERROR_EQ(
                this->client_->async_call(
                    prefix + "add_second_default",
                    std::make_tuple("a"_arg = 1, "b"_arg = 2, "c"_arg = 3),
                    use_future),
                "cannot convert arguments: unexpected argument c");
        }
    }
}

TYPED_TEST(BasicTest, test_extra_arguments)
{
    using rpc_type = typename std::decay_t<decltype(*this)>::client_type::rpc_type;
    constexpr bool has_named_args =
        !std::is_same_v<rpc_type, packio::msgpack_rpc::rpc>;

    this->server_->async_serve_forever();
    this->async_run();
    this->connect();

    this->server_->dispatcher()->add(
        "add",
        {
            allow_extra_arguments,
            "a"_arg = 10,
            "b",
        },
        [](int a, int b) { return a + b; });

    EXPECT_ERROR_EQ(
        this->client_->async_call("add", std::make_tuple(10), use_future),
        "cannot convert arguments: no value for argument b");

    EXPECT_RESULT_EQ(
        this->client_->async_call("add", std::make_tuple(1, 2), use_future), 3);

    EXPECT_RESULT_EQ(
        this->client_->async_call("add", std::make_tuple(1, 2, 100), use_future),
        3);

    if constexpr (has_named_args) {
        EXPECT_ERROR_EQ(
            this->client_->async_call(
                "add", std::make_tuple("a"_arg = 10), use_future),
            "cannot convert arguments: no value for argument b");

        EXPECT_RESULT_EQ(
            this->client_->async_call(
                "add", std::make_tuple("b"_arg = 2), use_future),
            12);

        EXPECT_RESULT_EQ(
            this->client_->async_call(
                "add", std::make_tuple("b"_arg = 2, "c"_arg = 100), use_future),
            12);

        EXPECT_RESULT_EQ(
            this->client_->async_call(
                "add",
                std::make_tuple("a"_arg = 1, "b"_arg = 2, "c"_arg = 100),
                use_future),
            3);
    }
}
