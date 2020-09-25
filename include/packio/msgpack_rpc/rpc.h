// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef PACKIO_MSGPACK_RPC_RPC_H
#define PACKIO_MSGPACK_RPC_RPC_H

#include <msgpack.hpp>

#include "../arg.h"
#include "../internal/config.h"
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

    std::optional<request> get_request()
    {
        try_parse_object();
        if (!parsed_) {
            return std::nullopt;
        }
        auto object = std::move(*parsed_);
        parsed_.reset();
        return parse_request(std::move(object));
    }

    std::optional<response> get_response()
    {
        try_parse_object();
        if (!parsed_) {
            return std::nullopt;
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

    static std::optional<response> parse_response(::msgpack::object_handle&& res)
    {
        if (res->type != ::msgpack::type::ARRAY) {
            PACKIO_ERROR("unexpected message type: {}", res->type);
            return std::nullopt;
        }
        if (res->via.array.size != 4) {
            PACKIO_ERROR("unexpected message size: {}", res->via.array.size);
            return std::nullopt;
        }
        int type = res->via.array.ptr[0].as<int>();
        if (type != static_cast<int>(msgpack_rpc_type::response)) {
            PACKIO_ERROR("unexpected type: {}", type);
            return std::nullopt;
        }

        std::optional<response> parsed{std::in_place};
        parsed->zone = std::move(res.zone());
        const auto& array = res->via.array.ptr;

        parsed->id = array[1].as<id_type>();
        if (array[2].type != ::msgpack::type::NIL) {
            parsed->error = array[2];
        }
        else {
            parsed->result = array[3];
        }
        return parsed;
    }

    static std::optional<request> parse_request(::msgpack::object_handle&& req)
    {
        if (req->type != ::msgpack::type::ARRAY || req->via.array.size < 3) {
            PACKIO_ERROR("unexpected message type: {}", req->type);
            return std::nullopt;
        }

        std::optional<request> parsed{std::in_place};
        parsed->zone = std::move(req.zone());
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
                parsed->id = array[idx++].as<id_type>();
                expected_size = 4;
                parsed->type = call_type::request;
                break;
            case msgpack_rpc_type::notification:
                expected_size = 3;
                parsed->type = call_type::notification;
                break;
            default:
                PACKIO_ERROR("unexpected type: {}", type);
                return std::nullopt;
            }

            if (array_size != expected_size) {
                PACKIO_ERROR("unexpected message size: {}", array_size);
                return std::nullopt;
            }

            parsed->method = array[idx++].as<std::string>();
            parsed->args = array[idx++];

            return parsed;
        }
        catch (::msgpack::type_error& exc) {
            PACKIO_ERROR("unexpected message content: {}", exc.what());
            (void)exc;
            return std::nullopt;
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

    template <typename T, typename NamesContainer>
    static std::optional<T> extract_args(
        const ::msgpack::object& args,
        const NamesContainer&)
    {
        if (args.type != ::msgpack::type::ARRAY) {
            PACKIO_ERROR("arguments is not an array");
            return std::nullopt;
        }

        if (args.via.array.size != std::tuple_size_v<T>) {
            // keep this check otherwise msgpack unpacker
            // may silently drop arguments
            return std::nullopt;
        }

        try {
            return args.as<T>();
        }
        catch (::msgpack::type_error&) {
            return std::nullopt;
        }
    }
};

} // msgpack_rpc
} // packio

#endif // PACKIO_MSGPACK_RPC_RPC_H
