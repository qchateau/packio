#include "basic_test.h"

using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace packio::arg_literals;
using namespace packio::net;
using namespace packio;

TYPED_TEST(BasicTest, test_timeout)
{
    using completion_handler =
        typename std::decay_t<decltype(*this)>::completion_handler;
    using socket_type = typename std::decay_t<decltype(*this)>::socket_type;

    if constexpr (supports_cancellation<socket_type>()) {
        this->server_->async_serve_forever();
        this->async_run();
        this->connect();

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
            EXPECT_FUTURE_CANCELLED(f1);
            EXPECT_FUTURE_CANCELLED(f2);
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
            EXPECT_FUTURE_CANCELLED(f2);
            this->client_->cancel(id1);
            EXPECT_FUTURE_CANCELLED(f1);
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
            EXPECT_RESULT_IS_OK(this->client_->async_call("unblock", use_future));
            EXPECT_RESULT_IS_OK(f);
        }

        this->io_.stop();
    }
}
