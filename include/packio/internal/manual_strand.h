// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef PACKIO_MANUAL_STRAND_H
#define PACKIO_MANUAL_STRAND_H

#include <queue>

#include "config.h"
#include "movable_function.h"

namespace packio {
namespace internal {

template <typename Executor>
class manual_strand {
public:
    using function_type = movable_function<void()>;

    explicit manual_strand(net::strand<Executor>& strand) : strand_{strand} {}

    void push(function_type function)
    {
        net::dispatch(strand_, [this, function = std::move(function)]() mutable {
            queue_.push(std::move(function));

            if (!executing_) {
                executing_ = true;
                execute();
            }
        });
    }

    void next()
    {
        net::dispatch(strand_, [this] { execute(); });
    }

private:
    void execute()
    {
        if (queue_.empty()) {
            executing_ = false;
            return;
        }

        auto function = std::move(queue_.front());
        queue_.pop();
        function();
    }

    net::strand<Executor>& strand_;
    std::queue<function_type> queue_;
    bool executing_{false};
};

} // internal
} // packio

#endif // PACKIO_MANUAL_STRAND_H
