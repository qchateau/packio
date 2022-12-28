#pragma once

#include <atomic>
#include <chrono>
#include <future>

#include <gtest/gtest.h>

#include <packio/packio.h>

#include <packio/extra/ssl.h>

#if __has_include(<filesystem>)
#include <filesystem>
#endif

#if !PACKIO_STANDALONE_ASIO
#include <packio/extra/websocket.h>
#endif // !PACKIO_STANDALONE_ASIO

#if PACKIO_HAS_MSGPACK
namespace default_rpc = packio::msgpack_rpc;
#elif PACKIO_HAS_NLOHMANN_JSON
namespace default_rpc = packio::nl_json_rpc;
#elif PACKIO_HAS_BOOST_JSON
namespace default_rpc = packio::json_rpc;
#else
#error No serialization library available
#endif

template <typename endpoint>
endpoint get_endpoint();

template <>
inline packio::net::ip::tcp::endpoint get_endpoint()
{
    return {packio::net::ip::make_address("127.0.0.1"), 0};
}

#if defined(PACKIO_HAS_LOCAL_SOCKETS)
template <>
inline packio::net::local::stream_protocol::endpoint get_endpoint()
{
    auto ts = std::chrono::system_clock::now().time_since_epoch().count();
#if __linux__ // gcc8 has <filesystem> but segfaults, just use hardcoded path on linux
    auto path = "/tmp/packio-" + std::to_string(ts);
#elif __has_include(<filesystem>)
    auto path = (std::filesystem::temp_directory_path()
                 / ("packio-" + std::to_string(ts)))
                    .string();
#else
#error "Need std::filesystem or a linux system"
#endif
    return {path};
}
#endif // defined(PACKIO_HAS_LOCAL_SOCKETS)

template <typename Socket>
constexpr bool supports_cancellation()
{
    return true;
}

class latch {
public:
    latch(int expected) : remaining_{expected} {}

    void count_down(int n = 1)
    {
        std::unique_lock lock{mutex_};
        remaining_ -= n;
        cv_.notify_all();
    }

    void count_up(int n = 1)
    {
        std::unique_lock lock{mutex_};
        remaining_ += n;
    }

    void reset(int n)
    {
        std::unique_lock lock{mutex_};
        remaining_ = n;
    }

    void wait()
    {
        std::unique_lock lock{mutex_};
        cv_.wait(lock, [this] { return remaining_ == 0; });
    }

    template <typename Duration>
    bool wait_for(Duration duration)
    {
        std::unique_lock lock{mutex_};
        return cv_.wait_for(lock, duration, [this] { return remaining_ == 0; });
    }

    template <typename Timepoint>
    bool wait_until(Timepoint timeout)
    {
        std::unique_lock lock{mutex_};
        return cv_.wait_until(lock, timeout, [this] { return remaining_ == 0; });
    }

private:
    mutable std::mutex mutex_;
    mutable std::condition_variable cv_;
    int remaining_;
};

template <typename T>
struct my_allocator : public std::allocator<T> {
    using std::allocator<T>::allocator;
};

class my_spinlock {
public:
    void lock()
    {
        while (locked_.exchange(true)) {
        }
    }

    void unlock() { locked_ = false; }

private:
    std::atomic<bool> locked_{false};
};

template <typename Key, typename T>
using my_unordered_map = std::unordered_map<
    Key,
    T,
    std::hash<Key>,
    std::equal_to<Key>,
    my_allocator<std::pair<const Key, T>>>;

#if PACKIO_HAS_MSGPACK
template <typename T>
T get(const ::msgpack::object& value)
{
    return value.as<T>();
}

inline std::string get_error_message(const ::msgpack::object& error)
{
    return error.as<std::string>();
}

inline bool is_error_response(const packio::msgpack_rpc::rpc::response_type& resp)
{
    return resp.result.is_nil() && !resp.error.is_nil();
}
#endif // PACKIO_HAS_MSGPACK

#if PACKIO_HAS_NLOHMANN_JSON
template <typename T>
T get(const ::nlohmann::json& value)
{
    return value.get<T>();
}

inline std::string get_error_message(const ::nlohmann::json& error)
{
    return error["message"].get<std::string>();
}

inline bool is_error_response(const packio::nl_json_rpc::rpc::response_type& resp)
{
    return resp.result.is_null() && !resp.error.is_null();
}
#endif // PACKIO_HAS_NLOHMANN_JSON

