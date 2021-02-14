#include <iostream>

#include <packio/extra/ssl.h>
#include <packio/packio.h>

namespace ssl = packio::net::ssl;
using packio::net::ip::make_address;
using packio::nl_json_rpc::make_client;
using packio::nl_json_rpc::make_server;
using packio::nl_json_rpc::rpc;

using tcp_socket = packio::net::ip::tcp::socket;
using ssl_socket = packio::extra::ssl_stream_adapter<ssl::stream<tcp_socket>>;
using tcp_acceptor = packio::net::ip::tcp::acceptor;
using ssl_acceptor = packio::extra::ssl_acceptor_adapter<tcp_acceptor, ssl_socket>;

int main(int, char**)
{
    // Create the server SSL context
    ssl::context server_ctx(ssl::context::sslv23);
    server_ctx.use_certificate_chain_file("certs/server.cert");
    server_ctx.use_private_key_file("certs/server.key", ssl::context::pem);

    // Create the client SSL context
    ssl::context client_ctx(ssl::context::sslv23);
    client_ctx.set_verify_mode(ssl::verify_none);

    // Declare a server and a client, sharing the same io_context
    packio::net::io_context io;
    packio::net::ip::tcp::endpoint bind_ep{make_address("127.0.0.1"), 0};

    auto server = make_server(ssl_acceptor{tcp_acceptor{io, bind_ep}, server_ctx});
    auto client = make_client(ssl_socket{io, client_ctx});

    server->dispatcher()->add("add", [](int a, int b) { return a + b; });

    // Accept connections
    server->async_serve_forever();

    // Connect the socket, perform SSL handshake and call "add"
    auto on_call = [&](packio::error_code, const rpc::response_type& r) {
        std::cout << "42 + 24 = " << r.result.get<int>() << std::endl;
        io.stop();
    };
    auto on_handshake = [&](packio::error_code) {
        client->async_call("add", std::tuple{42, 24}, on_call);
    };
    auto on_connect = [&](packio::error_code) {
        client->socket().async_handshake(ssl_socket::client, on_handshake);
    };
    client->socket().lowest_layer().async_connect(
        server->acceptor().local_endpoint(), on_connect);

    // Run the io_context
    io.run();

    return 0;
}
