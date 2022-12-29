// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef PACKIO_EXPECTED_H
#define PACKIO_EXPECTED_H

#include <variant>

namespace packio {
namespace internal {

template <typename Error>
struct unexpected {
public:
    explicit unexpected(Error error) : error_(std::move(error)) {}
    Error& error() { return error_; }

private:
    Error error_;
};

template <typename T, typename Error>
class expected {
public:
    template <typename G>
    expected(unexpected<G> unexpected)
        : data_(Error(std::move(unexpected.error())))
    {
    }
    expected(T value) : data_(std::move(value)) {}

    Error& error() { return std::get<Error>(data_); }
    T& value() { return std::get<T>(data_); }

    explicit operator bool() const { return std::holds_alternative<T>(data_); }
    T& operator*() { return value(); }
    T* operator->() { return &this->operator*(); }

private:
    std::variant<T, Error> data_;
};

} // internal
} // packio

#endif // PACKIO_EXPECTED_H
