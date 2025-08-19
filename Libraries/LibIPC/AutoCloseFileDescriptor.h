/*
 * Copyright (c) 2025, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/RefCounted.h>
#include <LibCore/PlatformHandle.h>
#include <LibCore/System.h>

namespace IPC {

// FIXME: Look if this even needs to exist with OwningPlatformHandle
class AutoCloseFileDescriptor : public RefCounted<AutoCloseFileDescriptor> {
public:
    AutoCloseFileDescriptor(Core::PlatformHandle handle)
        : m_handle(handle)
    {
    }

    ~AutoCloseFileDescriptor() = default;

    Core::PlatformHandle value() const { return m_handle; }

    Core::PlatformHandle take_handle()
    {
        return m_handle.release();
    }

private:
    Core::OwningPlatformHandle m_handle;
};

}
