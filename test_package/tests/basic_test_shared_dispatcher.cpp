#include "basic_test.h"

using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace packio::arg_literals;
using namespace packio::net;
using namespace packio;

TYPED_TEST(BasicTest, test_shared_dispatcher)
{
    using server_type = typename std::decay_t<decltype(*this)>::server_type;
    using client_type = typename std::decay_t<decltype(*this)>::client_type;
    using socket_type = typename std::decay_t<decltype(*this)>::socket_type;
    using endpoint_type = typename std::decay_t<decltype(*this)>::endpoint_type;
    using acceptor_type = typename std::decay_t<decltype(*this)>::acceptor_type;
    using completion_handler =
        typename std::decay_t<decltype(*this)>::completion_handler;

    this->server_->async_serve_forever();
    this->async_run();
    this->connect();

    // server2 is a different server but shares the same dispatcher as server_
    auto server2 = std::make_shared<server_type>(
        acceptor_type(this->io_, get_endpoint<endpoint_type>()),
        this->server_->dispatcher());

    auto client2 = std::make_shared<client_type>(socket_type{this->io_});

    auto ep = server2->acceptor().local_endpoint();
    server2->async_serve_forever();
    client2->socket().connect(ep);

    ASSERT_NE(
        this->server_->acceptor().local_endpoint(),
        server2->acceptor().local_endpoint());

    latch l{2};
    ASSERT_TRUE(this->server_->dispatcher()->add_async(
        "inc", [&](completion_handler handler) {
            l.count_down();
            handler();
        }));

    this->client_->async_notify("inc", [](auto ec) { ASSERT_FALSE(ec); });
    client2->async_notify("inc", [](auto ec) { ASSERT_FALSE(ec); });

    ASSERT_TRUE(l.wait_for(1s));
}
