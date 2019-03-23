#ifndef RPCPACK_SIMPLE_SERVER_H
#define RPCPACK_SIMPLE_SERVER_H

#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "basic_server.h"
#include "internal/utils.h"

namespace rpcpack {

template <typename Protocol, typename Dispatcher>
class simple_server {
public:
    using protocol_type = Protocol;
    using dispatcher_type = Dispatcher;
    using acceptor_type = typename protocol_type::acceptor;
    using endpoint_type = typename protocol_type::endpoint;
    using impl_type = basic_server<protocol_type, dispatcher_type>;

    explicit simple_server(const endpoint_type& endpoint)
        : server_{acceptor_type{io_, endpoint}}
    {
        server_.async_serve_forever();
    }

    template <typename T = protocol_type>
    simple_server(
        std::string_view bind_addr,
        uint16_t port,
        typename internal::enable_if_tcp<T>::type* = 0)
        : simple_server{internal::make_tcp_endpoint(bind_addr, port)}
    {
    }

    template <typename T = protocol_type>
    simple_server(
        std::string_view bind_addr_port,
        typename internal::enable_if_tcp<T>::type* = 0)
        : simple_server{internal::make_tcp_endpoint(bind_addr_port)}
    {
    }

    template <typename T = protocol_type>
    simple_server(
        std::string_view path,
        typename internal::enable_if_local<T>::type* = 0)
        : simple_server{internal::make_local_endpoint(path)}
    {
    }

    ~simple_server() { stop(); }

    acceptor_type& acceptor() { return server_.acceptor(); }
    const acceptor_type& acceptor() const { return server_.acceptor(); }

    template <typename F>
    bool add(std::string_view name, F&& fct)
    {
        DEBUG("add: {}", name);
        return server_.dispatcher()->add(name, std::forward<F>(fct));
    }

    bool remove(const std::string& name)
    {
        DEBUG("remove: {}", name);
        return server_.dispatcher()->remove(name);
    }

    size_t clear() { return server_.dispatcher()->clear(); }

    bool has(const std::string& name) const
    {
        return server_.dispatcher()->has(name);
    }

    std::vector<std::string> known() const
    {
        return server_.dispatcher()->known();
    }

    void run() { io_.run(); }

    void async_run(int nthreads = 1)
    {
        run_threads_.reserve(nthreads);
        for (int i = 0; i < nthreads; ++i) {
            run_threads_.emplace_back([this] { io_.run(); });
        }
    }

    void async_stop() { io_.stop(); }

    void stop()
    {
        async_stop();

        for (auto& thread : run_threads_) {
            thread.join();
        }
        run_threads_.clear();
    }

    void restart() { io_.restart(); }

private:
    boost::asio::io_context io_;
    impl_type server_;
    std::vector<std::thread> run_threads_;
};

using simple_ip_server = simple_server<boost::asio::ip::tcp, default_dispatcher>;

#if defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)
using simple_local_server =
    simple_server<boost::asio::local::stream_protocol, default_dispatcher>;
#endif // defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)

} // rpcpack

#endif // RPCPACK_SIMPLE_SERVER_H