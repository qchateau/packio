// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef PACKIO_JSON_RPC_CONVERTERS_H
#define PACKIO_JSON_RPC_CONVERTERS_H

#include <boost/json.hpp>

namespace packio {
namespace json_rpc {
namespace internal {

template <typename... Args, std::size_t... Idxs>
std::tuple<Args...> json_to_tuple(
    const boost::json::array& jv,
    std::index_sequence<Idxs...>)
{
    return {boost::json::value_to<Args>(jv.at(Idxs))...};
}

} // internal
} // json_rpc
} // packio

BOOST_JSON_NS_BEGIN

template <typename... Args>
std::tuple<Args...> tag_invoke(value_to_tag<std::tuple<Args...>>, const value& jv)
{
    return ::packio::json_rpc::internal::json_to_tuple<Args...>(
        jv.get_array(), std::make_index_sequence<sizeof...(Args)>());
}

template <
    typename CStr,
    typename = std::enable_if_t<std::is_same_v<std::decay_t<CStr>, const char*>>>
void tag_invoke(value_from_tag, value& jv, CStr&& from)
{
    jv.emplace_string().assign(std::forward<CStr>(from));
}

BOOST_JSON_NS_END

#endif // PACKIO_JSON_RPC_CONVERTERS_H