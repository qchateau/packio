## Header-only | JSON-RPC | msgpack-RPC | asio | coroutines

This library requires C++17 and is designed as an extension to `boost.asio`. It will let you build asynchronous servers or client for JSON-RPC or msgpack-RPC.

The project is hosted on [GitHub](https://github.com/qchateau/packio/) and available on [Conan Center](https://conan.io/center/). Documentation is available on [GitHub Pages](https://qchateau.github.io/packio/).

## Overview

```cpp
#include <iostream>

#include <packio/packio.h>

using packio::allow_extra_arguments;
using packio::arg;
using packio::nl_json_rpc::completion_handler;
using packio::nl_json_rpc::make_client;
using packio::nl_json_rpc::make_server;
using packio::nl_json_rpc::rpc;
using namespace packio::arg_literals;

int main(int, char**)
{
    using namespace packio::arg_literals;

    // Declare a server and a client, sharing the same io_context
    packio::net::io_context io;
    packio::net::ip::tcp::endpoint bind_ep{
        packio::net::ip::make_address("127.0.0.1"), 0};
    auto server = make_server(packio::net::ip::tcp::acceptor{io, bind_ep});
    auto client = make_client(packio::net::ip::tcp::socket{io});

    // Declare a synchronous callback with named arguments
    server->dispatcher()->add(
        "add", {"a", "b"}, [](int a, int b) { return a + b; });
    // Declare an asynchronous callback with named arguments,
    // an argument with a default value and an option to
    // accept and discard extra arguments
    server->dispatcher()->add_async(
        "multiply",
        {allow_extra_arguments, "a", "b"_arg = 2},
        [&io](completion_handler complete, int a, int b) {
            // Call the completion handler later
            packio::net::post(
                io, [a, b, complete = std::move(complete)]() mutable {
                    complete(a * b);
                });
        });
    // Declare a coroutine with unnamed arguments
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

    // Make an asynchronous call with named arguments
    // using either `packio::arg` or `packio::arg_literals`
    std::promise<int> add1_result, multiply_result;
    client->async_call(
        "add",
        std::tuple{arg("a") = 42, "b"_arg = 24},
        [&](packio::error_code, const rpc::response_type& r) {
            add1_result.set_value(r.result.get<int>());
        });
    std::cout << "42 + 24 = " << add1_result.get_future().get() << std::endl;

    // Use packio::net::use_future with named arguments and literals
    auto add_future = client->async_call(
        "multiply",
        std::tuple{"a"_arg = 12, "b"_arg = 23},
        packio::net::use_future);
    std::cout << "12 * 23 = " << add_future.get().result.get<int>() << std::endl;

    // Spawn the coroutine and wait for its completion
    std::promise<int> pow_result;
    packio::net::co_spawn(
        io,
        [&]() -> packio::net::awaitable<void> {
            // Call using an awaitable and positional arguments
            auto res = co_await client->async_call(
                "pow", std::tuple{2, 8}, packio::net::use_awaitable);
            pow_result.set_value(res.result.get<int>());
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
- nlohmann_json >= 3.9.1
- boost.asio >= 1.70.0 or asio >= 1.13.0

Older versions of `msgpack` and `nlohmann_json` are probably compatible but they are not tested on the CI.

## Configurations

### Standalone or Boost.Asio

By default, `packio` uses `boost::asio`. It is also compatible with standalone `asio`. To use the standalone version, the preprocessor macro `PACKIO_STANDALONE_ASIO=1` must be defined.

If you are using the conan package, you can use the option `standalone_asio=True`.

Depending on your choice, the namespace `packio::net` will be an alias for either `boost::asio` or `asio`.

### RPC components

You can define the following preprocessor macros to either 0 or 1 to force-disable or force-enable components of `packio`:

- `PACKIO_HAS_MSGPACK`
- `PACKIO_HAS_NLOHMANN_JSON`
- `PACKIO_HAS_BOOST_JSON`

If you're using the conan package, use the associated options instead, conan will define these macros accordingly.

If you're not using the conan package, `packio` will try to auto-detect whether these components are available on your system. Define the macros to the appropriate value if you encounter any issue.

### Boost before 1.75

If you're using the conan package with a boost version older than 1.75, you need to manually disable `Boost.Json` with the options `boost_json=False`.
`Boost.Json` version 1.75 contains some bugs when using C-strings as arguments so I'd recommend at using at least version 1.76.

## Tested compilers

- gcc-9
- gcc-10
- gcc-11
- gcc-12
- clang-11
- clang-12
- clang-13
- clang-14
- Apple clang-13
- Visual Studio 2019 Version 16
- Visual Studio 2022 Version 17

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

- gcc-11
- gcc-12
- clang-14
- Apple clang-12

## Samples

You will find some samples in `test_package/samples/` to help you get a hand on `packio`.

## Bonus

Let's compute fibonacci's numbers recursively over websockets with coroutines on a single thread ... in 65 lines of code.

```cpp
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
```
