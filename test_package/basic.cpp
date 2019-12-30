#include <atomic>
#include <chrono>
#include <future>
#include <unordered_map>

#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

#include <packio/client.h>
#include <packio/server.h>

#include "misc.h"

using namespace std::chrono;
using namespace boost::asio;
using namespace packio;
using std::this_thread::sleep_for;

template <typename T>
struct my_allocator : public std::allocator<T> {
};

template <typename Key, typename T>
using my_unordered_map = std::unordered_map<
    Key,
    T,
    std::hash<Key>,
    std::equal_to<Key>,
    my_allocator<std::pair<const Key, T>>>;

typedef ::testing::Types<
    std::pair<client<boost::asio::ip::tcp>, server<boost::asio::ip::tcp>>,
#if defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)
    std::pair<client<boost::asio::local::stream_protocol>, server<boost::asio::local::stream_protocol>>,
#endif // defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)
    std::pair<client<boost::asio::ip::tcp, my_unordered_map>, server<boost::asio::ip::tcp>>,
    std::pair<
        client<boost::asio::ip::tcp, std::map, std::recursive_mutex>,
        server<boost::asio::ip::tcp>>>
    Implementations;

class packio_exception : public std::system_error {
public:
    packio_exception(std::error_code ec, msgpack::object result)
        : system_error(ec)
    {
        if (result.type == msgpack::type::STR) {
            result_message_ = result.as<std::string>();
        }
        else {
            result_message_ = "not a string";
        }
    }

    std::string result_message() const { return result_message_; }

private:
    std::string result_message_;
};

template <class Impl>
class Test : public ::testing::Test {
protected:
    using client_type = typename Impl::first_type;
    using server_type = typename Impl::second_type;
    using protocol_type = typename client_type::protocol_type;
    using endpoint_type = typename protocol_type::endpoint;
    using socket_type = typename protocol_type::socket;
    using acceptor_type = typename protocol_type::acceptor;

    Test()
        : server_{acceptor_type(io_, get_endpoint<endpoint_type>())},
          client_{socket_type{io_}}
    {
    }

    ~Test()
    {
        io_.stop();
        if (runner_.joinable()) {
            runner_.join();
        }
    }

    void async_run()
    {
        runner_ = std::thread{[this] { io_.run(); }};
    }

    void connect()
    {
        auto ep = server_.acceptor().local_endpoint();
        client_.socket().connect(ep);
    }

    template <typename... Args>
    auto future_notify(std::string_view name, Args&&... args)
    {
        std::promise<void> p;
        auto f = p.get_future();
        client_.async_notify(
            name,
            std::forward_as_tuple(args...),
            [p = std::move(p)](auto ec) mutable {
                if (ec) {
                    p.set_exception(
                        std::make_exception_ptr(std::system_error(ec)));
                }
                else {
                    p.set_value();
                }
            });
        return f;
    }

    template <typename R, typename... Args>
    auto future_call(std::string_view name, Args&&... args)
    {
        id_type id;
        return future_call<R>(id, name, std::forward<Args>(args)...);
    }

    template <typename R, typename... Args>
    auto future_call(id_type& id, std::string_view name, Args&&... args)
    {
        std::promise<R> p;
        auto f = p.get_future();
        id = client_.async_call(
            name,
            std::forward_as_tuple(args...),
            [p = std::move(p)](auto ec, auto result) mutable {
                if (ec) {
                    p.set_exception(std::make_exception_ptr(
                        packio_exception(ec, result.get())));
                }
                else {
                    if constexpr (std::is_void_v<R>) {
                        if (result->type != msgpack::type::NIL) {
                            p.set_exception(std::make_exception_ptr(
                                std::runtime_error("bad result type")));
                        }
                        else {
                            p.set_value();
                        }
                    }
                    else {
                        p.set_value(result->template as<R>());
                    }
                }
            });
        return f;
    }

    boost::asio::io_context io_;
    server_type server_;
    client_type client_;
    std::thread runner_;
};

TYPED_TEST_CASE(Test, Implementations);

