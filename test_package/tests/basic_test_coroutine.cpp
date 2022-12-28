#include "basic_test.h"

using namespace std::chrono_literals;
using namespace packio::net;

#if defined(PACKIO_HAS_CO_AWAIT) || defined(PACKIO_FORCE_COROUTINES)
TYPED_TEST(BasicTest, test_coroutine)
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
        this->io_, // executor context
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
    auto fut = p.get_future();
    EXPECT_FUTURE_NO_THROW(fut);
}
#endif // defined(PACKIO_HAS_CO_AWAIT) || defined(PACKIO_FORCE_COROUTINES)
