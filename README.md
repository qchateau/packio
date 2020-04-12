
## Header-only | msgpack-RPC | asio | Coroutines

This library requires C++17 and is designed as an extension to ``boost.asio``. It will let you build asynchronous servers or client for msgpack-RPC.

The project is hosted on [GitHub](https://github.com/qchateau/packio/) and available on [Conan Center](https://conan.io/center/). Documentation is available on [GitHub Pages](https://qchateau.github.io/packio/).

## Primer

```cpp
// Declare a server and a client, sharing the same io_context
packio::asio::io_context io;
ip::tcp::endpoint bind_ep{ip::make_address("127.0.0.1"), 0};
auto server = packio::make_server(ip::tcp::acceptor{io, bind_ep});
auto client = packio::make_client(ip::tcp::socket{io});

// Declare a synchronous callback
server->dispatcher()->add("add", [](int a, int b) { return a + b; });
// Declare an asynchronous callback
server->dispatcher()->add_async(
    "multiply", [](packio::completion_handler complete, int a, int b) {
        // Call the completion handler later
        packio::asio::post(
            io, [a, b, complete = std::move(complete)]() mutable {
                complete(a * b);
            });
    });

// Accept connections forever
server->async_serve_forever();
// Connect the client
client->socket().connect(server.acceptor().local_endpoint());

// Make an asynchronous call
client->async_call("add", std::make_tuple(42, 24),
    [&](packio::err::error_code, msgpack::object r) {
        std::cout << "The result is: " << r.as<int>() << std::endl;
    });

// Use packio::asio::use_future
std::future<msgpack::object_handle> add_future = client->async_call(
    "add", std::tuple{12, 23}, packio::asio::use_future);
std::cout << "The result is: " << add_future.get()->as<int>() << std::endl;

// Use auto result type conversion
client->async_call(
    "multiply",
    std::make_tuple(42, 24),
    [&](packio::err::error_code, std::optional<int> r) {
        std::cout << "The result is: " << *r << std::endl;
    });
```

## Requirements

- C++17
- msgpack >= 3.2.1
- boost.asio >= 1.70.0 or asio >= 1.13.0

### Standalone or boost asio

By default, ``packio`` uses ``boost.asio``. It is also compatible with standalone ``asio``. To use the standalone version, the preprocessor macro ``PACKIO_STANDALONE_ASIO=1` must be defined.
If you are using the conan package, you must also use the option ``standalone_asio=True``.

## Tested compilers

- gcc-7
- gcc-8
- gcc-9
- clang-5
- clang-6
- clang-7
- clang-8
- clang-9
- Apple clang-10
- Apple clang-11
- Visual Studio 2019 Version 16

## Install with conan

```bash
conan install packio/1.2.0
```

## Coroutines

``packio`` is compatible with C++20 coroutines:
- calls can use the ``packio::asio::use_awaitable`` completion token
- coroutines can be registered in the server

Coroutines are tested for the following compilers:
- clang-9 with libc++
- Visual Studio 2019 Version 16

## Bonus

Let's compute fibonacci's numbers recursively using ``packio`` and coroutines on a single thread.

```cpp
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
        [&](packio::err::error_code, std::optional<int> r) {
            result = *r;
            io.stop();
        });

    io.run();

    std::cout << "F{" << n << "} = " << result << std::endl;

    return 0;
}
```
