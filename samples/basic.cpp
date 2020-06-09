#include <iostream>

#include <packio/packio.h>

namespace ip = packio::asio::ip;

int main(int, char**)
{
    // Declare a server and a client, sharing the same io_context
    packio::asio::io_context io;
    ip::tcp::endpoint bind_ep{ip::make_address("127.0.0.1"), 0};
    auto server = packio::make_server(ip::tcp::acceptor{io, bind_ep});
    auto client = packio::make_client(ip::tcp::socket{io});

    // Declare a synchronous callback
    server->dispatcher()->add("add", [](int a, int b) { return a + b; });
    // Declare an asynchronous callback
    server->dispatcher()->add_async(
        "multiply", [&io](packio::completion_handler complete, int a, int b) {
            // Call the completion handler later
            packio::asio::post(
                io, [a, b, complete = std::move(complete)]() mutable {
                    complete(a * b);
                });
        });

    // Connect the client
    client->socket().connect(server->acceptor().local_endpoint());
    // Accept connections
    server->async_serve_forever();
    // Run the io_context
    std::thread thread{[&] { io.run(); }};

    // Make an asynchronous call
    std::promise<int> add_result, multiply_result;
    client->async_call(
        "add",
        std::tuple{42, 24},
        [&](packio::err::error_code, msgpack::object_handle r) {
            add_result.set_value(r->as<int>());
        });
    std::cout << "42 + 24 = " << add_result.get_future().get() << std::endl;

    // Use packio::asio::use_future
    std::future<msgpack::object_handle> add_future = client->async_call(
        "add", std::tuple{12, 23}, packio::asio::use_future);
    std::cout << "12 + 23 = " << add_future.get()->as<int>() << std::endl;

    // Use auto result type conversion
    client->async_call(
        "multiply",
        std::tuple{42, 24},
        packio::as<int>([&](packio::err::error_code, std::optional<int> r) {
            multiply_result.set_value(*r);
        }));
    std::cout << "42 * 24 = " << multiply_result.get_future().get() << std::endl;

    io.stop();
    thread.join();

    return 0;
}
