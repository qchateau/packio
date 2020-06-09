#include "tests.h"

TYPED_TEST(Test, test_coroutine)
{
    using packio::asio::use_awaitable;

    packio::asio::steady_timer timer{this->io_};

    this->server_->dispatcher()->add_coro(
        "add",
        this->io_.get_executor(), // executor
        [&](int a, int b) -> packio::asio::awaitable<int> {
            timer.expires_after(std::chrono::milliseconds{1});
            co_await timer.async_wait(use_awaitable);
            co_return a + b;
        });

    this->server_->dispatcher()->add_coro(
        "add2",
        this->io_, // executor context
        [&](int a, int b) -> packio::asio::awaitable<int> {
            timer.expires_after(std::chrono::milliseconds{1});
            co_await timer.async_wait(use_awaitable);
            co_return a + b;
        });

    packio::asio::co_spawn(
        this->io_,
        [&]() -> packio::asio::awaitable<void> {
            while (true) {
                auto session = co_await this->server_->async_serve(use_awaitable);
                session->start();
            }
        },
        packio::asio::detached);

    this->async_run();
    this->connect();

    std::promise<void> p;
    packio::asio::co_spawn(
        this->io_,
        [&]() -> packio::asio::awaitable<void> {
            // Call using an awaitable
            msgpack::object_handle res = co_await this->client_->async_call(
                "add", std::tuple{12, 23}, use_awaitable);
            EXPECT_EQ(res->as<int>(), 35);

            res = co_await this->client_->async_call(
                "add2", std::tuple{31, 3}, use_awaitable);
            EXPECT_EQ(res->as<int>(), 34);

            p.set_value();
        },
        packio::asio::detached);
    ASSERT_EQ(
        p.get_future().wait_for(std::chrono::seconds{1}),
        std::future_status::ready);
}
