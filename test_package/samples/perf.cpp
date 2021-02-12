#include <iostream>

#include <packio/packio.h>

#if !PACKIO_STANDALONE_ASIO
#include <packio/extra/websocket.h>

class websocket : public packio::extra::websocket_adapter<
                      boost::beast::websocket::stream<boost::beast::tcp_stream>> {
public:
    template <typename... Args>
    explicit websocket(Args&&... args)
        : packio::extra::websocket_adapter<
            boost::beast::websocket::stream<boost::beast::tcp_stream>>(
            std::forward<Args>(args)...)
    {
        binary(true);
    }

    template <typename Endpoint>
    void connect(Endpoint ep)
    {
        next_layer().connect(ep);
        handshake("127.0.0.1:" + std::to_string(ep.port()), "/");
    }
};

using websocket_acceptor =
    packio::extra::websocket_acceptor_adapter<packio::net::ip::tcp::acceptor, websocket>;
#endif // !PACKIO_STANDALONE_ASIO

constexpr auto N = 10000;

template <typename S, typename C>
std::chrono::nanoseconds benchmark(S& server, C& client)
{
    server->dispatcher()->add("fct", [&](int a, const std::string& b, double c) {
        return std::make_tuple(a, b, c);
    });
    server->async_serve_forever();
    client->socket().connect(server->acceptor().local_endpoint());

    auto args = std::make_tuple(12, "this is a string", 24.0);
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < N; ++i) {
        client->async_call("fct", args, packio::net::use_future).get();
    }
    return std::chrono::steady_clock::now() - start;
}

template <typename Duration>
void print_result(const std::string& name, const Duration& res, const Duration& best)
{
    std::cout << std::fixed
              << std::setprecision(6)
              // name
              << std::left << std::setw(10) << name
              << ": "
              // time
              << std::right << std::setw(10) << std::setprecision(6)
              << res.count() / 1e9
              << "s "
              // percentage
              << "(+" << std::setw(3) << std::setprecision(0)
              << std::round((100.0 * res.count() / best.count()) - 100) << "%)"
              << std::endl;
}

int main(int, char**)
{
    using namespace packio::arg_literals;

    // Declare a server and a client, sharing the same io_context
    packio::net::io_context io;
    packio::net::ip::tcp::endpoint bind_ep{
        packio::net::ip::make_address("127.0.0.1"), 0};

    auto wg = packio::net::make_work_guard(io);
    std::thread thread{[&] { io.run(); }};

    std::cout << "Running benchmarks ..." << std::endl;
    std::chrono::nanoseconds best = std::chrono::hours(1);

#if PACKIO_HAS_MSGPACK
    std::cout << "  msgpack ..." << std::endl;
    auto msgpack_server = packio::msgpack_rpc::make_server(
        packio::net::ip::tcp::acceptor{io, bind_ep});
    auto msgpack_client = packio::msgpack_rpc::make_client(
        packio::net::ip::tcp::socket{io});
    const auto msgpack_time = benchmark(msgpack_server, msgpack_client);
    best = std::min(best, msgpack_time);
#endif // PACKIO_HAS_MSGPACK

#if PACKIO_HAS_NLOHMANN_JSON
    std::cout << "  nlohmann json ..." << std::endl;
    auto nl_json_server = packio::nl_json_rpc::make_server(
        packio::net::ip::tcp::acceptor{io, bind_ep});
    auto nl_json_client = packio::nl_json_rpc::make_client(
        packio::net::ip::tcp::socket{io});
    const auto nl_json_time = benchmark(nl_json_server, nl_json_client);
    best = std::min(best, nl_json_time);
#endif // PACKIO_HAS_NLOHMANN_JSON

#if PACKIO_HAS_BOOST_JSON
    std::cout << "  boost json ..." << std::endl;
    auto json_server = packio::json_rpc::make_server(
        packio::net::ip::tcp::acceptor{io, bind_ep});
    auto json_client = packio::json_rpc::make_client(
        packio::net::ip::tcp::socket{io});
    const auto json_time = benchmark(json_server, json_client);
    best = std::min(best, json_time);
#endif // PACKIO_HAS_BOOST_JSON

#if !PACKIO_STANDALONE_ASIO
    std::cout << "  msgpack over websockets ..." << std::endl;
    auto ws_server = packio::msgpack_rpc::make_server(
        websocket_acceptor{io, bind_ep});
    auto ws_client = packio::msgpack_rpc::make_client(websocket{io});
    const auto ws_time = benchmark(ws_server, ws_client);
    best = std::min(best, ws_time);
#endif // !PACKIO_STANDALONE_ASIO

#if PACKIO_HAS_MSGPACK
    print_result("msgpack", msgpack_time, best);
#endif // PACKIO_HAS_MSGPACK
#if PACKIO_HAS_NLOHMANN_JSON
    print_result("nl_json", nl_json_time, best);
#endif // PACKIO_HAS_NLOHMANN_JSON
#if PACKIO_HAS_BOOST_JSON
    print_result("json", json_time, best);
#endif // PACKIO_HAS_NLOHMANN_JSON
#if !PACKIO_STANDALONE_ASIO
    print_result("ws", ws_time, best);
#endif // !PACKIO_STANDALONE_ASIO

    io.stop();
    thread.join();

    return 0;
}
