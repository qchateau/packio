#include "tests.h"

using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace packio::arg_literals;
using namespace packio::net;
using namespace packio;

TYPED_TEST(Test, test_connect)
{
    this->connect();
    ASSERT_TRUE(this->client_->socket().is_open());
}

TYPED_TEST(Test, test_server_crash)
{
    using server_type = std::decay_t<decltype(*this->server_)>;
    using session_type = std::decay_t<typename server_type::session_type>;

    std::shared_ptr<session_type> session_ptr;
    this->server_->async_serve([&](auto ec, auto session) {
        ASSERT_FALSE(ec);
        session->start();
        session_ptr = session;
    });
    this->server_->dispatcher()->add(
        "close", [&]() { session_ptr->socket().close(); });

    this->connect();
    this->async_run();

    auto f = this->client_->async_call("close", use_future);
    ASSERT_FUTURE_THROW(f, std::exception);
}

TYPED_TEST(Test, test_typical_usage)
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
        this->connect();
        this->async_run();

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
        ASSERT_FUTURE_NO_THROW(f);
        ASSERT_TRUE(call_latch.wait_for(1s));
        ASSERT_EQ(42, call_arg_received.load());
    }

    {
        call_latch.reset(1);
        call_arg_received = 0;

        auto f = this->client_->async_call("echo", std::tuple{42}, use_future);
        ASSERT_RESULT_EQ(f, 42);
        ASSERT_EQ(42, call_arg_received.load());
    }
}

TYPED_TEST(Test, test_timeout)
{
    using completion_handler =
        typename std::decay_t<decltype(*this)>::completion_handler;

    this->server_->async_serve_forever();
    this->connect();
    this->async_run();

    std::mutex mtx;

    std::list<completion_handler> pending;
    this->server_->dispatcher()->add_async(
        "block", [&](completion_handler handler) {
            std::unique_lock l{mtx};
            pending.push_back(std::move(handler));
        });
    this->server_->dispatcher()->add_async(
        "unblock", [&](completion_handler handler) {
            std::unique_lock l{mtx};
            for (auto& pending_handler : pending) {
                pending_handler();
            }
            pending.clear();
            handler();
        });

    {
        auto f1 = this->client_->async_call("block", use_future);
        auto f2 = this->client_->async_call("block", use_future);
        ASSERT_FUTURE_BLOCKS(f1, 100ms);
        ASSERT_FUTURE_BLOCKS(f2, 100ms);
        this->client_->cancel();
        ASSERT_FUTURE_CANCELLED(f1);
        ASSERT_FUTURE_CANCELLED(f2);
    }

    {
        std::unique_lock l{mtx};
        pending.clear();
    }

    {
        using id_type =
            typename std::decay_t<decltype(*this)>::client_type::id_type;
        id_type id1, id2;
        auto f1 = this->client_->async_call("block", use_future, id1);
        auto f2 = this->client_->async_call("block", use_future, id2);
        ASSERT_FUTURE_BLOCKS(f1, 10ms);
        ASSERT_FUTURE_BLOCKS(f2, 10ms);
        this->client_->cancel(id2);
        ASSERT_FUTURE_BLOCKS(f1, 100ms);
        ASSERT_FUTURE_CANCELLED(f2);
        this->client_->cancel(id1);
        ASSERT_FUTURE_CANCELLED(f1);
        this->client_->cancel(id2);
        this->client_->cancel(id1);
        this->client_->cancel(424242);
    }

    {
        std::unique_lock l{mtx};
        pending.clear();
    }

    {
        auto f = this->client_->async_call("block", use_future);
        ASSERT_FUTURE_BLOCKS(f, 100ms);
        ASSERT_RESULT_IS_OK(this->client_->async_call("unblock", use_future));
        ASSERT_RESULT_IS_OK(f);
    }

    this->io_.stop();
}