TYPED_TEST(Test, test_connect)
{
    this->connect();
    ASSERT_TRUE(this->client_.socket().is_open());
}

TYPED_TEST(Test, test_typical_usage)
{
    {
        latch connected{1};
        this->server_.async_serve([&](auto ec, auto session) {
            ASSERT_FALSE(ec);
            session->start();
            connected.count_down();
        });
        this->connect();
        this->async_run();

        ASSERT_TRUE(this->client_.socket().is_open());
        ASSERT_TRUE(connected.wait_for(std::chrono::seconds{1}));
    }

    std::atomic<int> call_arg_received{0};
    latch call_latch{0};
    this->server_.dispatcher()->add_async(
        "echo", [&](completion_handler handler, int i) {
            call_arg_received = i;
            call_latch.count_down();
            handler(i);
        });

    {
        call_latch.reset(1);

        auto f = this->future_notify("echo", 42);
        ASSERT_EQ(std::future_status::ready, f.wait_for(std::chrono::seconds{1}));
        ASSERT_NO_THROW(f.get());
        ASSERT_TRUE(call_latch.wait_for(std::chrono::seconds{1}));
        ASSERT_EQ(42, call_arg_received.load());
    }

    {
        call_latch.reset(1);
        call_arg_received = 0;

        auto f = this->template future_call<int>("echo", 42);
        ASSERT_EQ(std::future_status::ready, f.wait_for(std::chrono::seconds{1}));
        ASSERT_EQ(42, f.get());
        ASSERT_EQ(42, call_arg_received.load());
    }
}

TYPED_TEST(Test, test_timeout)
{
    this->server_.async_serve_forever();
    this->connect();
    this->async_run();

    std::mutex mtx;

    std::list<completion_handler> pending;
    this->server_.dispatcher()->add_async(
        "block", [&](completion_handler handler) {
            std::unique_lock l{mtx};
            pending.push_back(std::move(handler));
        });
    this->server_.dispatcher()->add_async(
        "unblock", [&](completion_handler handler) {
            std::unique_lock l{mtx};
            for (auto& pending_handler : pending) {
                pending_handler();
            }
            pending.clear();
            handler();
        });

    const auto assert_blocks = [](auto& future) {
        ASSERT_EQ(
            std::future_status::timeout,
            future.wait_for(std::chrono::milliseconds{1}));
    };
    const auto assert_cancelled = [](auto& future) {
        ASSERT_EQ(
            std::future_status::ready,
            future.wait_for(std::chrono::milliseconds{1}));
        try {
            future.get();
            ASSERT_FALSE(true); // never reached
        }
        catch (std::system_error& err) {
            ASSERT_EQ(make_error_code(packio::error::cancelled), err.code());
        }
    };

    {
        auto f1 = this->template future_call<void>("block");
        auto f2 = this->template future_call<void>("block");
        assert_blocks(f1);
        assert_blocks(f2);
        ASSERT_EQ(2ul, this->client_.cancel());
        assert_cancelled(f1);
        assert_cancelled(f2);
    }

    {
        std::unique_lock l{mtx};
        pending.clear();
    }

    {
        id_type id1, id2;
        auto f1 = this->template future_call<void>(id1, "block");
        auto f2 = this->template future_call<void>(id2, "block");
        assert_blocks(f1);
        assert_blocks(f2);
        ASSERT_EQ(1ul, this->client_.cancel(id2));
        assert_blocks(f1);
        assert_cancelled(f2);
        ASSERT_EQ(1ul, this->client_.cancel(id1));
        assert_cancelled(f1);
        ASSERT_EQ(0ul, this->client_.cancel(id2));
        ASSERT_EQ(0ul, this->client_.cancel(id1));
        ASSERT_EQ(0ul, this->client_.cancel(424242));
    }

    {
        std::unique_lock l{mtx};
        pending.clear();
    }

    {
        auto f = this->template future_call<void>("block");
        assert_blocks(f);
        this->template future_call<void>("unblock").get();
        f.get();
    }

    this->io_.stop();
}

