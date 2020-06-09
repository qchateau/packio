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

    server->dispatcher()->add_async(
        "fibonacci", [&](packio::completion_handler complete, int n) {
            if (n <= 1) {
                complete(n);
                return;
            }

            client->async_call(
                "fibonacci",
                std::tuple{n - 1},
                packio::as<int>([n, &client, complete = std::move(complete)](
                                    packio::err::error_code,
                                    std::optional<int> r1) mutable {
                    client->async_call(
                        "fibonacci",
                        std::tuple{n - 2},
                        packio::as<int>([r1, complete = std::move(complete)](
                                            packio::err::error_code,
                                            std::optional<int> r2) mutable {
                            complete(*r1 + *r2);
                        }));
                }));
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
