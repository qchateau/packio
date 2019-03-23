#ifndef RPCPACK_LOG_H
#define RPCPACK_LOG_H

#if defined(RPCPACK_LOGGING) && RPCPACK_LOGGING
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

#endif // RPCPACK_LOG_H
