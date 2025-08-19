/*
 * Copyright (c) 2020, Sergey Bugaev <bugaevc@serenityos.org>
 * Copyright (c) 2021, Andreas Kling <andreas@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Noncopyable.h>
#include <AK/StdLibExtras.h>
#include <LibCore/File.h>
#include <LibCore/PlatformHandle.h>
#include <LibCore/System.h>

namespace IPC {

class File {
    AK_MAKE_NONCOPYABLE(File);

public:
    File() = default;

    static File adopt_file(NonnullOwnPtr<Core::File> file)
    {
        return File(file->leak_handle());
    }

    static File adopt_handle(Core::PlatformHandle handle)
    {
        return File(handle);
    }

    static ErrorOr<File> clone_handle(Core::PlatformHandle handle)
    {
        Core::PlatformHandle new_handle = TRY(Core::System::dup(handle));
        return File(new_handle);
    }

    File(File&& other)
        : m_handle(exchange(other.m_handle, Core::PlatformHandle {}))
    {
    }

    File& operator=(File&& other)
    {
        if (this != &other) {
            m_handle = exchange(other.m_handle, Core::PlatformHandle {});
        }
        return *this;
    }

    ~File()
    {
        if (m_handle.is_valid())
            (void)Core::System::close(move(m_handle));
    }

    Core::PlatformHandle handle() const { return m_handle; }

    // NOTE: This is 'const' since generated IPC messages expose all parameters by const reference.
    [[nodiscard]] Core::PlatformHandle take_handle() const
    {
        return exchange(m_handle, Core::PlatformHandle {});
    }

    // FIXME: IPC::Files transferred over the wire are always set O_CLOEXEC during decoding.
    //        Perhaps we should add an option to IPC::File to allow the receiver to decide whether to
    //        make it O_CLOEXEC or not. Or an attribute in the .ipc file?
    ErrorOr<void> clear_close_on_exec()
    {
        return Core::System::set_close_on_exec(m_handle, false);
    }

private:
    explicit File(Core::PlatformHandle handle)
        : m_handle(handle)
    {
    }

    mutable Core::PlatformHandle m_handle;
};

}
