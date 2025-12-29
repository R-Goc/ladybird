/*
 * Copyright (c) 2025, Ryszard Goc <ryszardgoc@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Platform.h>
#include <AK/Types.h>

#if defined(AK_OS_WINDOWS)
#    include <AK/Windows.h>
#    include <TraceLoggingProvider.h>
#    include <winmeta.h>
#elif defined(AK_OS_MACOS)
#    include <os/log.h>
#    include <os/signpost.h>
#elif !defined(AK_OS_ANDROID)
#    include <sys/sdt.h>
#    include <unistd.h>
#endif

// WARN: This header may only be included in implemntation files due to the inclusion of platform specific headers
// This design was chosen as it prevents call overhead and issues around how tracing libraries handle shared libraries
// TODO: Implement tracing using lttng on linux for lower overhead
//  Implement tracing for android using ATrace.

#if defined(AK_OS_WINDOWS)
TRACELOGGING_DECLARE_PROVIDER(TRACING_PROVIDER_HANDLE);
struct TraceProviderHelper {
    TraceProviderHelper()
    {
        TraceLoggingRegister(TRACING_PROVIDER_HANDLE);
    }
    ~TraceProviderHelper()
    {
        TraceLoggingUnregister(TRACING_PROVIDER_HANDLE);
    }
};

enum class Keyword : u64 {
    Error = 1,
    IO = 2,
    Network = 4,
};

// NOTE: How to obtain the uuid is described at https://learn.microsoft.com/en-us/windows/win32/api/traceloggingprovider/nf-traceloggingprovider-tracelogging_define_provider
// The uuid used is based on the hash of the name which allows just giving the name of the provider to trace instead of the uuid
// The provider group is just Ladybird: 883800af-db41-5049-1169-b493d66633e4
#    define TRACING_DEFINE_PROVIDER(library_name, uuid)                                                                                                                                 \
        TRACELOGGING_DEFINE_PROVIDER(TRACING_PROVIDER_HANDLE, library_name, uuid, TraceLoggingOptionGroup(0x883800af, 0xdb41, 0x5049, 0x11, 0x69, 0xb4, 0x93, 0xd6, 0x66, 0x33, 0xe4)); \
        static TraceProviderHelper s_helper;

#    define _TRACING_EVENT(name, level, keyword) \
        TraceLoggingWrite(TRACING_PROVIDER_HANDLE, name, TraceLoggingLevel(level), TraceLoggingKeyword(to_underlying(keyword)));

#    define _TRACING_START(name, level, keyword) \
        TraceLoggingWrite(TRACING_PROVIDER_HANDLE, name, TraceLoggingLevel(level), TraceLoggingKeyword(to_underlying(keyword)));

#    define _TRACING_END(name, level, keyword, result) \
        TraceLoggingWrite(TRACING_PROVIDER_HANDLE, name, TraceLoggingLevel(level), TraceLoggingKeyword(to_underlying(keyword)), TraceLoggingValue(result, "result"));
#elif defined(AK_OS_MACOS)
#elif !defined(AK_OS_ANDROID)
#endif