TYPED_TEST(Test, test_args_types)
{
    this->server_->async_serve_forever();
    this->connect();
    this->async_run();

    this->server_->dispatcher()->add("add", [](int a, int b) { return a + b; });

    // rvalue tuple
    ASSERT_RESULT_EQ(
        this->client_->async_call("add", std::tuple{12, 23}, use_future), 35);

    // lvalue tuple
    std::tuple tup{12, 23};
    ASSERT_RESULT_EQ(this->client_->async_call("add", tup, use_future), 35);

    // rvalue array
    ASSERT_RESULT_EQ(
        this->client_->async_call("add", std::array{12, 23}, use_future), 35);

    // lvalue array
    std::array arr{12, 23};
    ASSERT_RESULT_EQ(this->client_->async_call("add", arr, use_future), 35);

    // rvalue pair
    ASSERT_RESULT_EQ(
        this->client_->async_call("add", std::pair{12, 23}, use_future), 35);

    // lvalue pair
    std::pair pair{12, 23};
    ASSERT_RESULT_EQ(this->client_->async_call("add", pair, use_future), 35);
}

TYPED_TEST(Test, test_functions)
{
    using completion_handler =
        typename std::decay_t<decltype(*this)>::completion_handler;
    using tuple_int_str = std::tuple<int, std::string>;
    using rpc_type = typename std::decay_t<decltype(*this)>::client_type::rpc_type;
    using native_type = typename rpc_type::native_type;

    this->server_->async_serve_forever();
    this->connect();
    this->async_run();

    this->server_->dispatcher()->add_async(
        "async_void_void", [](completion_handler handler) { handler(); });
    this->server_->dispatcher()->add_async(
        "async_int_void", [](completion_handler handler) { handler(42); });
    this->server_->dispatcher()->add_async(
        "async_void_int", [](completion_handler handler, int) { handler(); });
    this->server_->dispatcher()->add_async(
        "async_int_int", [](completion_handler handler, int i) { handler(i); });
    this->server_->dispatcher()->add_async(
        "async_int_intref",
        [](completion_handler handler, const int& i) { handler(i); });
    this->server_->dispatcher()->add_async(
        "async_int_intref_int",
        [](completion_handler handler, const int& i, int) { handler(i); });
    this->server_->dispatcher()->add_async(
        "async_str_str",
        [](completion_handler handler, std::string s) { handler(s); });
    this->server_->dispatcher()->add_async(
        "async_str_strref",
        [](completion_handler handler, const std::string& s) { handler(s); });
    this->server_->dispatcher()->add_async(
        "async_tuple_int_str",
        [](completion_handler handler, int i, std::string s) {
            handler(std::tuple{i, s});
        });
    this->server_->dispatcher()->add_async(
        "async_native", [](completion_handler handler, native_type native) {
            handler(std::move(native));
        });

    this->server_->dispatcher()->add("sync_void_void", []() {});
    this->server_->dispatcher()->add("sync_int_void", []() { return 42; });
    this->server_->dispatcher()->add("sync_void_int", [](int) {});
    this->server_->dispatcher()->add("sync_int_int", [](int i) { return i; });
    this->server_->dispatcher()->add(
        "sync_int_intref", [](const int& i) { return i; });
    this->server_->dispatcher()->add(
        "sync_int_intref_int", [](const int& i, int) { return i; });
    this->server_->dispatcher()->add(
        "sync_str_str", [](std::string s) { return s; });
    this->server_->dispatcher()->add(
        "sync_str_strref", [](const std::string& s) { return s; });
    this->server_->dispatcher()->add(
        "sync_tuple_int_str", [](int i, std::string s) {
            return std::tuple{i, s};
        });
    this->server_->dispatcher()->add(
        "sync_native", [](native_type native) { return native; });

    ASSERT_RESULT_IS_OK(this->client_->async_call("async_void_void", use_future));

    ASSERT_RESULT_EQ(this->client_->async_call("async_int_void", use_future), 42);
    ASSERT_RESULT_IS_OK(this->client_->async_call(
        "async_void_int", std::tuple{42}, use_future));
    ASSERT_RESULT_EQ(
        this->client_->async_call("async_int_int", std ::tuple{42}, use_future),
        42);
    ASSERT_RESULT_EQ(
        this->client_->async_call("async_int_intref", std::tuple{42}, use_future),
        42);
    ASSERT_RESULT_EQ(
        this->client_->async_call(
            "async_int_intref_int", std::tuple{42, 24}, use_future),
        42);
    ASSERT_RESULT_EQ(
        this->client_->async_call(
            "async_str_str", std::make_tuple("foobar"), use_future),
        "foobar"s);
    ASSERT_RESULT_EQ(
        this->client_->async_call(
            "async_str_strref", std::make_tuple("foobar"), use_future),
        "foobar"s);
    ASSERT_RESULT_EQ(
        this->client_->async_call(
            "async_tuple_int_str", std::make_tuple(42, "foobar"), use_future),
        tuple_int_str(42, "foobar"));
    ASSERT_RESULT_EQ(
        this->client_->async_call(
            "async_native", std::make_tuple("foobar"), use_future),
        "foobar"s);

    ASSERT_RESULT_IS_OK(this->client_->async_call("sync_void_void", use_future));
    ASSERT_RESULT_EQ(this->client_->async_call("sync_int_void", use_future), 42);
    ASSERT_RESULT_IS_OK(
        this->client_->async_call("sync_void_int", std::tuple{42}, use_future));
    ASSERT_RESULT_EQ(
        this->client_->async_call("sync_int_int", std::tuple{42}, use_future),
        42);
    ASSERT_RESULT_EQ(
        this->client_->async_call("sync_int_intref", std::tuple{42}, use_future),
        42);
    ASSERT_RESULT_EQ(
        this->client_->async_call(
            "sync_int_intref_int", std::tuple{42, 24}, use_future),
        42);
    ASSERT_RESULT_EQ(
        this->client_->async_call(
            "sync_str_str", std::make_tuple("foobar"), use_future),
        "foobar"s);
    ASSERT_RESULT_EQ(
        this->client_->async_call(
            "sync_str_strref", std::make_tuple("foobar"), use_future),
        "foobar"s);
    ASSERT_RESULT_EQ(
        this->client_->async_call(
            "sync_tuple_int_str", std::make_tuple(42, "foobar"), use_future),
        tuple_int_str(42, "foobar"));
    ASSERT_RESULT_EQ(
        this->client_->async_call(
            "sync_native", std::make_tuple("foobar"), use_future),
        "foobar"s);
}

