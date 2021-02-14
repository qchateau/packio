#include <iostream>

#include <packio/extra/websocket.h>
#include <packio/packio.h>

using packio::msgpack_rpc::make_client;
using packio::msgpack_rpc::make_server;
using packio::net::ip::make_address;

using awaitable_tcp_stream = decltype(packio::net::use_awaitable_t<>::as_default_on(
    std::declval<boost::beast::tcp_stream>()));
using websocket = packio::extra::
    websocket_adapter<boost::beast::websocket::stream<awaitable_tcp_stream>, true>;
using ws_acceptor =
    packio::extra::websocket_acceptor_adapter<packio::net::ip::tcp::acceptor, websocket>;

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cerr << "I require one argument" << std::endl;
        return 1;
    }
    const int n = std::atoi(argv[1]);

    packio::net::io_context io;
    packio::net::ip::tcp::endpoint bind_ep{make_address("127.0.0.1"), 0};

    auto server = make_server(ws_acceptor{io, bind_ep});
    auto client = make_client(websocket{io});

    server->dispatcher()->add_coro(
        "fibonacci", io, [&](int n) -> packio::net::awaitable<int> {
            if (n <= 1) {
                co_return n;
            }

            auto r1 = co_await client->async_call("fibonacci", std::tuple{n - 1});
            auto r2 = co_await client->async_call("fibonacci", std::tuple{n - 2});

            co_return r1.result.as<int>() + r2.result.as<int>();
        });

    int result = 0;
    packio::net::co_spawn(
        io,
        [&]() -> packio::net::awaitable<void> {
            auto ep = server->acceptor().local_endpoint();
            co_await client->socket().next_layer().async_connect(ep);
            co_await client->socket().async_handshake(
                "127.0.0.1:" + std::to_string(ep.port()), "/");
            auto ret = co_await client->async_call("fibonacci", std::tuple{n});
            result = ret.result.template as<int>();
            io.stop();
        },
        packio::net::detached);

    server->async_serve_forever();
    io.run();

    std::cout << "F{" << n << "} = " << result << std::endl;

    return 0;
}
