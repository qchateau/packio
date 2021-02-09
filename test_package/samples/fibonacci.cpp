#include <iostream>

#include <packio/extra/ssl.h>
#include <packio/packio.h>

namespace ssl = packio::net::ssl;
using packio::msgpack_rpc::make_client;
using packio::msgpack_rpc::make_server;
using packio::net::ip::make_address;

using tcp_socket = packio::net::ip::tcp::socket;
using awaitable_tcp_socket = decltype(packio::net::use_awaitable_t<>::as_default_on(
    std::declval<tcp_socket>()));
using ssl_socket =
    packio::extra::ssl_stream_adapter<ssl::stream<awaitable_tcp_socket>>;
using tcp_acceptor = packio::net::ip::tcp::acceptor;
using ssl_acceptor = packio::extra::ssl_acceptor_adapter<tcp_acceptor, ssl_socket>;

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cerr << "I require one argument" << std::endl;
        return 1;
    }
    const int n = std::atoi(argv[1]);

    ssl::context server_ctx(ssl::context::sslv23);
    server_ctx.use_certificate_chain_file("certs/server.cert");
    server_ctx.use_private_key_file("certs/server.key", ssl::context::pem);

    ssl::context client_ctx(ssl::context::sslv23);
    client_ctx.set_verify_mode(ssl::verify_none);

    packio::net::io_context io;
    packio::net::ip::tcp::endpoint bind_ep{make_address("127.0.0.1"), 0};

    auto server = make_server(ssl_acceptor{tcp_acceptor{io, bind_ep}, server_ctx});
    auto client = make_client(ssl_socket{io, client_ctx});

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
            co_await client->socket().lowest_layer().async_connect(
                server->acceptor().local_endpoint());
            co_await client->socket().async_handshake(ssl_socket::client);
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
