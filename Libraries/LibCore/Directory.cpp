/*
 * Copyright (c) 2022, kleines Filmröllchen <filmroellchen@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibCore/Directory.h>
#include <LibCore/PlatformHandle.h>
#include <LibCore/System.h>

namespace Core {

// We assume that the fd is a valid directory.
Directory::Directory(PlatformHandle handle, LexicalPath path)
    : m_path(move(path))
    , m_directory_handle(handle)
{
}

Directory::Directory(Directory&& other)
    : m_path(move(other.m_path))
    , m_directory_handle(other.m_directory_handle)
{
    other.m_directory_handle.set_invalid();
}

Directory::~Directory()
{
    if (m_directory_handle.is_valid())
        MUST(System::close(move(m_directory_handle)));
}

#ifndef AK_OS_WINDOWS
ErrorOr<void> Directory::chown(uid_t uid, gid_t gid)
{
    if (m_directory_fd == -1)
        return Error::from_syscall("fchown"sv, EBADF);
    TRY(Core::System::fchown(m_directory_fd, uid, gid));
    return {};
}
#endif

ErrorOr<bool> Directory::is_valid_directory(PlatformHandle handle)
{
    auto stat = TRY(System::fstat(handle));
    return stat.st_mode & S_IFDIR;
}

ErrorOr<Directory> Directory::adopt_handle(PlatformHandle handle, LexicalPath path)
{
    // This will also fail if the fd is invalid in the first place.
    if (!TRY(Directory::is_valid_directory(handle)))
        return Error::from_errno(ENOTDIR);
    return Directory { handle, move(path) };
}

ErrorOr<Directory> Directory::create(ByteString path, CreateDirectories create_directories, mode_t creation_mode)
{
    return create(LexicalPath { move(path) }, create_directories, creation_mode);
}

ErrorOr<Directory> Directory::create(LexicalPath path, CreateDirectories create_directories, mode_t creation_mode)
{
    if (create_directories == CreateDirectories::Yes)
        TRY(ensure_directory(path, creation_mode));
    // FIXME: doesn't work on Linux probably
    auto handle = TRY(System::open(path.string(), O_CLOEXEC));
    return adopt_handle(handle, move(path));
}

ErrorOr<void> Directory::ensure_directory(LexicalPath const& path, mode_t creation_mode)
{
    if (path.is_root() || path.string() == ".")
        return {};

    TRY(ensure_directory(path.parent(), creation_mode));

    auto return_value = System::mkdir(path.string(), creation_mode);
    // We don't care if the directory already exists.
    if (return_value.is_error() && return_value.error().code() != EEXIST)
        return return_value;

    return {};
}

ErrorOr<NonnullOwnPtr<File>> Directory::open(StringView filename, File::OpenMode mode) const
{
    auto handle = TRY(System::openat(m_directory_handle, filename, File::open_mode_to_options(mode)));
    return File::adopt_handle(handle, mode);
}

ErrorOr<struct stat> Directory::stat(StringView filename, int flags) const
{
    return System::fstatat(m_directory_handle, filename, flags);
}

ErrorOr<struct stat> Directory::stat() const
{
    return System::fstat(m_directory_handle);
}

ErrorOr<void> Directory::for_each_entry(DirIterator::Flags flags, Core::Directory::ForEachEntryCallback callback)
{
    DirIterator iterator { path().string(), flags };
    if (iterator.has_error())
        return iterator.error();

    while (iterator.has_next()) {
        if (iterator.has_error())
            return iterator.error();

        auto entry = iterator.next();
        if (!entry.has_value())
            break;

        auto decision = TRY(callback(entry.value(), *this));
        if (decision == IterationDecision::Break)
            break;
    }

    return {};
}

ErrorOr<void> Directory::for_each_entry(AK::StringView path, DirIterator::Flags flags, Core::Directory::ForEachEntryCallback callback)
{
    auto directory = TRY(Directory::create(path, CreateDirectories::No));
    return directory.for_each_entry(flags, move(callback));
}

}
