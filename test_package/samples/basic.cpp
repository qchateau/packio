#include <iostream>

#include <packio/packio.h>

int main(int, char**)
{
    // Declare a server and a client, sharing the same io_context
    packio::net::io_context io;
    packio::net::ip::tcp::endpoint bind_ep{
        packio::net::ip::make_address("127.0.0.1"), 0};
    auto server = packio::make_server(packio::net::ip::tcp::acceptor{io, bind_ep});
    auto client = packio::make_client(packio::net::ip::tcp::socket{io});

    // Declare a synchronous callback
    server->dispatcher()->add("add", [](int a, int b) { return a + b; });
    // Declare an asynchronous callback
    server->dispatcher()->add_async(
        "multiply", [&io](packio::completion_handler complete, int a, int b) {
            // Call the completion handler later
            packio::net::post(
                io, [a, b, complete = std::move(complete)]() mutable {
                    complete(a * b);
                });
        });
    // Declare a coroutine
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

    // Make an asynchronous call
    std::promise<int> add1_result, multiply_result;
    client->async_call(
        "add",
        std::tuple{42, 24},
        [&](packio::error_code, msgpack::object_handle r) {
            add1_result.set_value(r->as<int>());
        });
    std::cout << "42 + 24 = " << add1_result.get_future().get() << std::endl;

    // Use auto result type conversion
    std::promise<int> add2_result;
    client->async_call(
        "add",
        std::tuple{11, 32},
        packio::as<int>([&](packio::error_code, std::optional<int> r) {
            add2_result.set_value(*r);
        }));
    std::cout << "11 + 32 = " << add2_result.get_future().get() << std::endl;

    // Use packio::net::use_future
    std::future<msgpack::object_handle> add_future = client->async_call(
        "multiply", std::tuple{12, 23}, packio::net::use_future);
    std::cout << "12 * 23 = " << add_future.get()->as<int>() << std::endl;

    // Spawn the coroutine and wait for its completion
    std::promise<int> pow_result;
    packio::net::co_spawn(
        io,
        [&]() -> packio::net::awaitable<void> {
            // Call using an awaitable
            msgpack::object_handle res = co_await client->async_call(
                "pow", std::tuple{2, 8}, packio::net::use_awaitable);
            pow_result.set_value(res->as<int>());
        },
        packio::net::detached);
    std::cout << "2 ** 8 = " << pow_result.get_future().get() << std::endl;

    io.stop();
    thread.join();

    return 0;
}