TYPED_TEST(Test, test_functions)
{
    using tuple_int_str = std::tuple<int, std::string>;
    this->server_.async_serve_forever();
    this->connect();
    this->async_run();

    this->server_.dispatcher()->add_async(
        "async_void_void", [](completion_handler handler) { handler(); });
    this->server_.dispatcher()->add_async(
        "async_int_void", [](completion_handler handler) { handler(42); });
    this->server_.dispatcher()->add_async(
        "async_void_int", [](completion_handler handler, int) { handler(); });
    this->server_.dispatcher()->add_async(
        "async_int_int", [](completion_handler handler, int i) { handler(i); });
    this->server_.dispatcher()->add_async(
        "async_int_intref",
        [](completion_handler handler, const int& i) { handler(i); });
    this->server_.dispatcher()->add_async(
        "async_int_intref_int",
        [](completion_handler handler, const int& i, int) { handler(i); });
    this->server_.dispatcher()->add_async(
        "async_str_str",
        [](completion_handler handler, std::string s) { handler(s); });
    this->server_.dispatcher()->add_async(
        "async_str_strref",
        [](completion_handler handler, const std::string& s) { handler(s); });
    this->server_.dispatcher()->add_async(
        "async_tuple_int_str",
        [](completion_handler handler, int i, std::string s) {
            handler(std::tuple(i, s));
        });

    this->server_.dispatcher()->add("sync_void_void", []() {});
    this->server_.dispatcher()->add("sync_int_void", []() { return 42; });
    this->server_.dispatcher()->add("sync_void_int", [](int) {});
    this->server_.dispatcher()->add("sync_int_int", [](int i) { return i; });
    this->server_.dispatcher()->add(
        "sync_int_intref", [](const int& i) { return i; });
    this->server_.dispatcher()->add(
        "sync_int_intref_int", [](const int& i, int) { return i; });
    this->server_.dispatcher()->add(
        "sync_str_str", [](std::string s) { return s; });
    this->server_.dispatcher()->add(
        "sync_str_strref", [](const std::string& s) { return s; });
    this->server_.dispatcher()->add(
        "sync_tuple_int_str",
        [](int i, std::string s) { return std::tuple(i, s); });

    ASSERT_NO_THROW(this->template future_call<void>("async_void_void").get());
    ASSERT_EQ(42, this->template future_call<int>("async_int_void").get());
    ASSERT_NO_THROW(this->template future_call<void>("async_void_int", 42).get());
    ASSERT_EQ(42, this->template future_call<int>("async_int_int", 42).get());
    ASSERT_EQ(42, this->template future_call<int>("async_int_intref", 42).get());
    ASSERT_EQ(
        42,
        this->template future_call<int>("async_int_intref_int", 42, 24).get());
    ASSERT_EQ(
        "foobar",
        this->template future_call<std::string>("async_str_str", "foobar").get());
    ASSERT_EQ(
        "foobar",
        this->template future_call<std::string>("async_str_strref", "foobar").get());
    ASSERT_EQ(
        tuple_int_str(42, "foobar"),
        this->template future_call<tuple_int_str>(
                "async_tuple_int_str", 42, "foobar")
            .get());

    ASSERT_NO_THROW(this->template future_call<void>("sync_void_void").get());
    ASSERT_EQ(42, this->template future_call<int>("sync_int_void").get());
    ASSERT_NO_THROW(this->template future_call<void>("sync_void_int", 42).get());
    ASSERT_EQ(42, this->template future_call<int>("sync_int_int", 42).get());
    ASSERT_EQ(42, this->template future_call<int>("sync_int_intref", 42).get());
    ASSERT_EQ(
        42, this->template future_call<int>("sync_int_intref_int", 42, 24).get());
    ASSERT_EQ(
        "foobar",
        this->template future_call<std::string>("sync_str_str", "foobar").get());
    ASSERT_EQ(
        "foobar",
        this->template future_call<std::string>("sync_str_strref", "foobar").get());
    ASSERT_EQ(
        tuple_int_str(42, "foobar"),
        this->template future_call<tuple_int_str>(
                "sync_tuple_int_str", 42, "foobar")
            .get());
}