TYPED_TEST(Test, test_named_arguments)
{
    using completion_handler =
        typename std::decay_t<decltype(*this)>::completion_handler;
    using rpc_type = typename std::decay_t<decltype(*this)>::client_type::rpc_type;
    constexpr bool has_named_args =
        std::is_same_v<rpc_type, packio::nl_json_rpc::rpc>;

    this->server_->async_serve_forever();
    this->connect();
    this->async_run();

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

    ASSERT_RESULT_EQ(
        this->client_->async_call("echo", std::make_tuple("toto"), use_future),
        "toto"s);
    if constexpr (has_named_args) {
        ASSERT_RESULT_EQ(
            this->client_->async_call(
                "echo", std::tuple{arg("a") = "toto"}, use_future),
            "toto"s);
        ASSERT_RESULT_EQ(
            this->client_->async_call(
                "echo", std::tuple{arg("a") = "toto", "b"_arg = "titi"}, use_future),
            "toto"s);
    }

    ASSERT_RESULT_EQ(
        this->client_->async_call(
            "concat", std::make_tuple("toto", "titi"), use_future),
        "tototiti"s);
    if constexpr (has_named_args) {
        ASSERT_RESULT_EQ(
            this->client_->async_call(
                "concat",
                std::tuple{arg("b") = "titi", "a"_arg = "toto"},
                use_future),
            "tototiti"s);
        ASSERT_RESULT_EQ(
            this->client_->async_call(
                "concat",
                std::tuple{"c"_arg = "tata", arg("b") = "titi", "a"_arg = "toto"},
                use_future),
            "tototiti"s);
    }

    ASSERT_RESULT_EQ(
        this->client_->async_call("aecho", std::make_tuple("toto"), use_future),
        "toto"s);
    if constexpr (has_named_args) {
        ASSERT_RESULT_EQ(
            this->client_->async_call(
                "aecho", std::tuple{arg("a") = "toto"}, use_future),
            "toto"s);
        ASSERT_RESULT_EQ(
            this->client_->async_call(
                "aecho",
                std::tuple{arg("a") = "toto", "b"_arg = "titi"},
                use_future),
            "toto"s);
    }
    ASSERT_RESULT_EQ(
        this->client_->async_call(
            "aconcat", std::make_tuple("toto", "titi"), use_future),
        "tototiti"s);
    if constexpr (has_named_args) {
        ASSERT_RESULT_EQ(
            this->client_->async_call(
                "aconcat",
                std::tuple{"b"_arg = "titi", arg("a") = "toto"},
                use_future),
            "tototiti"s);
        ASSERT_RESULT_EQ(
            this->client_->async_call(
                "aconcat",
                std::tuple{"c"_arg = "tata", arg("b") = "titi", "a"_arg = "toto"},
                use_future),
            "tototiti"s);
    }

#if defined(PACKIO_HAS_CO_AWAIT) || defined(PACKIO_FORCE_COROUTINES)
    ASSERT_RESULT_EQ(
        this->client_->async_call("cecho", std::make_tuple("toto"), use_future),
        "toto"s);
    if constexpr (has_named_args) {
        ASSERT_RESULT_EQ(
            this->client_->async_call(
                "cecho", std::tuple{"a"_arg = "toto"}, use_future),
            "toto"s);
        ASSERT_RESULT_EQ(
            this->client_->async_call(
                "cecho",
                std::tuple{arg("a") = "toto", "b"_arg = "titi"},
                use_future),
            "toto"s);
    }
    ASSERT_RESULT_EQ(
        this->client_->async_call(
            "cconcat", std::make_tuple("toto", "titi"), use_future),
        "tototiti"s);
    if constexpr (has_named_args) {
        ASSERT_RESULT_EQ(
            this->client_->async_call(
                "cconcat",
                std::tuple{"b"_arg = "titi", "a"_arg = "toto"},
                use_future),
            "tototiti"s);
        ASSERT_RESULT_EQ(
            this->client_->async_call(
                "cconcat",
                std::tuple{"c"_arg = "tata", arg("b") = "titi", "a"_arg = "toto"},
                use_future),
            "tototiti"s);
    }
#endif // defined(PACKIO_HAS_CO_AWAIT) || defined(PACKIO_FORCE_COROUTINES)
}

