// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef PACKIO_LOG_H
#define PACKIO_LOG_H

#if defined(PACKIO_LOGGING)
#include <spdlog/spdlog.h>
#define PACKIO_TRACE(...) SPDLOG_TRACE(__VA_ARGS__)
#define PACKIO_DEBUG(...) SPDLOG_DEBUG(__VA_ARGS__)
#define PACKIO_INFO(...) SPDLOG_INFO(__VA_ARGS__)
#define PACKIO_WARN(...) SPDLOG_WARN(__VA_ARGS__)
#define PACKIO_ERROR(...) SPDLOG_ERROR(__VA_ARGS__)
#else
#define PACKIO_TRACE(...) (void)0
#define PACKIO_DEBUG(...) (void)0
#define PACKIO_INFO(...) (void)0
#define PACKIO_WARN(...) (void)0
#define PACKIO_ERROR(...) (void)0
#endif

#endif // PACKIO_LOG_H