#if PACKIO_HAS_BOOST_JSON
template <typename T>
T get(const ::boost::json::value& value)
{
    return ::boost::json::value_to<T>(value);
}

inline std::string get_error_message(const ::boost::json::value& error)
{
    return error.as_object().at("message").as_string().c_str();
}

inline bool is_error_response(const packio::json_rpc::rpc::response_type& resp)
{
    return resp.result.is_null() && !resp.error.is_null();
}
#endif // PACKIO_HAS_BOOST_JSON

#define ASSERT_FUTURE_BLOCKS(fut, duration)                        \
    ASSERT_EQ(std::future_status::timeout, fut.wait_for(duration)) \
        << "future did not block"

#define ASSERT_FUTURE_NO_BLOCK(fut, duration)                    \
    ASSERT_EQ(std::future_status::ready, fut.wait_for(duration)) \
        << "future was not ready"

#define EXPECT_RESULT_EQ(fut, value)                                           \
    do {                                                                       \
        ASSERT_FUTURE_NO_BLOCK(fut, std::chrono::seconds{1});                  \
        auto res = fut.get();                                                  \
        EXPECT_FALSE(is_error_response(res));                                  \
        EXPECT_NO_THROW(                                                       \
            EXPECT_EQ(get<std::decay_t<decltype(value)>>(res.result), value)); \
    } while (false)

#define EXPECT_ERROR_EQ(fut, message)                                      \
    do {                                                                   \
        ASSERT_FUTURE_NO_BLOCK(fut, std::chrono::seconds{1});              \
        auto res = fut.get();                                              \
        EXPECT_TRUE(is_error_response(res));                               \
        EXPECT_NO_THROW(EXPECT_EQ(get_error_message(res.error), message)); \
    } while (false)

#define EXPECT_RESULT_IS_OK(fut)                                     \
    do {                                                             \
        ASSERT_FUTURE_NO_BLOCK(fut, std::chrono::seconds{1});        \
        EXPECT_NO_THROW(EXPECT_FALSE(is_error_response(fut.get()))); \
    } while (false)

#define EXPECT_RESULT_IS_ERROR(fut)                                 \
    do {                                                            \
        ASSERT_FUTURE_NO_BLOCK(fut, std::chrono::seconds{1});       \
        EXPECT_NO_THROW(EXPECT_TRUE(is_error_response(fut.get()))); \
    } while (false)

#define EXPECT_FUTURE_THROW(fut, exc)                         \
    do {                                                      \
        ASSERT_FUTURE_NO_BLOCK(fut, std::chrono::seconds{1}); \
        EXPECT_THROW(fut.get(), exc);                         \
    } while (false)

#define EXPECT_FUTURE_NO_THROW(fut)                           \
    do {                                                      \
        ASSERT_FUTURE_NO_BLOCK(fut, std::chrono::seconds{1}); \
        EXPECT_NO_THROW(fut.get());                           \
    } while (false)

#define EXPECT_FUTURE_CANCELLED(fut)                                  \
    do {                                                              \
        try {                                                         \
            ASSERT_FUTURE_NO_BLOCK(fut, std::chrono::seconds{1});     \
            fut.get();                                                \
            EXPECT_FALSE(true) << "future not cancelled as expected"; \
        }                                                             \
        catch (system_error & err) {                                  \
            EXPECT_EQ(net::error::operation_aborted, err.code());     \
        }                                                             \
    } while (false)

using test_ssl_stream = packio::extra::ssl_stream_adapter<
    packio::net::ssl::stream<packio::net::ip::tcp::socket>>;

class test_client_ssl_stream : public test_ssl_stream {
public:
    template <typename... Args>
    explicit test_client_ssl_stream(Args&&... args)
        : test_ssl_stream(
            packio::net::ip::tcp::socket(std::forward<Args>(args)...),
            context())
    {
    }

    template <typename Endpoint>
    void connect(Endpoint ep)
    {
        lowest_layer().connect(ep);
        handshake(client);
    }

private:
    static packio::net::ssl::context& context()
    {
        static packio::net::ssl::context ctx(packio::net::ssl::context::sslv23);
        ctx.set_verify_mode(packio::net::ssl::verify_none);
        return ctx;
    }
};

template <>
constexpr bool supports_cancellation<test_client_ssl_stream>()
{
    return false;
}