TYPED_TEST(Test, test_dispatcher)
{
    using completion_handler =
        typename std::decay_t<decltype(*this)>::completion_handler;

    this->server_->async_serve_forever();
    this->connect();
    this->async_run();

    ASSERT_TRUE(this->server_->dispatcher()->add_async(
        "f001", [](completion_handler handler) { handler(); }));
    ASSERT_TRUE(this->server_->dispatcher()->add("f002", []() {}));

    ASSERT_FALSE(this->server_->dispatcher()->add_async(
        "f001", [](completion_handler handler) { handler(); }));
    ASSERT_FALSE(this->server_->dispatcher()->add_async(
        "f002", [](completion_handler handler) { handler(); }));
    ASSERT_FALSE(this->server_->dispatcher()->add("f001", []() {}));
    ASSERT_FALSE(this->server_->dispatcher()->add("f002", []() {}));

    ASSERT_RESULT_IS_OK(this->client_->async_call("f001", use_future));
    ASSERT_RESULT_IS_OK(this->client_->async_call("f002", use_future));

    ASSERT_TRUE(this->server_->dispatcher()->has("f001"));
    ASSERT_TRUE(this->server_->dispatcher()->has("f002"));
    ASSERT_FALSE(this->server_->dispatcher()->has("f003"));
    auto known = this->server_->dispatcher()->known();
    ASSERT_EQ(
        (std::set<std::string>{"f001", "f002"}),
        std::set<std::string>(begin(known), end(known)));

    this->server_->dispatcher()->remove("f001");
    ASSERT_RESULT_IS_ERROR(this->client_->async_call("f001", use_future));

    ASSERT_FALSE(this->server_->dispatcher()->has("f001"));
    ASSERT_TRUE(this->server_->dispatcher()->has("f002"));
    ASSERT_FALSE(this->server_->dispatcher()->has("f003"));

    ASSERT_EQ(1u, this->server_->dispatcher()->clear());

    ASSERT_FALSE(this->server_->dispatcher()->has("f001"));
    ASSERT_FALSE(this->server_->dispatcher()->has("f002"));
    ASSERT_FALSE(this->server_->dispatcher()->has("f003"));
}

