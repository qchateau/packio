#include "basic_test.h"

using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace packio::arg_literals;
using namespace packio::net;
using namespace packio;

TYPED_TEST(BasicTest, test_end_of_work)
{
    using client_type = typename std::decay_t<decltype(*this)>::client_type;
    using socket_type = typename std::decay_t<decltype(*this)>::socket_type;
    using id_type = typename client_type::id_type;
    using completion_handler =
        typename std::decay_t<decltype(*this)>::completion_handler;

    this->server_->async_serve_forever();
    std::vector<completion_handler> pending;
    this->server_->dispatcher()->add("func", []() {});
    this->server_->dispatcher()->add_async(
        "block", [&](completion_handler c) { pending.push_back(std::move(c)); });
    this->async_run();

    auto ep = this->server_->acceptor().local_endpoint();

    io_context io;
    auto client = std::make_shared<client_type>(socket_type{io});
    client->socket().connect(ep);

    // client runs out of work if there is no pending calls
    io.run_for(1s);
    ASSERT_TRUE(io.stopped());

    // client runs out of work after a notify
    io.restart();
    client->async_notify("func", [](auto) {});
    ASSERT_FALSE(io.stopped());
    io.run_for(1s);
    ASSERT_TRUE(io.stopped());

    // client runs out of work after a call
    io.restart();
    client->async_call("func", [](auto, auto) {});
    ASSERT_FALSE(io.stopped());
    io.run_for(1s);
    ASSERT_TRUE(io.stopped());

    // if this kind of socket does not support cancellation, stop here
    if constexpr (supports_cancellation<socket_type>()) {
        // client runs out of work after a cancelled call
        int cancelled_count{0};
        io.restart();
        ASSERT_FALSE(io.stopped());
        id_type id{};
        client->async_call(
            "block",
            [&](auto ec, auto) {
                ASSERT_TRUE(ec);
                cancelled_count++;
            },
            id);
        io.run_for(10ms);
        ASSERT_FALSE(io.stopped());
        client->cancel(id);
        io.run_for(1s);
        ASSERT_TRUE(io.stopped());

        // client runs out of work after multiple cancelled calls
        io.restart();
        client->async_call("block", [&](auto ec, auto) {
            ASSERT_TRUE(ec);
            cancelled_count++;
        });
        client->async_call("block", [&](auto ec, auto) {
            ASSERT_TRUE(ec);
            cancelled_count++;
        });
        io.run_for(10ms);
        ASSERT_FALSE(io.stopped());
        client->cancel();
        io.run_for(1s);
        ASSERT_TRUE(io.stopped());
        ASSERT_EQ(3, cancelled_count);
    }
}
