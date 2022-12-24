#include "basic_test.h"

using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace packio::arg_literals;
using namespace packio::net;
using namespace packio;

TYPED_TEST(BasicTest, test_response_after_disconnect)
{
    using completion_handler =
        typename std::decay_t<decltype(*this)>::completion_handler;

    this->server_->async_serve_forever();
    this->async_run();
    this->connect();

    // Use a unique_ptr here because MSVC's promise is bugged and required
    // default-constructible type
    std::promise<std::unique_ptr<completion_handler>> complete_promise;
    auto future = complete_promise.get_future();
    this->server_->dispatcher()->add_async(
        "block", [&](completion_handler complete) {
            complete_promise.set_value(
                std::make_unique<completion_handler>(std::move(complete)));
        });

    this->client_->async_call("block", [&](auto, auto) {});
    ASSERT_FUTURE_NO_BLOCK(future, std::chrono::seconds{1});
    this->client_->socket().close();
    (*future.get())();
}