TYPED_TEST(Test, test_dispatcher)
{
    this->server_.async_serve_forever();
    this->connect();
    this->async_run();

    ASSERT_TRUE(this->server_.dispatcher()->add_async(
        "f001", [](completion_handler handler) { handler(); }));
    ASSERT_TRUE(this->server_.dispatcher()->add("f002", []() {}));

    ASSERT_FALSE(this->server_.dispatcher()->add_async(
        "f001", [](completion_handler handler) { handler(); }));
    ASSERT_FALSE(this->server_.dispatcher()->add_async(
        "f002", [](completion_handler handler) { handler(); }));
    ASSERT_FALSE(this->server_.dispatcher()->add("f001", []() {}));
    ASSERT_FALSE(this->server_.dispatcher()->add("f002", []() {}));

    this->template future_call<void>("f001").get();
    this->template future_call<void>("f002").get();

    ASSERT_TRUE(this->server_.dispatcher()->has("f001"));
    ASSERT_TRUE(this->server_.dispatcher()->has("f002"));
    ASSERT_FALSE(this->server_.dispatcher()->has("f003"));
    auto known = this->server_.dispatcher()->known();
    ASSERT_EQ(
        (std::set<std::string>{"f001", "f002"}),
        std::set<std::string>(begin(known), end(known)));

    this->server_.dispatcher()->remove("f001");
    ASSERT_THROW(
        this->template future_call<void>("f001").get(), packio_exception);

    ASSERT_FALSE(this->server_.dispatcher()->has("f001"));
    ASSERT_TRUE(this->server_.dispatcher()->has("f002"));
    ASSERT_FALSE(this->server_.dispatcher()->has("f003"));

    ASSERT_EQ(1u, this->server_.dispatcher()->clear());

    ASSERT_FALSE(this->server_.dispatcher()->has("f001"));
    ASSERT_FALSE(this->server_.dispatcher()->has("f002"));
    ASSERT_FALSE(this->server_.dispatcher()->has("f003"));
}

TYPED_TEST(Test, test_special_callables)
{
    using session_type = typename decltype(this->server_)::session_type;
    struct move_only {
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
        void operator()(boost::system::error_code){};
    };
    struct call_handler : public move_only {
        void operator()(boost::system::error_code, msgpack::object_handle){};
    };
    struct serve_handler : public move_only {
        void operator()(boost::system::error_code, std::shared_ptr<session_type>){};
    };

    // this test just needs to compile
    // test with move-only lambdas
    // and with move-only callables that have non-const operator()

    this->connect();

    this->server_.dispatcher()->add_async(
        "move_only_lambda_async_001",
        [ptr = std::unique_ptr<int>{}](completion_handler) {});
    this->server_.dispatcher()->add(
        "move_only_lambda_sync_001", [ptr = std::unique_ptr<int>{}]() {});

    this->server_.dispatcher()->add_async(
        "move_only_callable_async_001", async_procedure{});
    this->server_.dispatcher()->add(
        "move_only_callable_sync_001", sync_procedure{});

    this->server_.async_serve([ptr = std::unique_ptr<int>{}](auto, auto) {});
    this->server_.async_serve(serve_handler{});

    this->server_.dispatcher()->add("f", sync_procedure{});
    this->client_.async_notify("f", [ptr = std::unique_ptr<int>{}](auto) {});
    this->client_.async_notify("f", notify_handler{});
    this->client_.async_call("f", [ptr = std::unique_ptr<int>{}](auto, auto) {});
    this->client_.async_call("f", call_handler{});

    static_assert(std::is_move_assignable_v<completion_handler>);
    static_assert(std::is_move_constructible_v<completion_handler>);
}

