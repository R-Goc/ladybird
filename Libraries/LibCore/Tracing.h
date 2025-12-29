#pragma once

#define TRACING_PROVIDER_HANDLE g_tracing_provider_core

#include <AK/TracingBase.h>

#define CORE_IO_TRACE_START(name) \
    _TRACING_START(name, WINEVENT_LEVEL_INFO, Keyword::IO);

#define CORE_IO_TRACE_END(name, result) \
    _TRACING_END(name, WINEVENT_LEVEL_INFO, Keyword::IO, result);

#define CORE_IO_EVENT_INFO(name, keyword) \
    _TRACING_EVENT(name, WINEVENT_LEVEL_INFO, keyword)
