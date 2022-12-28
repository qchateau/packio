#include <iostream>

#include <packio/packio.h>

using packio::allow_extra_arguments;
using packio::arg;
using packio::nl_json_rpc::completion_handler;
using packio::nl_json_rpc::make_client;
using packio::nl_json_rpc::make_server;
using packio::nl_json_rpc::rpc;
using namespace packio::arg_literals;

int main(int, char**)
{
    using namespace packio::arg_literals;

    // Declare a server and a client, sharing the same io_context
    packio::net::io_context io;
    packio::net::ip::tcp::endpoint bind_ep{
        packio::net::ip::make_address("127.0.0.1"), 0};
    auto server = make_server(packio::net::ip::tcp::acceptor{io, bind_ep});
    auto client = make_client(packio::net::ip::tcp::socket{io});

    // Declare a synchronous callback with named arguments
    server->dispatcher()->add(
        "add", {"a", "b"}, [](int a, int b) { return a + b; });
    // Declare an asynchronous callback with named arguments,
    // an argument with a default value and an option to
    // accept and discard extra arguments
    server->dispatcher()->add_async(
        "multiply",
        {allow_extra_arguments, "a", "b"_arg = 2},
        [&io](completion_handler complete, int a, int b) {
            // Call the completion handler later
            packio::net::post(
                io, [a, b, complete = std::move(complete)]() mutable {
                    complete(a * b);
                });
        });
    // Declare a coroutine with unnamed arguments
    server->dispatcher()->add_coro(
        "pow", io, [](int a, int b) -> packio::net::awaitable<int> {
            co_return std::pow(a, b);
        });

    // Connect the client
    client->socket().connect(server->acceptor().local_endpoint());
    // Accept connections
    server->async_serve_forever();
    // Run the io_context
    std::thread thread{[&] { io.run(); }};

    // Make an asynchronous call with named arguments
    // using either `packio::arg` or `packio::arg_literals`
    std::promise<int> add1_result, multiply_result;
    client->async_call(
        "add",
        std::tuple{arg("a") = 42, "b"_arg = 24},
        [&](packio::error_code, const rpc::response_type& r) {
            add1_result.set_value(r.result.get<int>());
        });
    std::cout << "42 + 24 = " << add1_result.get_future().get() << std::endl;

    // Use packio::net::use_future with named arguments and literals
    auto add_future = client->async_call(
        "multiply",
        std::tuple{"a"_arg = 12, "b"_arg = 23},
        packio::net::use_future);
    std::cout << "12 * 23 = " << add_future.get().result.get<int>() << std::endl;

    // Spawn the coroutine and wait for its completion
    std::promise<int> pow_result;
    packio::net::co_spawn(
        io,
        [&]() -> packio::net::awaitable<void> {
            // Call using an awaitable and positional arguments
            auto res = co_await client->async_call(
                "pow", std::tuple{2, 8}, packio::net::use_awaitable);
            pow_result.set_value(res.result.get<int>());
        },
        packio::net::detached);
    std::cout << "2 ** 8 = " << pow_result.get_future().get() << std::endl;

    io.stop();
    thread.join();

    return 0;
}
