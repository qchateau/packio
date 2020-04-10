// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef PACKIO_ERROR_CODE_H
#define PACKIO_ERROR_CODE_H

//! @file
//! Enum @ref packio::error "error"

#include <type_traits>
#include <boost/system/error_code.hpp>

#include "internal/config.h"

namespace packio {

//! The error codes enumeration
enum class error {
    success = 0, //!< Success
    error_during_call, //!< An error happened during the call, server-side error
    unknown_procedure, //!< The procedure name is unknown, server-side error
    cancelled, //!< The operation has been cancelled
    call_error, //!< An error happened during the call
    bad_result_type //!< The result type is not as expected
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
        case error::unknown_procedure:
            return "Unknown function";
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