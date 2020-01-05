// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef PACKIO_ERROR_CODE_H
#define PACKIO_ERROR_CODE_H

#include <type_traits>
#include <boost/system/error_code.hpp>

namespace packio {

enum class error {
    success = 0,
    error_during_call,
    unknown_function,
    communication_failure,
    cancelled,
    call_error,
    bad_result_type
};

struct error_category : boost::system::error_category {
    const char* name() const noexcept override { return "packio"; }
    std::string message(int ev) const override
    {
        switch (static_cast<error>(ev)) {
        case error::success:
            return "Success";
        case error::error_during_call:
            return "Error during call";
        case error::unknown_function:
            return "Unknown function";
        case error::communication_failure:
            return "Communication failure";
        case error::cancelled:
            return "Cancelled";
        case error::call_error:
            return "Call error";
        case error::bad_result_type:
            return "Bad result type";
        default:
            return "Unrecognized error";
        }
    }
};

inline boost::system::error_code make_error_code(error e)
{
    static constexpr error_category category;
    return {static_cast<int>(e), category};
}

} // packio

namespace boost::system {

template <>
struct is_error_code_enum<packio::error> : std::true_type {
};

} // boost::system

#endif // PACKIO_ERROR_CODE_H