/*
 * Copyright (c) 2025, Ryszard Goc <ryszardgoc@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Format.h>
#include <AK/Forward.h>
#include <AK/Noncopyable.h>
#include <AK/Variant.h>
#include <cstdint>

namespace Core {

#if defined(AK_OS_WINDOWS)
using NativeFileType = void*;
using NativeSocketType = uintptr_t;
#else
struct PosixFileTag;
struct PosixSocketTag;
using NativeFileType = int;
using NativeSocketType = int;
#endif

namespace Detail {

#if defined(AK_OS_WINDOWS)
using InternalFileType = NativeFileType;
using InternalSocketType = NativeSocketType;
#else
struct PosixFileTag;
struct PosixSocketTag;
using InternalFileType = DistinctNumeric<int, PosixFileTag, -1>;
using InternalSocketType = DistinctNumeric<int, PosixSocketTag, -1>;
#endif

}

struct InvalidHandle {
    constexpr bool operator==(InvalidHandle const&) const = default;
};

class [[nodiscard]] PlatformHandle {
    AK_MAKE_DEFAULT_MOVABLE(PlatformHandle);
    AK_MAKE_DEFAULT_COPYABLE(PlatformHandle);

public:
    using HandleVariant = Variant<InvalidHandle, Detail::InternalFileType, Detail::InternalSocketType>;

    PlatformHandle()
        : m_handle(InvalidHandle {})
    {
    }
    ~PlatformHandle() = default;

    bool operator==(PlatformHandle const&) const = default;

    static PlatformHandle from_file(NativeFileType file);
    static PlatformHandle from_socket(NativeSocketType socket);

    explicit PlatformHandle(NativeFileType file);
    explicit PlatformHandle(NativeSocketType socket);

#if defined(AK_OS_WINDOWS)
    // NOTE: This is here because the windows CRT when converting between fd and handle returns an intptr_t.
    // If there is no more dependency on CRT functions that use FDs on windows these can be removed as well.
    explicit PlatformHandle(intptr_t file);
    operator intptr_t() const
    {
        // TODO: Variant get() verifies. Consider adding an unchecked get() and manually checking.
        return reinterpret_cast<intptr_t>(file());
    }
#endif

    [[nodiscard]] bool is_valid() const;
    explicit operator bool() const { return is_valid(); }

    void set_invalid()
    {
        m_handle.set(InvalidHandle {});
    }

    [[nodiscard]] bool is_file() const { return m_handle.has<Detail::InternalFileType>(); }
    [[nodiscard]] bool is_socket() const { return m_handle.has<Detail::InternalSocketType>(); }

    [[nodiscard]] NativeFileType file() const
    {
#if defined(AK_OS_WINDOWS)
        return m_handle.get<Detail::InternalFileType>();
#else
        return m_handle.get<Detail::InternalFileType>().value();
#endif
    }

    [[nodiscard]] NativeSocketType socket() const
    {
#if defined(AK_OS_WINDOWS)
        return m_handle.get<Detail::InternalSocketType>();
#else
        return m_handle.get<Detail::InternalSocketType>().value();
#endif
    }

    template<typename... Visitors>
    auto visit(Visitors&&... visitors)
    {
        return m_handle.visit(forward<Visitors>(visitors)...);
    }

private:
    friend class OwningPlatformHandle;
    explicit PlatformHandle(HandleVariant handle)
        : m_handle(move(handle))
    {
    }
    HandleVariant m_handle;
};

class [[nodiscard]] OwningPlatformHandle {
    AK_MAKE_NONCOPYABLE(OwningPlatformHandle);

public:
    OwningPlatformHandle() = default;

    ~OwningPlatformHandle()
    {
        close();
    }

    OwningPlatformHandle(OwningPlatformHandle&& other)
        : m_handle(other.release())
    {
    }
    OwningPlatformHandle& operator=(OwningPlatformHandle&& other)
    {
        if (this != &other) {
            close();
            m_handle = other.release();
        }
        return *this;
    }

    OwningPlatformHandle& operator=(PlatformHandle&& other)
    {

        close();
        m_handle = PlatformHandle { other.m_handle };

        return *this;
    }

    explicit OwningPlatformHandle(PlatformHandle&& handle);
    explicit OwningPlatformHandle(NativeFileType file);
    explicit OwningPlatformHandle(NativeSocketType socket);

#if defined(AK_OS_WINDOWS)
    explicit OwningPlatformHandle(intptr_t file);
#endif

    [[nodiscard]] bool is_valid() const { return m_handle.is_valid(); }
    explicit operator bool() const { return is_valid(); }
    void set_invalid()
    {
        m_handle.set_invalid();
    }

    [[nodiscard]] bool is_file() const { return m_handle.is_file(); }
    [[nodiscard]] bool is_socket() const { return m_handle.is_socket(); }

    [[nodiscard]] NativeFileType file() const { return m_handle.file(); }
    [[nodiscard]] NativeSocketType socket() const { return m_handle.socket(); }

    template<typename... Visitors>
    auto visit(Visitors&&... visitors)
    {
        return m_handle.visit(forward<Visitors>(visitors)...);
    }

    // Leaves the handle as InvalidHandle
    PlatformHandle release()
    {
        auto temp = m_handle;
        m_handle = PlatformHandle {};
        return temp;
    }

    PlatformHandle& handle()
    {
        return m_handle;
    }

    // NOTE:: I'm not fully sure about this one, but it avoids having to do .handle() everywhere where we have an OwningPlatformHandle
    operator PlatformHandle const&() const
    {
        return m_handle;
    }

    // Leaves m_handle as InvalidHandle
    void close();

private:
    PlatformHandle m_handle;
};

}

template<>
struct AK::Formatter<Core::PlatformHandle> { };

template<>
struct AK::Formatter<Core::OwningPlatformHandle> {
};
