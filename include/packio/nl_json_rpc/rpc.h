// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef PACKIO_NL_JSON_RPC_RPC_H
#define PACKIO_NL_JSON_RPC_RPC_H

#include <deque>

#include <nlohmann/json.hpp>

#include "../arg.h"
#include "../internal/config.h"
#include "../internal/log.h"
#include "../internal/rpc.h"
#include "incremental_buffers.h"

namespace packio {
namespace nl_json_rpc {
namespace internal {

template <typename... Args>
constexpr bool positional_args_v = (!is_arg_v<Args> && ...);

template <typename... Args>
constexpr bool named_args_v = sizeof...(Args) > 0 && (is_arg_v<Args> && ...);

using id_type = nlohmann::json;
using native_type = nlohmann::json;

//! The object representing a client request
struct request {
    call_type type;
    internal::id_type id;
    std::string method;
    native_type args;
};

//! The object representing the response to a call
struct response {
    id_type id;
    native_type result;
    native_type error;
};

//! The incremental parser for JSON-RPC objects
class incremental_parser {
public:
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

    char* buffer()
    { //
        return incremental_buffers_.in_place_buffer();
    }

    std::size_t buffer_capacity() const
    { //
        return incremental_buffers_.in_place_buffer_capacity();
    }

    void buffer_consumed(std::size_t bytes)
    { //
        incremental_buffers_.in_place_buffer_consumed(bytes);
    }

    void reserve_buffer(std::size_t bytes)
    { //
        incremental_buffers_.reserve_in_place_buffer(bytes);
    }

private:
    void try_parse_object()
    {
        if (parsed_) {
            return;
        }
        auto buffer = incremental_buffers_.get_parsed_buffer();
        if (buffer) {
            parsed_ = nlohmann::json::parse(*buffer);
        }
    }

    static std::optional<response> parse_response(nlohmann::json&& res)
    {
        auto id_it = res.find("id");
        auto result_it = res.find("result");
        auto error_it = res.find("error");

        if (id_it == end(res)) {
            PACKIO_ERROR("missing id field");
            return std::nullopt;
        }
        if (result_it == end(res) && error_it == end(res)) {
            PACKIO_ERROR("missing error and result field");
            return std::nullopt;
        }

        std::optional<response> parsed{std::in_place};
        parsed->id = std::move(*id_it);
        if (error_it != end(res)) {
            parsed->error = std::move(*error_it);
        }
        if (result_it != end(res)) {
            parsed->result = std::move(*result_it);
        }
        return parsed;
    }

    static std::optional<request> parse_request(nlohmann::json&& req)
    {
        auto id_it = req.find("id");
        auto method_it = req.find("method");
        auto params_it = req.find("params");

        if (method_it == end(req)) {
            PACKIO_ERROR("missing method field");
            return std::nullopt;
        }
        if (!method_it->is_string()) {
            PACKIO_ERROR("method field is not a string");
            return std::nullopt;
        }

        std::optional<request> parsed{std::in_place};
        parsed->method = method_it->get<std::string>();
        if (params_it == end(req) || params_it->is_null()) {
            parsed->args = nlohmann::json::array();
        }
        else if (!params_it->is_array() && !params_it->is_object()) {
            PACKIO_ERROR("non-structured arguments are not supported");
            return std::nullopt;
        }
        else {
            parsed->args = std::move(*params_it);
        }

        if (id_it == end(req) || id_it->is_null()) {
            parsed->type = call_type::notification;
        }
        else {
            parsed->type = call_type::request;
            parsed->id = std::move(*id_it);
        }
        return parsed;
    }

    std::optional<nlohmann::json> parsed_;
    incremental_buffers incremental_buffers_;
};

} // internal

//! The JSON-RPC protocol implementation
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
    { //
        return id.dump();
    }

    template <typename... Args>
    static auto serialize_notification(std::string_view method, Args&&... args)
        -> std::enable_if_t<internal::positional_args_v<Args...>, std::string>
    {
        return nlohmann::json({
                                  {"jsonrpc", "2.0"},
                                  {"method", method},
                                  {"params",
                                   nlohmann::json::array({nlohmann::json(
                                       std::forward<Args>(args))...})},
                              })
            .dump();
    }

    template <typename... Args>
    static auto serialize_notification(std::string_view method, Args&&... args)
        -> std::enable_if_t<internal::named_args_v<Args...>, std::string>
    {
        return nlohmann::json({
                                  {"jsonrpc", "2.0"},
                                  {"method", method},
                                  {"params", {{args.name, args.value}...}},
                              })
            .dump();
    }