TYPED_TEST(Test, test_response_after_disconnect)
{
    this->server_.async_serve_forever();
    this->connect();
    this->async_run();

    // Use a unique_ptr here because MSVC's promise is bugged and required
    // default-constructible type
    std::promise<std::unique_ptr<completion_handler>> complete_promise;
    auto future = complete_promise.get_future();
    this->server_.dispatcher()->add_async(
        "block", [&](completion_handler complete) {
            complete_promise.set_value(
                std::make_unique<completion_handler>(std::move(complete)));
        });

    this->client_.async_call("block", [&](auto, auto) {});
    auto complete_ptr = future.get();
    this->client_.socket().shutdown(boost::asio::ip::tcp::socket::shutdown_both);
    (*complete_ptr)();
}

TYPED_TEST(Test, test_shared_dispatcher)
{
    using server_type = typename std::decay_t<decltype(*this)>::server_type;
    using client_type = typename std::decay_t<decltype(*this)>::client_type;
    using socket_type = typename std::decay_t<decltype(*this)>::socket_type;
    using endpoint_type = typename std::decay_t<decltype(*this)>::endpoint_type;
    using acceptor_type = typename std::decay_t<decltype(*this)>::acceptor_type;

    this->server_.async_serve_forever();
    this->connect();
    this->async_run();

    // server2 is a different server but shares the same dispatcher as server_
    server_type server2{acceptor_type(this->io_, get_endpoint<endpoint_type>()),
                        this->server_.dispatcher()};

    client_type client2{socket_type{this->io_}};

    auto ep = server2.acceptor().local_endpoint();
    client2.socket().connect(ep);
    server2.async_serve_forever();

    ASSERT_NE(
        this->server_.acceptor().local_endpoint(),
        server2.acceptor().local_endpoint());

    latch l{2};
    ASSERT_TRUE(this->server_.dispatcher()->add_async(
        "inc", [&](completion_handler handler) {
            l.count_down();
            handler();
        }));

    this->client_.async_notify("inc", [](auto ec) { ASSERT_FALSE(ec); });
    client2.async_notify("inc", [](auto ec) { ASSERT_FALSE(ec); });

    ASSERT_TRUE(l.wait_for(std::chrono::seconds{1}));
}

TYPED_TEST(Test, test_errors)
{
    constexpr auto kErrorMessage{"error message"};

    this->server_.async_serve_forever();
    this->connect();
    this->async_run();

    ASSERT_TRUE(this->server_.dispatcher()->add_async(
        "error",
        [&](completion_handler handler) { handler.set_error(kErrorMessage); }));
    ASSERT_TRUE(this->server_.dispatcher()->add_async(
        "empty_error", [](completion_handler handler) { handler.set_error(); }));
    ASSERT_TRUE(this->server_.dispatcher()->add_async(
        "no_result", [&](completion_handler) {}));
    ASSERT_TRUE(this->server_.dispatcher()->add_async(
        "add", [](completion_handler handler, int a, int b) { handler(a + b); }));
    ASSERT_TRUE(this->server_.dispatcher()->add(
        "add_sync", [](int a, int b) { return a + b; }));

    auto assert_error_message =
        [&](std::string message, std::string procedure, auto... args) {
            try {
                this->template future_call<void>(procedure, args...).get();
                ASSERT_FALSE(true); // never reached
            }
            catch (packio_exception& exc) {
                ASSERT_EQ(message, exc.result_message());
            }
        };

    assert_error_message(kErrorMessage, "error");
    assert_error_message("Error during call", "empty_error");
    assert_error_message("Call finished with no result", "no_result");
    assert_error_message("Unknown function", "unexisting");
    assert_error_message("Incompatible arguments", "add", 1, "two");
    assert_error_message("Incompatible arguments", "add");
    assert_error_message("Incompatible arguments", "add", 1, 2, 3);
    assert_error_message("Incompatible arguments", "add_sync", 1, "two");
    assert_error_message("Incompatible arguments", "add_sync");
    assert_error_message("Incompatible arguments", "add_sync", 1, 2, 3);
}

int main(int argc, char** argv)
{
    ::spdlog::default_logger()->set_level(::spdlog::level::trace);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
