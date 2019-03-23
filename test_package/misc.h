#ifndef RPCPACK_TESTS_MISC_H
#define RPCPACK_TESTS_MISC_H

#include <atomic>
#include <chrono>
#include <future>

#include <boost/asio.hpp>
#include <boost/filesystem.hpp>
#include <gtest/gtest.h>

template <typename endpoint>
void init_endpoint(endpoint& ep);

template <>
void init_endpoint(boost::asio::ip::tcp::endpoint& ep)
{
    ep = boost::asio::ip::tcp::endpoint(
        boost::asio::ip::make_address("127.0.0.1"), 0);
}

template <>
void init_endpoint(boost::asio::local::stream_protocol::endpoint& ep)
{
    boost::filesystem::path temp = boost::filesystem::unique_path();
    std::string sock('\0' + temp.string());
    ep = boost::asio::local::stream_protocol::endpoint(sock);
}

template <typename endpoint>
endpoint get_endpoint()
{
    endpoint ep;
    init_endpoint(ep);
    return ep;
}

template <typename Duration, typename F1, typename F2>
void expect_blocks(Duration d, const F1& func, const F2& timeout_handler)
{
    std::atomic<bool> done{false};
    auto handler = std::async(std::launch::async, [&] {
        std::this_thread::sleep_for(d);
        EXPECT_FALSE(done.load()) << "function did not block as expected";
        if (!done.load()) {
            timeout_handler();
        }
    });
    func();
    done.store(true);
    handler.wait();
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

#endif // RPCPACK_TESTS_MISC_H