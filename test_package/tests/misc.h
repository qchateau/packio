#pragma once

#include <atomic>
#include <chrono>
#include <future>

#include <gtest/gtest.h>

#include <packio/packio.h>

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
    // FIXME: make this cross platform
    return {"/tmp/packio-" + std::to_string(ts)};
}
#endif // defined(PACKIO_HAS_LOCAL_SOCKETS)

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

template <typename Future>
decltype(auto) safe_future_get(Future&& fut)
{
    if (fut.wait_for(std::chrono::seconds{1}) != std::future_status::ready) {
        throw std::runtime_error{"future was not ready"};
    }
    return fut.get();
}

#define ASSERT_RESULT_EQ(fut, value) \
    ASSERT_EQ(                       \
        get<std::decay_t<decltype(value)>>(safe_future_get(fut).result), value)

#define ASSERT_RESULT_IS_OK(fut) \
    ASSERT_FALSE(is_error_response(safe_future_get(fut)))

#define ASSERT_RESULT_IS_ERROR(fut) \
    ASSERT_TRUE(is_error_response(safe_future_get(fut)))

#define ASSERT_FUTURE_THROW(fut, exc) \
    ASSERT_THROW([&] { return safe_future_get(fut); }(), exc)

#define ASSERT_FUTURE_NO_THROW(fut) \
    ASSERT_NO_THROW([&] { return safe_future_get(fut); }())

#define ASSERT_FUTURE_BLOCKS(fut, duration) \
    ASSERT_EQ(std::future_status::timeout, fut.wait_for(duration))

#define ASSERT_FUTURE_CANCELLED(fut)                              \
    do {                                                          \
        try {                                                     \
            safe_future_get(fut);                                 \
            ASSERT_FALSE(true);                                   \
        }                                                         \
        catch (system_error & err) {                              \
            ASSERT_EQ(net::error::operation_aborted, err.code()); \
        }                                                         \
    } while (false)
