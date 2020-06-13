#pragma once

#include <atomic>
#include <chrono>
#include <future>

#include <gtest/gtest.h>

#include <packio/packio.h>

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