class test_ssl_acceptor
    : public packio::extra::ssl_acceptor_adapter<packio::net::ip::tcp::acceptor, test_ssl_stream> {
public:
    template <typename... Args>
    explicit test_ssl_acceptor(Args&&... args)
        : packio::extra::ssl_acceptor_adapter<packio::net::ip::tcp::acceptor, test_ssl_stream>(
            packio::net::ip::tcp::acceptor(std::forward<Args>(args)...),
            context())
    {
    }

private:
    static packio::net::ssl::context& context()
    {
        static packio::net::ssl::context ctx(packio::net::ssl::context::sslv23);
        ctx.use_certificate_chain_file("certs/server.cert");
        ctx.use_private_key_file(
            "certs/server.key", packio::net::ssl::context::pem);
        return ctx;
    }
};

#if !PACKIO_STANDALONE_ASIO
template <bool kBinary>
class test_websocket
    : public packio::extra::websocket_adapter<
          boost::beast::websocket::stream<boost::beast::tcp_stream>,
          kBinary> {
public:
    using packio::extra::websocket_adapter<
        boost::beast::websocket::stream<boost::beast::tcp_stream>,
        kBinary>::websocket_adapter;

    template <typename Endpoint>
    void connect(Endpoint ep)
    {
        this->next_layer().connect(ep);
        this->handshake("127.0.0.1:" + std::to_string(ep.port()), "/");
    }
};

template <>
constexpr bool supports_cancellation<test_websocket<true>>()
{
    return false;
}

template <>
constexpr bool supports_cancellation<test_websocket<false>>()
{
    return false;
}

template <bool kBinary>
using test_websocket_acceptor = packio::extra::websocket_acceptor_adapter<
    packio::net::ip::tcp::acceptor,
    test_websocket<kBinary>>;

#endif // !PACKIO_STANDALONE_ASIO

template <typename... Args>
struct test_types;

template <typename... Args>
struct test_types<std::tuple<Args...>> {
    using type = ::testing::Types<Args...>;
};

template <typename... Args>
using test_types_t = typename test_types<Args...>::type;

template <typename... Tuples>
using tuple_cat_t = decltype(std::tuple_cat(std::declval<Tuples>()...));

template <bool Condition, typename Tuple, typename... Tuples>
using tuple_cat_if_t =
    std::conditional_t<Condition, tuple_cat_t<Tuple, Tuples...>, Tuple>;

using implementations0 = std::tuple<

#if !PACKIO_STANDALONE_ASIO
    std::pair<
        default_rpc::client<test_websocket<true>>,
        default_rpc::server<test_websocket_acceptor<true>>>,
    std::pair<
        packio::nl_json_rpc::client<test_websocket<false>>,
        packio::nl_json_rpc::server<test_websocket_acceptor<false>>>,
#endif // !PACKIO_STANDALONE_ASIO

#if PACKIO_HAS_BOOST_JSON
    std::pair<
        packio::json_rpc::client<packio::net::ip::tcp::socket>,
        packio::json_rpc::server<packio::net::ip::tcp::acceptor>>,
#endif // PACKIO_HAS_BOOST_JSON

#if defined(PACKIO_HAS_LOCAL_SOCKETS)
    std::pair<
        default_rpc::client<packio::net::local::stream_protocol::socket>,
        default_rpc::server<packio::net::local::stream_protocol::acceptor>>,
#endif // defined(PACKIO_HAS_LOCAL_SOCKETS)

    std::pair<
        packio::msgpack_rpc::client<packio::net::ip::tcp::socket>,
        packio::msgpack_rpc::server<packio::net::ip::tcp::acceptor>>,
    std::pair<
        packio::msgpack_rpc::client<packio::net::ip::tcp::socket>,
        packio::msgpack_rpc::server<
            packio::net::ip::tcp::acceptor,
            packio::msgpack_rpc::dispatcher<std::map, my_spinlock>>>,
    std::pair<
        packio::msgpack_rpc::client<packio::net::ip::tcp::socket, my_unordered_map>,
        packio::msgpack_rpc::server<packio::net::ip::tcp::acceptor>>,

    std::pair<
        packio::nl_json_rpc::client<packio::net::ip::tcp::socket>,
        packio::nl_json_rpc::server<packio::net::ip::tcp::acceptor>>>;

using implementations_ssl = std::tuple<std::pair<
    default_rpc::client<test_client_ssl_stream>,
    default_rpc::server<test_ssl_acceptor>>>;

using implementations =
    tuple_cat_if_t<std::is_move_constructible_v<test_ssl_stream>, implementations0, implementations_ssl>;

using test_implementations = test_types_t<implementations>;
