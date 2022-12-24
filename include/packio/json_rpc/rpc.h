// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef PACKIO_JSON_RPC_RPC_H
#define PACKIO_JSON_RPC_RPC_H

#include <queue>

#include <boost/json.hpp>

#include "../arg.h"
#include "../internal/config.h"
#include "../internal/expected.h"
#include "../internal/log.h"
#include "../internal/rpc.h"
#include "converters.h"
#include "hash.h"

namespace packio {
namespace json_rpc {
namespace internal {

template <typename... Args>
constexpr bool positional_args_v = (!is_arg_v<Args> && ...);

template <typename... Args>
constexpr bool named_args_v = sizeof...(Args) > 0 && (is_arg_v<Args> && ...);

using id_type = boost::json::value;
using native_type = boost::json::value;
using string_type = boost::json::string;
using packio::internal::expected;
using packio::internal::unexpected;

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
    incremental_parser()
        : parser_{std::make_unique<boost::json::stream_parser>()}
    { //
        parser_->reset();
    }

    expected<request, std::string> get_request()
    {
        if (parsed_.empty()) {
            return unexpected{"no request parsed"};
        }
        auto object = std::move(parsed_.front());
        parsed_.pop();
        return parse_request(std::move(object.as_object()));
    }

    expected<response, std::string> get_response()
    {
        if (parsed_.empty()) {
            return unexpected{"no response parsed"};
        }
        auto object = std::move(parsed_.front());
        parsed_.pop();
        return parse_response(std::move(object.as_object()));
    }

    char* buffer()
    { //
        return buffer_.data();
    }

    std::size_t buffer_capacity() const
    { //
        return buffer_.size();
    }

    void buffer_consumed(std::size_t bytes)
    { //
        std::size_t parsed = 0;
        while (parsed < bytes) {
            parsed += parser_->write_some(
                buffer_.data() + parsed, bytes - parsed);
            if (parser_->done()) {
                parsed_.push(parser_->release());
                parser_->reset();
            }
        }
    }

    void reserve_buffer(std::size_t bytes)
    { //
        buffer_.resize(bytes);
    }

private:
    static expected<response, std::string> parse_response(boost::json::object&& res)
    {
        auto id_it = res.find("id");
        auto result_it = res.find("result");
        auto error_it = res.find("error");

        if (id_it == res.end()) {
            return unexpected{"missing id field"};
        }
        if (result_it == res.end() && error_it == res.end()) {
            return unexpected{"missing error and result field"};
        }

        response parsed;
        parsed.id = std::move(id_it->value());
        if (error_it != res.end()) {
            parsed.error = std::move(error_it->value());
        }
        if (result_it != res.end()) {
            parsed.result = std::move(result_it->value());
        }
        return parsed;
    }

    static expected<request, std::string> parse_request(boost::json::object&& req)
    {
        auto id_it = req.find("id");
        auto method_it = req.find("method");
        auto params_it = req.find("params");

        if (method_it == req.end()) {
            return unexpected{"missing method field"};
        }
        if (!method_it->value().is_string()) {
            return unexpected{"method field is not a string"};
        }

        request parsed;
        parsed.method = std::string{
            method_it->value().get_string().data(),
            method_it->value().get_string().size(),
        };
        if (params_it == req.end() || params_it->value().is_null()) {
            parsed.args = boost::json::array{};
        }
        else if (!params_it->value().is_array() && !params_it->value().is_object()) {
            return unexpected{"non-structured arguments are not supported"};
        }
        else {
            parsed.args = std::move(params_it->value());
        }

        if (id_it == req.end() || id_it->value().is_null()) {
            parsed.type = call_type::notification;
        }
        else {
            parsed.type = call_type::request;
            parsed.id = std::move(id_it->value());
        }
        return parsed;
    }

    std::vector<char> buffer_;
    std::queue<boost::json::value> parsed_;
    std::unique_ptr<boost::json::stream_parser> parser_;
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
        return boost::json::serialize(id);
    }

    template <typename... Args>
    static auto serialize_notification(std::string_view method, Args&&... args)
        -> std::enable_if_t<internal::positional_args_v<Args...>, std::string>
    {
        auto res = boost::json::serialize(boost::json::object({
            {"jsonrpc", "2.0"},
            {"method", method},
            {"params", {boost::json::value_from(std::forward<Args>(args))...}},
        }));
        PACKIO_TRACE("notification: " + res);
        return res;
    }

