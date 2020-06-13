
## Header-only | msgpack-RPC | asio | coroutines

This library requires C++17 and is designed as an extension to `boost.asio`. It will let you build asynchronous servers or client for msgpack-RPC.

The project is hosted on [GitHub](https://github.com/qchateau/packio/) and available on [Conan Center](https://conan.io/center/). Documentation is available on [GitHub Pages](https://qchateau.github.io/packio/).

## Primer

```cpp
#include <iostream>

#include <packio/packio.h>

int main(int, char**)
{
    // Declare a server and a client, sharing the same io_context
    packio::net::io_context io;
    packio::net::ip::tcp::endpoint bind_ep{
        packio::net::ip::make_address("127.0.0.1"), 0};
    auto server = packio::make_server(packio::net::ip::tcp::acceptor{io, bind_ep});
    auto client = packio::make_client(packio::net::ip::tcp::socket{io});

    // Declare a synchronous callback
    server->dispatcher()->add("add", [](int a, int b) { return a + b; });
    // Declare an asynchronous callback
    server->dispatcher()->add_async(
        "multiply", [&io](packio::completion_handler complete, int a, int b) {
            // Call the completion handler later
            packio::net::post(
                io, [a, b, complete = std::move(complete)]() mutable {
                    complete(a * b);
                });
        });
    // Declare a coroutine
    server->dispatcher()->add_coro(
        "pow", io, [](int a, int b) -> packio::net::awaitable<int> {
            co_return std::pow(a, b);
        });

    // Connect the client
    client->socket().connect(server->acceptor().local_endpoint());
    // Accept connections
    server->async_serve_forever();
    // Run the io_context
    std::thread thread{[&] { io.run(); }};

    // Make an asynchronous call
    std::promise<int> add1_result, multiply_result;
    client->async_call(
        "add",
        std::tuple{42, 24},
        [&](packio::error_code, msgpack::object_handle r) {
            add1_result.set_value(r->as<int>());
        });
    std::cout << "42 + 24 = " << add1_result.get_future().get() << std::endl;

    // Use auto result type conversion
    std::promise<int> add2_result;
    client->async_call(
        "add",
        std::tuple{11, 32},
        packio::as<int>([&](packio::error_code, std::optional<int> r) {
            add2_result.set_value(*r);
        }));
    std::cout << "11 + 32 = " << add2_result.get_future().get() << std::endl;

    // Use packio::net::use_future
    std::future<msgpack::object_handle> add_future = client->async_call(
        "multiply", std::tuple{12, 23}, packio::net::use_future);
    std::cout << "12 * 23 = " << add_future.get()->as<int>() << std::endl;

    // Spawn the coroutine and wait for its completion
    std::promise<int> pow_result;
    packio::net::co_spawn(
        io,
        [&]() -> packio::net::awaitable<void> {
            // Call using an awaitable
            msgpack::object_handle res = co_await client->async_call(
                "pow", std::tuple{2, 8}, packio::net::use_awaitable);
            pow_result.set_value(res->as<int>());
        },
        packio::net::detached);
    std::cout << "2 ** 8 = " << pow_result.get_future().get() << std::endl;

    io.stop();
    thread.join();

    return 0;
}
```

## Requirements

- C++17 or C++20
- msgpack >= 3.2.1
- boost.asio >= 1.70.0 or asio >= 1.13.0

### Standalone or boost asio

By default, `packio` uses `boost.asio`. It is also compatible with standalone `asio`. To use the standalone version, the preprocessor macro `PACKIO_STANDALONE_ASIO=1` must be defined.
If you are using the conan package, you must also use the option `standalone_asio=True`.

## Tested compilers

- gcc-7
- gcc-8
- gcc-9
- gcc-10
- clang-6
- clang-7
- clang-8
- clang-9
- clang-10
- Apple clang-11
- Visual Studio 2019 Version 16

Older compilers may be compatible but are not tested.

## Install with conan

```bash
conan install packio/x.x.x
```

## Coroutines

`packio` is compatible with C++20 coroutines:
- calls can use the `packio::asio::use_awaitable` completion token
- coroutines can be registered in the server

Coroutines are tested for the following compilers:
- clang-9 with libc++
- Visual Studio 2019 Version 16

## Bonus

Let's compute fibonacci's numbers recursively using `packio` and coroutines on a single thread.

```cpp
#include <iostream>

#include <packio/packio.h>

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
    auto server = packio::make_server(packio::net::ip::tcp::acceptor{io, bind_ep});
    auto client = packio::make_client(packio::net::use_awaitable_t<>::as_default_on(
        packio::net::ip::tcp::socket{io}));

    server->dispatcher()->add_coro(
        "fibonacci", io, [&](int n) -> packio::net::awaitable<int> {
            if (n <= 1) {
                co_return n;
            }

            auto r1 = co_await client->async_call("fibonacci", std::tuple{n - 1});
            auto r2 = co_await client->async_call("fibonacci", std::tuple{n - 2});

            co_return r1->as<int>() + r2->as<int>();
        });

    client->socket().connect(server->acceptor().local_endpoint());
    server->async_serve_forever();

    int result = 0;

    client->async_call(
        "fibonacci",
        std::tuple{n},
        packio::as<int>([&](packio::error_code, std::optional<int> r) {
            result = *r;
            io.stop();
        }));

    io.run();

    std::cout << "F{" << n << "} = " << result << std::endl;

    return 0;
}
```
