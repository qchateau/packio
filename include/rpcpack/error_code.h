#ifndef RPCPACK_ERROR_CODE_H
#define RPCPACK_ERROR_CODE_H

#include <boost/system/error_code.hpp>

namespace rpcpack {

enum class error {
    success = 0,
    exception_during_call,
    unknown_function,
    incompatible_arguments,
    communication_failure,
    timeout,
    call_error
};

struct rpcpack_error_category : boost::system::error_category {
    const char* name() const noexcept override;
    std::string message(int ev) const override;
};

const char* rpcpack_error_category::name() const noexcept
{
    return "rpcpack";
}

std::string rpcpack_error_category::message(int e) const
{
    switch (static_cast<error>(e)) {
    case error::success:
        return "Success";
    case error::exception_during_call:
        return "Exception during call";
    case error::unknown_function:
        return "Unknown function";
    case error::incompatible_arguments:
        return "Incompatible arguments";
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

inline const boost::system::error_category& rpcpack_category()
{
    static rpcpack_error_category cat;
    return cat;
};

inline boost::system::error_code make_error_code(error e)
{
    return {static_cast<int>(e), rpcpack_category()};
}

inline bool operator==(boost::system::error_code lhs, error rhs)
{
    return lhs.value() == static_cast<int>(rhs)
           && dynamic_cast<const rpcpack_error_category*>(&lhs.category());
}

inline bool operator==(error lhs, boost::system::error_code rhs)
{
    return rhs.value() == static_cast<int>(lhs)
           && dynamic_cast<const rpcpack_error_category*>(&rhs.category());
}

inline bool operator!=(error lhs, boost::system::error_code rhs)
{
    return !(lhs == rhs);
}

inline bool operator!=(boost::system::error_code lhs, error rhs)
{
    return !(lhs == rhs);
}

} // rpcpack

#endif // RPCPACK_ERROR_CODE_H