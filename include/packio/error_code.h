#ifndef PACKIO_ERROR_CODE_H
#define PACKIO_ERROR_CODE_H

#include <boost/system/error_code.hpp>

namespace packio {

enum class error {
    success = 0,
    error_during_call,
    unknown_function,
    communication_failure,
    timeout,
    call_error
};

struct packio_error_category : boost::system::error_category {
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
        case error::timeout:
            return "Timeout";
        case error::call_error:
            return "Call error";
        default:
            return "Unrecognized error";
        }
    }
};

inline const boost::system::error_category& packio_category()
{
    static packio_error_category cat;
    return cat;
}

inline boost::system::error_code make_error_code(error e)
{
    return {static_cast<int>(e), packio_category()};
}

inline bool operator==(boost::system::error_code lhs, error rhs)
{
    return lhs.value() == static_cast<int>(rhs)
           && dynamic_cast<const packio_error_category*>(&lhs.category());
}

inline bool operator==(error lhs, boost::system::error_code rhs)
{
    return rhs.value() == static_cast<int>(lhs)
           && dynamic_cast<const packio_error_category*>(&rhs.category());
}

inline bool operator!=(error lhs, boost::system::error_code rhs)
{
    return !(lhs == rhs);
}

inline bool operator!=(boost::system::error_code lhs, error rhs)
{
    return !(lhs == rhs);
}

} // packio

#endif // PACKIO_ERROR_CODE_H