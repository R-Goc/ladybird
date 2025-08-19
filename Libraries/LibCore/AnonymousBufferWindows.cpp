/*
 * Copyright (c) 2021, Andreas Kling <andreas@ladybird.org>
 * Copyright (c) 2024-2025, stasoid <stasoid@yahoo.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibCore/AnonymousBuffer.h>
#include <LibCore/PlatformHandle.h>
#include <LibCore/System.h>

#include <AK/Windows.h>

namespace Core {

AnonymousBufferImpl::AnonymousBufferImpl(PlatformHandle handle, size_t size, void* data)
    : m_handle(handle)
    , m_size(size)
    , m_data(data)
{
}

AnonymousBufferImpl::~AnonymousBufferImpl()
{
    if (m_data)
        VERIFY(UnmapViewOfFile(m_data));

    if (m_handle.is_valid())
        MUST(System::close(move(m_handle)));
}

ErrorOr<NonnullRefPtr<AnonymousBufferImpl>> AnonymousBufferImpl::create(size_t size)
{
    HANDLE map_handle = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, size >> 32, size & 0xFFFFFFFF, NULL);
    if (!map_handle)
        return Error::from_windows_error();

    return create(PlatformHandle { map_handle }, size);
}

ErrorOr<NonnullRefPtr<AnonymousBufferImpl>> AnonymousBufferImpl::create(PlatformHandle const& handle, size_t size)
{
    void* ptr = MapViewOfFile(handle.file(), FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (!ptr)
        return Error::from_windows_error();

    return adopt_ref(*new AnonymousBufferImpl(handle, size, ptr));
}

ErrorOr<AnonymousBuffer> AnonymousBuffer::create_with_size(size_t size)
{
    auto impl = TRY(AnonymousBufferImpl::create(size));
    return AnonymousBuffer(move(impl));
}

ErrorOr<AnonymousBuffer> AnonymousBuffer::create_from_anon_handle(PlatformHandle const& handle, size_t size)
{
    auto impl = TRY(AnonymousBufferImpl::create(handle, size));
    return AnonymousBuffer(move(impl));
}

}