TYPED_TEST(Test, test_end_of_work)
{
    using client_type = typename std::decay_t<decltype(*this)>::client_type;
    using socket_type = typename std::decay_t<decltype(*this)>::socket_type;
    using id_type = typename client_type::id_type;
    using completion_handler =
        typename std::decay_t<decltype(*this)>::completion_handler;

    this->server_->async_serve_forever();
    std::vector<completion_handler> pending;
    this->server_->dispatcher()->add("func", []() {});
    this->server_->dispatcher()->add_async(
        "block", [&](completion_handler c) { pending.push_back(std::move(c)); });
    this->async_run();

    auto ep = this->server_->acceptor().local_endpoint();

    io_context io;
    auto client = std::make_shared<client_type>(socket_type{io});
    client->socket().connect(ep);

    // client runs out of work if there is no pending calls
    io.run_for(1s);
    ASSERT_TRUE(io.stopped());

    // client runs out of work after a notify
    io.restart();
    client->async_notify("func", [](auto) {});
    ASSERT_FALSE(io.stopped());
    io.run_for(1s);
    ASSERT_TRUE(io.stopped());

    // client runs out of work after a call
    io.restart();
    client->async_call("func", [](auto, auto) {});
    ASSERT_FALSE(io.stopped());
    io.run_for(1s);
    ASSERT_TRUE(io.stopped());

    // client runs out of work after a cancelled call
    int cancelled_count{0};
    io.restart();
    ASSERT_FALSE(io.stopped());
    id_type id{};
    client->async_call(
        "block",
        [&](auto ec, auto) {
            ASSERT_TRUE(ec);
            cancelled_count++;
        },
        id);
    io.run_for(10ms);
    ASSERT_FALSE(io.stopped());
    client->cancel(id);
    io.run_for(1s);
    ASSERT_TRUE(io.stopped());

    // client runs out of work after multiple cancelled calls
    io.restart();
    client->async_call("block", [&](auto ec, auto) {
        ASSERT_TRUE(ec);
        cancelled_count++;
    });
    client->async_call("block", [&](auto ec, auto) {
        ASSERT_TRUE(ec);
        cancelled_count++;
    });
    io.run_for(10ms);
    ASSERT_FALSE(io.stopped());
    client->cancel();
    io.run_for(1s);
    ASSERT_TRUE(io.stopped());
    ASSERT_EQ(3, cancelled_count);
}

TYPED_TEST(Test, test_special_callables)
{
    using session_type = typename decltype(
        this->server_)::element_type::session_type;
    using completion_handler =
        typename std::decay_t<decltype(*this)>::completion_handler;
    using rpc_type = typename std::decay_t<decltype(*this)>::client_type::rpc_type;

    struct move_only {
        move_only() = default;

        move_only(const move_only&) = delete;
        move_only& operator=(const move_only&) = delete;

        move_only(move_only&&) = default;
        move_only& operator=(move_only&&) = default;
    };
    struct sync_procedure : public move_only {
        void operator()(){};
    };
    struct async_procedure : public move_only {
        void operator()(completion_handler complete) { complete(); };
    };
    struct notify_handler : public move_only {
        void operator()(error_code){};
    };
    struct call_handler : public move_only {
        void operator()(error_code, typename rpc_type::response_type){};
    };
    struct serve_handler : public move_only {
        void operator()(error_code, std::shared_ptr<session_type>){};
    };

    // this test just needs to compile
    // test with move-only lambdas
    // and with move-only callables that have non-const operator()

    this->connect();

    this->server_->dispatcher()->add_async(
        "move_only_lambda_async_001",
        [ptr = std::unique_ptr<int>{}](completion_handler) {});
    this->server_->dispatcher()->add(
        "move_only_lambda_sync_001", [ptr = std::unique_ptr<int>{}]() {});

    this->server_->dispatcher()->add_async(
        "move_only_callable_async_001", async_procedure{});
    this->server_->dispatcher()->add(
        "move_only_callable_sync_001", sync_procedure{});

    this->server_->async_serve([ptr = std::unique_ptr<int>{}](auto, auto) {});
    this->server_->async_serve(serve_handler{});

    this->server_->dispatcher()->add("f", sync_procedure{});
    this->client_->async_notify("f", [ptr = std::unique_ptr<int>{}](auto) {});
    this->client_->async_notify("f", notify_handler{});
    this->client_->async_call("f", [ptr = std::unique_ptr<int>{}](auto, auto) {});
    this->client_->async_call("f", call_handler{});

    static_assert(std::is_move_assignable_v<completion_handler>);
    static_assert(std::is_move_constructible_v<completion_handler>);
}

