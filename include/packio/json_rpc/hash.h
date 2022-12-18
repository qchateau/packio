// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef PACKIO_JSON_RPC_HASH_H
#define PACKIO_JSON_RPC_HASH_H

#include <boost/json.hpp>
#include <boost/version.hpp>

#if BOOST_VERSION < 107700

namespace packio {
namespace json_rpc {
namespace internal {

inline std::size_t combine(std::size_t seed, std::size_t h) noexcept
{
    seed ^= h + 0x9e3779b9 + (seed << 6U) + (seed >> 2U);
    return seed;
}

inline std::size_t hash(const boost::json::value& v)
{
    std::size_t seed = 0;
    switch (v.kind()) {
    case boost::json::kind::null:
        // Don't use std::hash<std::nullptr_t>, old gcc/clang do not support it
        return combine(seed, std::hash<void*>()(nullptr));
    case boost::json::kind::bool_:
        return combine(seed, std::hash<bool>{}(v.get_bool()));
    case boost::json::kind::uint64:
        return combine(seed, std::hash<uint64_t>{}(v.get_uint64()));
    case boost::json::kind::int64:
        return combine(seed, std::hash<int64_t>{}(v.get_int64()));
    case boost::json::kind::double_:
        return combine(seed, std::hash<double>{}(v.get_double()));
    case boost::json::kind::string:
        return combine(
            seed,
            std::hash<std::string_view>{}(std::string_view{
                v.get_string().data(), v.get_string().size()}));
    case boost::json::kind::array: {
        seed = combine(seed, v.get_array().size());
        for (const auto& element : v.get_array()) {
            seed = combine(seed, hash(element));
        }
        return seed;
    }
    case boost::json::kind::object: {
        seed = combine(seed, v.get_object().size());
        for (const auto& element : v.get_object()) {
            std::string_view key{element.key().data(), element.key().size()};
            seed = combine(seed, std::hash<std::string_view>{}(key));
            seed = combine(seed, hash(element.value()));
        }
        return seed;
    }
    }
}

} // internal
} // json_rpc
} // packio

namespace std {

template <>
struct hash<boost::json::value> {
    std::size_t operator()(const boost::json::value& v) const
    {
        return packio::json_rpc::internal::hash(v);
    }
};

} // std

#endif // BOOST_VERSION < 107700

#endif // PACKIO_JSON_RPC_HASH_H
