#include "basic_test.h"

using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace packio::arg_literals;
using namespace packio::net;
using namespace packio;

TYPED_TEST(BasicTest, test_functions)
{
    using completion_handler =
        typename std::decay_t<decltype(*this)>::completion_handler;
    using tuple_int_str = std::tuple<int, std::string>;
    using rpc_type = typename std::decay_t<decltype(*this)>::client_type::rpc_type;
    using native_type = typename rpc_type::native_type;

    this->server_->async_serve_forever();
    this->async_run();
    this->connect();

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
        [](completion_handler handler, std::tuple<int, std::string> tup) {
            handler(tup);
        });
    this->server_->dispatcher()->add_async(
        "async_tuple_int_strref",
        [](completion_handler handler,
           const std::tuple<int, std::string>& tup) { handler(tup); });
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
        "sync_tuple_int_str",
        [](std::tuple<int, std::string> tup) { return tup; });
    this->server_->dispatcher()->add(
        "sync_tuple_int_strref",
        [](const std::tuple<int, std::string>& tup) { return tup; });
    this->server_->dispatcher()->add(
        "sync_native", [](native_type native) { return native; });

    EXPECT_RESULT_IS_OK(this->client_->async_call("async_void_void", use_future));

    EXPECT_RESULT_EQ(this->client_->async_call("async_int_void", use_future), 42);
    EXPECT_RESULT_IS_OK(this->client_->async_call(
        "async_void_int", std::tuple{42}, use_future));
    EXPECT_RESULT_EQ(
        this->client_->async_call("async_int_int", std ::tuple{42}, use_future),
        42);
    EXPECT_RESULT_EQ(
        this->client_->async_call("async_int_intref", std::tuple{42}, use_future),
        42);
    EXPECT_RESULT_EQ(
        this->client_->async_call(
            "async_int_intref_int", std::tuple{42, 24}, use_future),
        42);
    EXPECT_RESULT_EQ(
        this->client_->async_call(
            "async_str_str", std::make_tuple("foobar"), use_future),
        "foobar"s);
    EXPECT_RESULT_EQ(
        this->client_->async_call(
            "async_str_strref", std::make_tuple("foobar"), use_future),
        "foobar"s);
    EXPECT_RESULT_EQ(
        this->client_->async_call(
            "async_tuple_int_str",
            std::make_tuple(std::make_tuple(42, "foobar")),
            use_future),
        tuple_int_str(42, "foobar"));
    EXPECT_RESULT_EQ(
        this->client_->async_call(
            "async_tuple_int_strref",
            std::make_tuple(std::make_tuple(42, "foobar")),
            use_future),
        tuple_int_str(42, "foobar"));
    EXPECT_RESULT_EQ(
        this->client_->async_call(
            "async_native", std::make_tuple("foobar"), use_future),
        "foobar"s);

    EXPECT_RESULT_IS_OK(this->client_->async_call("sync_void_void", use_future));
    EXPECT_RESULT_EQ(this->client_->async_call("sync_int_void", use_future), 42);
    EXPECT_RESULT_IS_OK(
        this->client_->async_call("sync_void_int", std::tuple{42}, use_future));
    EXPECT_RESULT_EQ(
        this->client_->async_call("sync_int_int", std::tuple{42}, use_future),
        42);
    EXPECT_RESULT_EQ(
        this->client_->async_call("sync_int_intref", std::tuple{42}, use_future),
        42);
    EXPECT_RESULT_EQ(
        this->client_->async_call(
            "sync_int_intref_int", std::tuple{42, 24}, use_future),
        42);
    EXPECT_RESULT_EQ(
        this->client_->async_call(
            "sync_str_str", std::make_tuple("foobar"), use_future),
        "foobar"s);
    EXPECT_RESULT_EQ(
        this->client_->async_call(
            "sync_str_strref", std::make_tuple("foobar"), use_future),
        "foobar"s);
    EXPECT_RESULT_EQ(
        this->client_->async_call(
            "sync_tuple_int_str",
            std::make_tuple(std::make_tuple(42, "foobar")),
            use_future),
        tuple_int_str(42, "foobar"));
    EXPECT_RESULT_EQ(
        this->client_->async_call(
            "sync_tuple_int_strref",
            std::make_tuple(std::make_tuple(42, "foobar")),
            use_future),
        tuple_int_str(42, "foobar"));
    EXPECT_RESULT_EQ(
        this->client_->async_call(
            "sync_native", std::make_tuple("foobar"), use_future),
        "foobar"s);
}
