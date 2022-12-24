#include "basic_test.h"

using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace packio::arg_literals;
using namespace packio::net;
using namespace packio;

TYPED_TEST(BasicTest, test_server_crash)
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

    this->async_run();
    this->connect();

    auto f = this->client_->async_call("close", use_future);
    EXPECT_FUTURE_THROW(f, std::exception);
}