TYPED_TEST(Test, test_response_after_disconnect)
{
    using completion_handler =
        typename std::decay_t<decltype(*this)>::completion_handler;

    this->server_->async_serve_forever();
    this->connect();
    this->async_run();

    // Use a unique_ptr here because MSVC's promise is bugged and required
    // default-constructible type
    std::promise<std::unique_ptr<completion_handler>> complete_promise;
    auto future = complete_promise.get_future();
    this->server_->dispatcher()->add_async(
        "block", [&](completion_handler complete) {
            complete_promise.set_value(
                std::make_unique<completion_handler>(std::move(complete)));
        });

    this->client_->async_call("block", [&](auto, auto) {});
    auto complete_ptr = safe_future_get(future);
    this->client_->socket().shutdown(ip::tcp::socket::shutdown_both);
    (*complete_ptr)();
}

TYPED_TEST(Test, test_shared_dispatcher)
{
    using server_type = typename std::decay_t<decltype(*this)>::server_type;
    using client_type = typename std::decay_t<decltype(*this)>::client_type;
    using socket_type = typename std::decay_t<decltype(*this)>::socket_type;
    using endpoint_type = typename std::decay_t<decltype(*this)>::endpoint_type;
    using acceptor_type = typename std::decay_t<decltype(*this)>::acceptor_type;
    using completion_handler =
        typename std::decay_t<decltype(*this)>::completion_handler;

    this->server_->async_serve_forever();
    this->connect();
    this->async_run();

    // server2 is a different server but shares the same dispatcher as server_
    auto server2 = std::make_shared<server_type>(
        acceptor_type(this->io_, get_endpoint<endpoint_type>()),
        this->server_->dispatcher());

    auto client2 = std::make_shared<client_type>(socket_type{this->io_});

    auto ep = server2->acceptor().local_endpoint();
    client2->socket().connect(ep);
    server2->async_serve_forever();

    ASSERT_NE(
        this->server_->acceptor().local_endpoint(),
        server2->acceptor().local_endpoint());

    latch l{2};
    ASSERT_TRUE(this->server_->dispatcher()->add_async(
        "inc", [&](completion_handler handler) {
            l.count_down();
            handler();
        }));

    this->client_->async_notify("inc", [](auto ec) { ASSERT_FALSE(ec); });
    client2->async_notify("inc", [](auto ec) { ASSERT_FALSE(ec); });

    ASSERT_TRUE(l.wait_for(1s));
}

TYPED_TEST(Test, test_errors)
{
    using completion_handler =
        typename std::decay_t<decltype(*this)>::completion_handler;
    using rpc_type = typename std::decay_t<decltype(*this)>::client_type::rpc_type;
    constexpr bool has_named_args =
        std::is_same_v<rpc_type, packio::nl_json_rpc::rpc>;
    const std::string kErrorMessage{"error message"};

    this->server_->async_serve_forever();
    this->connect();
    this->async_run();

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

#if defined(PACKIO_HAS_CO_AWAIT) || defined(PACKIO_FORCE_COROUTINES)
TYPED_TEST(Test, test_coroutine)
{
    steady_timer timer{this->io_};

    this->server_->dispatcher()->add_coro(
        "add",
        this->io_.get_executor(), // executor
        [&](int a, int b) -> awaitable<int> {
            timer.expires_after(1ms);
            co_await timer.async_wait(use_awaitable);
            co_return a + b;
        });

    this->server_->dispatcher()->add_coro(
        "add2",
        this->io_, // execution context
        [&](int a, int b) -> awaitable<int> {
            timer.expires_after(1ms);
            co_await timer.async_wait(use_awaitable);
            co_return a + b;
        });

    co_spawn(
        this->io_,
        [&]() -> awaitable<void> {
            while (true) {
                auto session = co_await this->server_->async_serve(use_awaitable);
                session->start();
            }
        },
        detached);

    this->async_run();
    this->connect();

    std::promise<void> p;
    co_spawn(
        this->io_,
        [&]() -> awaitable<void> {
            // Call using an awaitable
            auto res = co_await this->client_->async_call(
                "add", std::tuple{12, 23}, use_awaitable);
            EXPECT_EQ(get<int>(res.result), 35);

            res = co_await this->client_->async_call(
                "add2", std::tuple{31, 3}, use_awaitable);
            EXPECT_EQ(get<int>(res.result), 34);

            p.set_value();
        },
        detached);
    ASSERT_FUTURE_NO_THROW(p.get_future());
}
#endif // defined(PACKIO_HAS_CO_AWAIT) || defined(PACKIO_FORCE_COROUTINES)
