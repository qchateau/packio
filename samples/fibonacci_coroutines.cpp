#include <iostream>

#include <packio/packio.h>

namespace ip = packio::asio::ip;

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cerr << "I require one argument" << std::endl;
        return 1;
    }
    const int n = std::atoi(argv[1]);

    packio::asio::io_context io;
    ip::tcp::endpoint bind_ep{ip::make_address("127.0.0.1"), 0};
    auto server = packio::make_server(ip::tcp::acceptor{io, bind_ep});
    auto client = packio::make_client(ip::tcp::socket{io});

    server->dispatcher()->add_coro(
        "fibonacci", io, [&](int n) -> packio::asio::awaitable<int> {
            if (n <= 1) {
                co_return n;
            }

            msgpack::object_handle r1 = co_await client->async_call(
                "fibonacci", std::tuple{n - 1}, packio::asio::use_awaitable);
            msgpack::object_handle r2 = co_await client->async_call(
                "fibonacci", std::tuple{n - 2}, packio::asio::use_awaitable);

            co_return r1->as<int>() + r2->as<int>();
        });

    client->socket().connect(server->acceptor().local_endpoint());
    server->async_serve_forever();

    int result = 0;

    client->async_call(
        "fibonacci",
        std::tuple{n},
        packio::as<int>([&](packio::err::error_code, std::optional<int> r) {
            result = *r;
            io.stop();
        }));

    io.run();

    std::cout << "F{" << n << "} = " << result << std::endl;

    return 0;
}