    template <typename... Args>
    static auto serialize_notification(std::string_view method, Args&&... args)
        -> std::enable_if_t<internal::named_args_v<Args...>, std::string>
    {
        auto res = boost::json::serialize(boost::json::object({
            {"jsonrpc", "2.0"},
            {"method", method},
            {"params", {{args.name, boost::json::value_from(args.value)}...}},
        }));
        PACKIO_TRACE("notification: " + res);
        return res;
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
        auto res = boost::json::serialize(boost::json::object({
            {"jsonrpc", "2.0"},
            {"method", method},
            {"params", {boost::json::value_from(std::forward<Args>(args))...}},
            {"id", id},
        }));
        PACKIO_TRACE("request: " + res);
        return res;
    }

    template <typename... Args>
    static auto serialize_request(
        const id_type& id,
        std::string_view method,
        Args&&... args)
        -> std::enable_if_t<internal::named_args_v<Args...>, std::string>
    {
        auto res = boost::json::serialize(boost::json::object({
            {"jsonrpc", "2.0"},
            {"method", method},
            {"params", {{args.name, boost::json::value_from(args.value)}...}},
            {"id", id},
        }));
        PACKIO_TRACE("request: " + res);
        return res;
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
        return serialize_response(id, boost::json::value{});
    }

    template <typename T>
    static std::string serialize_response(const id_type& id, T&& value)
    {
        auto res = boost::json::serialize(boost::json::object({
            {"jsonrpc", "2.0"},
            {"id", id},
            {"result", std::forward<T>(value)},
        }));
        PACKIO_TRACE("response: " + res);
        return res;
    }

    template <typename T>
    static std::string serialize_error_response(const id_type& id, T&& value)
    {
        auto res = boost::json::serialize(boost::json::object({
            {"jsonrpc", "2.0"},
            {"id", id},
            {"error",
             [&]() {
                 boost::json::object error = {
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
        }));
        PACKIO_TRACE("response: " + res);
        return res;
    }

    static net::const_buffer buffer(const std::string& buf)
    {
        return net::const_buffer(buf.data(), buf.size());
    }

    template <typename T, typename... ArgSpecs>
    static internal::expected<T, std::string> extract_args(
        boost::json::value&& args,
        const std::tuple<ArgSpecs...>& args_specs)
    {
        try {
            if (args.is_array()) {
                if (args.get_array().size() > std::tuple_size_v<T>) {
                    // keep this check otherwise the converter
                    // may silently drop arguments
                    return internal::unexpected{
                        "cannot convert args: too many arguments"};
                }

                return convert_positional_args<T>(args.get_array(), args_specs);
            }
            else if (args.is_object()) {
                return convert_named_args<T>(args.get_object(), args_specs);
            }
            else {
                return internal::unexpected{
                    "arguments are not a structured type"};
            }
        }
        catch (const std::exception& exc) {
            return internal::unexpected{
                std::string{"cannot convert args: "} + exc.what()};
        }
    }

private:
    template <typename T, typename... ArgSpecs>
    static constexpr T convert_positional_args(
        const boost::json::array& array,
        const std::tuple<ArgSpecs...>& args_specs)
    {
        return convert_positional_args<T>(
            array, args_specs, std::make_index_sequence<std::tuple_size_v<T>>());
    }

    template <typename T, typename... ArgSpecs, std::size_t... Idxs>
    static constexpr T convert_positional_args(
        const boost::json::array& array,
        const std::tuple<ArgSpecs...>& args_specs,
        std::index_sequence<Idxs...>)
    {
        return {[&]() {
            if (Idxs < array.size()) {
                return boost::json::value_to<std::tuple_element_t<Idxs, T>>(
                    array.at(Idxs));
            }
            return std::get<Idxs>(args_specs).default_value();
        }()...};
    }

    template <typename T, typename... ArgSpecs>
    static constexpr T convert_named_args(
        const boost::json::object& args,
        const std::tuple<ArgSpecs...>& args_specs)
    {
        return convert_named_args<T>(
            args, args_specs, std::make_index_sequence<std::tuple_size_v<T>>());
    }

    template <typename T, typename... ArgSpecs, std::size_t... Idxs>
    static constexpr T convert_named_args(
        const boost::json::object& args,
        const std::tuple<ArgSpecs...>& args_specs,
        std::index_sequence<Idxs...>)
    {
        return T{[&]() {
            auto it = args.find(std::get<Idxs>(args_specs).name());
            if (it != args.end()) {
                return boost::json::value_to<std::tuple_element_t<Idxs, T>>(
                    it->value());
            }
            return std::get<Idxs>(args_specs).default_value();
        }()...};
    }
};

} // json_rpc
} // packio

#endif // PACKIO_JSON_RPC_RPC_H
