// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef PACKIO_LOG_H
#define PACKIO_LOG_H

#if defined(PACKIO_LOGGING) && PACKIO_LOGGING
#include <spdlog/spdlog.h>
#define TRACE(...) SPDLOG_TRACE(__VA_ARGS__)
#define DEBUG(...) SPDLOG_DEBUG(__VA_ARGS__)
#define INFO(...) SPDLOG_INFO(__VA_ARGS__)
#define WARN(...) SPDLOG_WARN(__VA_ARGS__)
#else
#define TRACE(...) (void)0
#define DEBUG(...) (void)0
#define INFO(...) (void)0
#define WARN(...) (void)0
#endif

#endif // PACKIO_LOG_H
