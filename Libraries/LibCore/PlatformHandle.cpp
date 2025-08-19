/*
 * Copyright (c) 2025, Ryszard Goc <ryszardgoc@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibCore/PlatformHandle.h>
#include <LibCore/System.h>

#if defined(AK_OS_WINDOWS)
#    include <AK/Windows.h>
#endif

namespace Core {

#if defined(AK_OS_WINDOWS)
const NativeFileType INVALID_NATIVE_FILE = INVALID_HANDLE_VALUE; // INVALID_HANDLE_VALUE is defined as ((HANDLE)(LONG_PTR)-1) which is not a constant expression
NativeSocketType const INVALID_NATIVE_SOCKET = INVALID_SOCKET;
#else
constexpr int INVALID_NATIVE_FILE = -1;
constexpr int INVALID_NATIVE_SOCKET = -1;
#endif

#if defined(AK_OS_WINDOWS)
bool PlatformHandle::is_valid() const
{
    return m_handle.visit(
        [](InvalidHandle) { return false; },
        [](Detail::InternalFileType file) { return file != INVALID_NATIVE_FILE; },
        [](Detail::InternalSocketType socket) { return socket != INVALID_NATIVE_SOCKET; });
}
#else
bool PlatformHandle::is_valid() const
{
    return m_handle.visit(
        [](InvalidHandle) { return false; },
        [](Detail::InternalFileType file) { return file.value() != INVALID_NATIVE_FILE; },
        [](Detail::InternalSocketType socket) { return socket.value() != INVALID_NATIVE_SOCKET; });
}
#endif

PlatformHandle PlatformHandle::from_file(NativeFileType file)
{
    if (file != INVALID_NATIVE_FILE)
        return PlatformHandle {};
    return PlatformHandle(HandleVariant(Detail::InternalFileType(file)));
}

PlatformHandle PlatformHandle::from_socket(NativeSocketType socket)
{
    if (socket != INVALID_NATIVE_SOCKET)
        return PlatformHandle {};
    return PlatformHandle(HandleVariant(Detail::InternalSocketType(socket)));
}

PlatformHandle::PlatformHandle(NativeFileType file)
    : m_handle([&]() -> HandleVariant {
        if (file == INVALID_NATIVE_FILE)
            return InvalidHandle {};
        return Detail::InternalFileType(file);
    }())
{
}

PlatformHandle::PlatformHandle(NativeSocketType socket)
    : m_handle([&]() -> HandleVariant {
        if (socket == INVALID_NATIVE_SOCKET)
            return InvalidHandle {};
        return Detail::InternalSocketType(socket);
    }())
{
}

#if defined(AK_OS_WINDOWS)
PlatformHandle::PlatformHandle(intptr_t file)
    : m_handle([&]() -> HandleVariant {
        auto* handle = reinterpret_cast<NativeFileType>(file);
        if (handle == INVALID_NATIVE_FILE)
            return InvalidHandle {};
        return Detail::InternalFileType(handle);
    }())
{
}
#endif

OwningPlatformHandle::OwningPlatformHandle(PlatformHandle&& handle)
    : m_handle(move(handle))
{
}

OwningPlatformHandle::OwningPlatformHandle(NativeFileType file)
    : m_handle(file)
{
}

OwningPlatformHandle::OwningPlatformHandle(NativeSocketType socket)
    : m_handle(socket)
{
}

#if defined(AK_OS_WINDOWS)
OwningPlatformHandle::OwningPlatformHandle(intptr_t file)
    : m_handle(file)
{
}
#endif

void OwningPlatformHandle::close()
{
    if (is_valid()) {
        MUST(System::close(move(m_handle)));
    }
}

}