    template <typename... Args>
    static auto serialize_notification(std::string_view, Args&&...) -> std::enable_if_t<
        !internal::positional_args_v<Args...> && !internal::named_args_v<Args...>,
        std::string>
    {
        static_assert(
            internal::positional_args_v<Args...> || internal::named_args_v<Args...>,
            "JSON-RPC does not support mixed named and unnamed arguments");
    }

    template <typename... Args>
    static auto serialize_request(
        const id_type& id,
        std::string_view method,
        Args&&... args)
        -> std::enable_if_t<internal::positional_args_v<Args...>, std::string>
    {
        return nlohmann::json({
                                  {"jsonrpc", "2.0"},
                                  {"method", method},
                                  {"params",
                                   nlohmann::json::array({nlohmann::json(
                                       std::forward<Args>(args))...})},
                                  {"id", id},
                              })
            .dump();
    }

    template <typename... Args>
    static auto serialize_request(
        const id_type& id,
        std::string_view method,
        Args&&... args)
        -> std::enable_if_t<internal::named_args_v<Args...>, std::string>
    {
        return nlohmann::json({
                                  {"jsonrpc", "2.0"},
                                  {"method", method},
                                  {"params", {{args.name, args.value}...}},
                                  {"id", id},
                              })
            .dump();
    }

    template <typename... Args>
    static auto serialize_request(const id_type&, std::string_view, Args&&...)
        -> std::enable_if_t<
            !internal::positional_args_v<Args...> && !internal::named_args_v<Args...>,
            std::string>
    {
        static_assert(
            internal::positional_args_v<Args...> || internal::named_args_v<Args...>,
            "JSON-RPC does not support mixed named and unnamed arguments");
    }

    static std::string serialize_response(const id_type& id)
    {
        return serialize_response(id, nlohmann::json{});
    }

    template <typename T>
    static std::string serialize_response(const id_type& id, T&& value)
    {
        return nlohmann::json{
            {"jsonrpc", "2.0"},
            {"id", id},
            {"result", std::forward<T>(value)},
        }
            .dump();
    }

    template <typename T>
    static std::string serialize_error_response(const id_type& id, T&& value)
    {
        return nlohmann::json{
            {"jsonrpc", "2.0"},
            {"id", id},
            {"error",
             [&]() {
                 nlohmann::json error = {
                     {"code", -32000}, // -32000 is an implementation-defined error
                     {"data", std::forward<T>(value)},
                 };
                 if (error["data"].is_string()) {
                     error["message"] = error["data"];
                 }
                 else {
                     error["message"] = "Unknown error";
                 }
                 return error;
             }()},
        }
            .dump();
    }

    static net::const_buffer buffer(const std::string& buf)
    {
        return net::const_buffer(buf.data(), buf.size());
    }

    template <typename T, typename NamesContainer>
    static std::optional<T> extract_args(
        const nlohmann::json& args,
        const NamesContainer& names)
    {
        try {
            if (args.is_array()) {
                if (args.size() != std::tuple_size_v<T>) {
                    // keep this check otherwise the converter
                    // may silently drop arguments
                    PACKIO_WARN(
                        "cannot convert args: wrong number of arguments");
                    return std::nullopt;
                }

                return args.get<T>();
            }
            else if (args.is_object()) {
                return convert_named_args<T>(args, names);
            }
            else {
                PACKIO_ERROR("arguments are not a structured type");
                return std::nullopt;
            }
        }
        catch (const std::exception& exc) {
            PACKIO_WARN("cannot convert args: {}", exc.what());
            (void)exc;
            return std::nullopt;
        }
    }

private:
    template <typename T, typename NamesContainer>
    static T convert_named_args(const nlohmann::json& args, const NamesContainer& names)
    {
        return convert_named_args<T>(
            args, names, std::make_index_sequence<std::tuple_size_v<T>>{});
    }

    template <typename T, typename NamesContainer, std::size_t... Idxs>
    static T convert_named_args(
        const nlohmann::json& args,
        const NamesContainer& names,
        std::index_sequence<Idxs...>)
    {
        return T{(args.at(names.at(Idxs))
                      .template get<std::tuple_element_t<Idxs, T>>())...};
    }
};

} // nl_json_rpc
} // packio

#endif // PACKIO_NL_JSON_RPC_RPC_H
