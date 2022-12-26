// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef PACKIO_MSGPACK_RPC_RPC_H
#define PACKIO_MSGPACK_RPC_RPC_H

#include <msgpack.hpp>

#include "../arg.h"
#include "../args_specs.h"
#include "../internal/config.h"
#include "../internal/expected.h"
#include "../internal/log.h"
#include "../internal/rpc.h"

namespace packio {
namespace msgpack_rpc {
namespace internal {

template <typename... Args>
constexpr bool positional_args_v = (!is_arg_v<Args> && ...);

enum class msgpack_rpc_type { request = 0, response = 1, notification = 2 };

using id_type = uint32_t;
using native_type = ::msgpack::object;
using packio::internal::expected;
using packio::internal::unexpected;

//! The object representing a client request
struct request {
    call_type type;
    id_type id;
    std::string method;
    native_type args;

    std::unique_ptr<::msgpack::zone> zone; //!< Msgpack zone storing the args
};

//! The object representing the response to a call
struct response {
    id_type id;
    native_type result;
    native_type error;

    std::unique_ptr<::msgpack::zone> zone; //!< Msgpack zone storing error and result
};

//! The incremental parser for msgpack-RPC objects
class incremental_parser {
public:
    incremental_parser() : unpacker_{std::make_unique<::msgpack::unpacker>()} {}

    expected<request, std::string> get_request()
    {
        try_parse_object();
        if (!parsed_) {
            return unexpected{"no request parsed"};
        }
        auto object = std::move(*parsed_);
        parsed_.reset();
        return parse_request(std::move(object));
    }

    expected<response, std::string> get_response()
    {
        try_parse_object();
        if (!parsed_) {
            return unexpected{"no response parsed"};
        }
        auto object = std::move(*parsed_);
        parsed_.reset();
        return parse_response(std::move(object));
    }

    char* buffer() const
    { //
        return unpacker_->buffer();
    }

    std::size_t buffer_capacity() const { return unpacker_->buffer_capacity(); }

    void buffer_consumed(std::size_t bytes)
    {
        unpacker_->buffer_consumed(bytes);
    }

    void reserve_buffer(std::size_t bytes) { unpacker_->reserve_buffer(bytes); }

private:
    void try_parse_object()
    {
        if (parsed_) {
            return;
        }
        ::msgpack::object_handle object;
        if (unpacker_->next(object)) {
            parsed_ = std::move(object);
        }
    }

    static expected<response, std::string> parse_response(
        ::msgpack::object_handle&& res)
    {
        if (res->type != ::msgpack::type::ARRAY) {
            return unexpected{
                "unexpected message type: " + std::to_string(res->type)};
        }
        if (res->via.array.size != 4) {
            return unexpected{
                "unexpected message size: " + std::to_string(res->via.array.size)};
        }
        int type = res->via.array.ptr[0].as<int>();
        if (type != static_cast<int>(msgpack_rpc_type::response)) {
            return unexpected{"unexpected type: " + std::to_string(type)};
        }

        response parsed;
        parsed.zone = std::move(res.zone());
        const auto& array = res->via.array.ptr;

        parsed.id = array[1].as<id_type>();
        if (array[2].type != ::msgpack::type::NIL) {
            parsed.error = array[2];
        }
        else {
            parsed.result = array[3];
        }
        return {std::move(parsed)};
    }

    static expected<request, std::string> parse_request(::msgpack::object_handle&& req)
    {
        if (req->type != ::msgpack::type::ARRAY || req->via.array.size < 3) {
            return unexpected{
                "unexpected message type: " + std::to_string(req->type)};
        }

        request parsed;
        parsed.zone = std::move(req.zone());
        const auto& array = req->via.array.ptr;
        auto array_size = req->via.array.size;
        ;

        try {
            int idx = 0;
            msgpack_rpc_type type = static_cast<msgpack_rpc_type>(
                array[idx++].as<int>());

            std::size_t expected_size;
            switch (type) {
            case msgpack_rpc_type::request:
                parsed.id = array[idx++].as<id_type>();
                expected_size = 4;
                parsed.type = call_type::request;
                break;
            case msgpack_rpc_type::notification:
                expected_size = 3;
                parsed.type = call_type::notification;
                break;
            default:
                return unexpected{
                    "unexpected type: " + std::to_string(static_cast<int>(type))};
            }

            if (array_size != expected_size) {
                return unexpected{
                    "unexpected message size: " + std::to_string(array_size)};
            }

            parsed.method = array[idx++].as<std::string>();
            parsed.args = array[idx++];

            return {std::move(parsed)};
        }
        catch (::msgpack::type_error& exc) {
            return unexpected{
                std::string{"unexpected message content: "} + exc.what()};
        }
    }

    std::optional<::msgpack::object_handle> parsed_;
    std::unique_ptr<::msgpack::unpacker> unpacker_;
};

} // internal

//! The msgpack RPC protocol implementation
class rpc {
public:
    //! Type of the call ID
    using id_type = internal::id_type;

