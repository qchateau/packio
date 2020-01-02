#include <iostream>

#include <packio/client.h>
#include <packio/server.h>

namespace ip = boost::asio::ip;

int main(int, char**)
{
    // Declare a server and a client, sharing the same io_context
    boost::asio::io_context io;
    ip::tcp::endpoint bind_ep{ip::make_address("127.0.0.1"), 0};
    auto server = std::make_shared<packio::server<ip::tcp>>(
        ip::tcp::acceptor{io, bind_ep});
    auto client = std::make_shared<packio::client<ip::tcp>>(ip::tcp::socket{io});

    // Declare a synchronous callback
    server->dispatcher()->add("add", [](int a, int b) { return a + b; });
    // Declare an asynchronous callback
    server->dispatcher()->add_async(
        "multiply", [](packio::completion_handler complete, int a, int b) {
            complete(a * b);
        });

    // Connect the client
    client->socket().connect(server->acceptor().local_endpoint());
    // Accept connections
    server->async_serve_forever();
    // Run the io_context
    std::thread thread{[&] { io.run(); }};

    // Make an asynchronous calls
    std::promise<int> add_result, multiply_result;
    client->async_call(
        "add",
        std::make_tuple(42, 24),
        [&](boost::system::error_code, msgpack::object_handle r) {
            add_result.set_value(r->as<int>());
        });

    client->async_call(
        "multiply",
        std::make_tuple(42, 24),
        [&](boost::system::error_code, msgpack::object_handle r) {
            multiply_result.set_value(r->as<int>());
        });

    std::cout << "42 + 24 = " << add_result.get_future().get() << std::endl;
    std::cout << "42 * 24 = " << multiply_result.get_future().get() << std::endl;

    io.stop();
    thread.join();

    return 0;
}