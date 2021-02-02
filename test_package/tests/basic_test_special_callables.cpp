#include "basic_test.h"

using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace packio::arg_literals;
using namespace packio::net;
using namespace packio;

TYPED_TEST(BasicTest, test_special_callables)
{
    using session_type =
        typename decltype(this->server_)::element_type::session_type;
    using completion_handler =
        typename std::decay_t<decltype(*this)>::completion_handler;
    using rpc_type = typename std::decay_t<decltype(*this)>::client_type::rpc_type;

    struct move_only {
        move_only() = default;

        move_only(const move_only&) = delete;
        move_only& operator=(const move_only&) = delete;

        move_only(move_only&&) = default;
        move_only& operator=(move_only&&) = default;
    };
    struct sync_procedure : public move_only {
        void operator()(){};
    };
    struct async_procedure : public move_only {
        void operator()(completion_handler complete) { complete(); };
    };
    struct notify_handler : public move_only {
        void operator()(error_code){};
    };
    struct call_handler : public move_only {
        void operator()(error_code, typename rpc_type::response_type){};
    };
    struct serve_handler : public move_only {
        void operator()(error_code, std::shared_ptr<session_type>){};
    };

    // this test just needs to compile
    // test with move-only lambdas
    // and with move-only callables that have non-const operator()

    this->server_->dispatcher()->add_async(
        "move_only_lambda_async_001",
        [ptr = std::unique_ptr<int>{}](completion_handler) {});
    this->server_->dispatcher()->add(
        "move_only_lambda_sync_001", [ptr = std::unique_ptr<int>{}]() {});

    this->server_->dispatcher()->add_async(
        "move_only_callable_async_001", async_procedure{});
    this->server_->dispatcher()->add(
        "move_only_callable_sync_001", sync_procedure{});

    this->server_->async_serve([ptr = std::unique_ptr<int>{}](auto, auto) {});
    this->server_->async_serve(serve_handler{});

    this->server_->dispatcher()->add("f", sync_procedure{});
    this->client_->async_notify("f", [ptr = std::unique_ptr<int>{}](auto) {});
    this->client_->async_notify("f", notify_handler{});
    this->client_->async_call("f", [ptr = std::unique_ptr<int>{}](auto, auto) {});
    this->client_->async_call("f", call_handler{});

    static_assert(std::is_move_assignable_v<completion_handler>);
    static_assert(std::is_move_constructible_v<completion_handler>);
}