    //! The native type of the serialization library
    using native_type = internal::native_type;

    //! The type of the parsed request object
    using request_type = internal::request;

    //! The type of the parsed response object
    using response_type = internal::response;

    //! The incremental parser type
    using incremental_parser_type = internal::incremental_parser;

    static std::string format_id(const id_type& id)
    {
        return std::to_string(id);
    }

    template <typename... Args>
    static auto serialize_notification(std::string_view method, Args&&... args)
        -> std::enable_if_t<internal::positional_args_v<Args...>, ::msgpack::sbuffer>
    {
        ::msgpack::sbuffer buffer;
        ::msgpack::pack(
            buffer,
            std::forward_as_tuple(
                static_cast<int>(internal::msgpack_rpc_type::notification),
                method,
                std::forward_as_tuple(std::forward<Args>(args)...)));
        return buffer;
    }

    template <typename... Args>
    static auto serialize_notification(std::string_view, Args&&...)
        -> std::enable_if_t<!internal::positional_args_v<Args...>, ::msgpack::sbuffer>
    {
        static_assert(
            internal::positional_args_v<Args...>,
            "msgpack-RPC does not support named arguments");
    }

    template <typename... Args>
    static auto serialize_request(id_type id, std::string_view method, Args&&... args)
        -> std::enable_if_t<internal::positional_args_v<Args...>, ::msgpack::sbuffer>
    {
        ::msgpack::sbuffer buffer;
        ::msgpack::pack(
            buffer,
            std::forward_as_tuple(
                static_cast<int>(internal::msgpack_rpc_type::request),
                id,
                method,
                std::forward_as_tuple(std::forward<Args>(args)...)));
        return buffer;
    }

    template <typename... Args>
    static auto serialize_request(id_type, std::string_view, Args&&...)
        -> std::enable_if_t<!internal::positional_args_v<Args...>, ::msgpack::sbuffer>
    {
        static_assert(
            internal::positional_args_v<Args...>,
            "msgpack-RPC does not support named arguments");
    }

    static ::msgpack::sbuffer serialize_response(id_type id)
    {
        return serialize_response(id, ::msgpack::object{});
    }

    template <typename T>
    static ::msgpack::sbuffer serialize_response(id_type id, T&& value)
    {
        ::msgpack::sbuffer buffer;
        ::msgpack::pack(
            buffer,
            std::forward_as_tuple(
                static_cast<int>(internal::msgpack_rpc_type::response),
                id,
                ::msgpack::object{},
                std::forward<T>(value)));
        return buffer;
    }

    template <typename T>
    static ::msgpack::sbuffer serialize_error_response(id_type id, T&& value)
    {
        ::msgpack::sbuffer buffer;
        ::msgpack::pack(
            buffer,
            std::forward_as_tuple(
                static_cast<int>(internal::msgpack_rpc_type::response),
                id,
                std::forward<T>(value),
                ::msgpack::object{}));
        return buffer;
    }

    static net::const_buffer buffer(const ::msgpack::sbuffer& buf)
    {
        return net::const_buffer(buf.data(), buf.size());
    }

    template <typename T, typename F>
    static internal::expected<T, std::string> extract_args(
        const ::msgpack::object& args,
        const args_specs<F>& specs)
    {
        try {
            if (args.type != ::msgpack::type::ARRAY) {
                throw std::runtime_error{"arguments is not an array"};
            }
            return convert_positional_args<T>(args.via.array, specs);
        }
        catch (const std::exception& exc) {
            return internal::unexpected{
                std::string{"cannot convert arguments: "} + exc.what()};
        }
    }

private:
    template <typename T, typename F>
    static constexpr T convert_positional_args(
        const ::msgpack::object_array& array,
        const args_specs<F>& specs)
    {
        return convert_positional_args<T>(
            array, specs, std::make_index_sequence<args_specs<F>::size()>());
    }

    template <typename T, typename F, std::size_t... Idxs>
    static constexpr T convert_positional_args(
        const ::msgpack::object_array& array,
        const args_specs<F>& specs,
        std::index_sequence<Idxs...>)
    {
        if (!specs.options().allow_extra_arguments
            && array.size > std::tuple_size_v<T>) {
            throw std::runtime_error{"too many arguments"};
        }
        return {[&]() {
            if (Idxs < array.size) {
                try {
                    return array.ptr[Idxs].as<std::tuple_element_t<Idxs, T>>();
                }
                catch (const ::msgpack::type_error&) {
                    throw std::runtime_error{
                        "invalid type for argument "
                        + specs.template get<Idxs>().name()};
                }
            }
            if (const auto& value = specs.template get<Idxs>().default_value()) {
                return *value;
            }
            throw std::runtime_error{
                "no value for argument " + specs.template get<Idxs>().name()};
        }()...};
    }
};

} // msgpack_rpc
} // packio

#endif // PACKIO_MSGPACK_RPC_RPC_H
