#include <iostream>

#include <packio/packio.h>

using packio::msgpack_rpc::make_client;
using packio::msgpack_rpc::make_server;

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cerr << "I require one argument" << std::endl;
        return 1;
    }
    const int n = std::atoi(argv[1]);

    packio::net::io_context io;
    packio::net::ip::tcp::endpoint bind_ep{
        packio::net::ip::make_address("127.0.0.1"), 0};
    auto server = make_server(packio::net::ip::tcp::acceptor{io, bind_ep});
    auto client = make_client(packio::net::use_awaitable_t<>::as_default_on(
        packio::net::ip::tcp::socket{io}));

    server->dispatcher()->add_coro(
        "fibonacci", io, [&](int n) -> packio::net::awaitable<int> {
            if (n <= 1) {
                co_return n;
            }

            auto r1 = co_await client->async_call("fibonacci", std::tuple{n - 1});
            auto r2 = co_await client->async_call("fibonacci", std::tuple{n - 2});

            co_return r1.result.as<int>() + r2.result.as<int>();
        });

    client->socket().connect(server->acceptor().local_endpoint());
    server->async_serve_forever();

    int result = 0;

    client->async_call(
        "fibonacci", std::tuple{n}, [&](packio::error_code, auto r) {
            result = r.result.template as<int>();
            io.stop();
        });

    io.run();

    std::cout << "F{" << n << "} = " << result << std::endl;

    return 0;
}
