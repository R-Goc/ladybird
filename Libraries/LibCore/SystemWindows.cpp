/*
 * Copyright (c) 2021-2022, Andreas Kling <kling@serenityos.org>
 * Copyright (c) 2021-2022, Kenneth Myhra <kennethmyhra@serenityos.org>
 * Copyright (c) 2021-2022, Sam Atkins <atkinssj@serenityos.org>
 * Copyright (c) 2022, Matthias Zimmerman <matthias291999@gmail.com>
 * Copyright (c) 2023, Cameron Youell <cameronyouell@gmail.com>
 * Copyright (c) 2024-2025, stasoid <stasoid@yahoo.com>
 * Copyright (c) 2025, ayeteadoe <ayeteadoe@gmail.com>
 * Copyright (c) 2025, Ryszard Goc <ryszardgoc@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "AK/Format.h"
#include <AK/Assertions.h>
#include <AK/ByteString.h>
#include <AK/Error.h>
#include <AK/ScopeGuard.h>
#include <LibCore/PlatformHandle.h>
#include <LibCore/Process.h>
#include <LibCore/System.h>
#include <cerrno>
#include <direct.h>
#include <sys/mman.h>

#include <AK/Windows.h>

namespace Core::System {

int windows_socketpair(SOCKET socks[2], int make_overlapped);

static void invalid_parameter_handler(wchar_t const*, wchar_t const*, wchar_t const*, unsigned int, uintptr_t)
{
}

static int init_crt_and_wsa()
{
    WSADATA wsa;
    WORD version = MAKEWORD(2, 2);
    int rc = WSAStartup(version, &wsa);
    VERIFY(!rc && wsa.wVersion == version);

    // Make _get_osfhandle return -1 instead of crashing on invalid fd in release (debug still __debugbreak's)
    _set_invalid_parameter_handler(invalid_parameter_handler);
    return 0;
}

static auto dummy = init_crt_and_wsa();

ErrorOr<OwningPlatformHandle> open(StringView path, int options, mode_t mode)
{
    ByteString str = path;
    int fd = _open(str.characters(), options | O_BINARY | _O_OBTAIN_DIR, mode);
    if (fd < 0)
        return Error::from_syscall("open"sv, errno);
    ScopeGuard guard = [&] { _close(fd); };
    return dup(PlatformHandle { _get_osfhandle(fd) });
}

ErrorOr<void> close(PlatformHandle&& handle)
{
    return handle.visit(
        [](InvalidHandle) -> ErrorOr<void> {
            // TODO: Return an error that makes sense here.
            VERIFY_NOT_REACHED();
            return {};
        },
        [](NativeFileType file)
            -> ErrorOr<void> {
            if (!CloseHandle(file)) {
                return Error::from_windows_error();
            }
            return {};
        },
        [](NativeSocketType socket) -> ErrorOr<void> {
            if (closesocket(socket))
                return Error::from_windows_error();
            return {};
        });
}

ErrorOr<ssize_t> read(PlatformHandle const& handle, Bytes buffer)
{
    DWORD n_read = 0;
    if (!ReadFile(handle.file(), buffer.data(), buffer.size(), &n_read, NULL))
        return Error::from_windows_error();
    return n_read;
}

ErrorOr<ssize_t> write(PlatformHandle const& handle, ReadonlyBytes buffer)
{
    DWORD n_written = 0;
    if (!WriteFile(handle.file(), buffer.data(), buffer.size(), &n_written, NULL))
        return Error::from_windows_error();
    return n_written;
}

ErrorOr<off_t> lseek(PlatformHandle const& handle, off_t offset, int origin)
{
    static_assert(FILE_BEGIN == SEEK_SET && FILE_CURRENT == SEEK_CUR && FILE_END == SEEK_END, "SetFilePointerEx origin values are incompatible with lseek");
    LARGE_INTEGER new_pointer = {};
    if (!SetFilePointerEx(handle.file(), { .QuadPart = offset }, &new_pointer, origin))
        return Error::from_windows_error();
    return new_pointer.QuadPart;
}

ErrorOr<void> ftruncate(PlatformHandle const& handle, off_t length)
{
    auto position = TRY(lseek(handle, 0, SEEK_CUR));
    ScopeGuard restore_position = [&] { MUST(lseek(handle, position, SEEK_SET)); };

    TRY(lseek(handle, length, SEEK_SET));

    if (!SetEndOfFile(handle.file()))
        return Error::from_windows_error();
    return {};
}

ErrorOr<struct stat> fstat(PlatformHandle const& handle)
{
    struct stat st = {};
    int fd = _open_osfhandle(TRY(dup(handle)).release(), 0);
    ScopeGuard guard = [&] { _close(fd); };
    if (::fstat(fd, &st) < 0)
        return Error::from_syscall("fstat"sv, errno);
    return st;
}

ErrorOr<void> ioctl(PlatformHandle const& handle, unsigned request, ...)
{
    va_list ap;
    va_start(ap, request);
    u_long arg = va_arg(ap, FlatPtr);
    va_end(ap);
    if (::ioctlsocket(handle.socket(), request, &arg) == SOCKET_ERROR)
        return Error::from_windows_error();
    return {};
}

ErrorOr<ByteString> getcwd()
{
    auto* cwd = _getcwd(nullptr, 0);
    if (!cwd)
        return Error::from_syscall("getcwd"sv, errno);

    ByteString string_cwd(cwd);
    free(cwd);
    return string_cwd;
}

ErrorOr<void> chdir(StringView path)
{
    if (path.is_null())
        return Error::from_errno(EFAULT);

    ByteString path_string = path;
    if (::_chdir(path_string.characters()) < 0)
        return Error::from_syscall("chdir"sv, errno);
    return {};
}

ErrorOr<struct stat> stat(StringView path)
{
    if (path.is_null())
        return Error::from_syscall("stat"sv, EFAULT);

    struct stat st = {};
    ByteString path_string = path;
    if (::stat(path_string.characters(), &st) < 0)
        return Error::from_syscall("stat"sv, errno);
    return st;
}

ErrorOr<void> rmdir(StringView path)
{
    if (path.is_null())
        return Error::from_errno(EFAULT);

    ByteString path_string = path;
    if (_rmdir(path_string.characters()) < 0)
        return Error::from_syscall("rmdir"sv, errno);
    return {};
}

ErrorOr<void> unlink(StringView path)
{
    if (path.is_null())
        return Error::from_errno(EFAULT);

    ByteString path_string = path;
    if (_unlink(path_string.characters()) < 0)
        return Error::from_syscall("unlink"sv, errno);
    return {};
}

ErrorOr<void> mkdir(StringView path, mode_t)
{
    ByteString str = path;
    if (_mkdir(str.characters()) < 0)
        return Error::from_syscall("mkdir"sv, errno);
    return {};
}

ErrorOr<OwningPlatformHandle> openat(int, StringView, int, mode_t)
{
    dbgln("Core::System::openat() is not implemented");
    VERIFY_NOT_REACHED();
}

ErrorOr<struct stat> fstatat(PlatformHandle const&, StringView, int)
{
    dbgln("Core::System::fstatat() is not implemented");
    VERIFY_NOT_REACHED();
}

ErrorOr<void*> mmap(void* address, size_t size, int protection, int flags, PlatformHandle const& file, off_t offset, size_t alignment, StringView)
{
    // custom alignment is not supported
    VERIFY(!alignment);
    int fd = _open_osfhandle(reinterpret_cast<intptr_t>(TRY(dup(file)).file()), 0);
    ScopeGuard guard
        = [&] { _close(fd); };
    void* ptr = ::mmap(address, size, protection, flags, fd, offset);
    if (ptr == MAP_FAILED)
        return Error::from_syscall("mmap"sv, errno);
    return ptr;
}

ErrorOr<void> munmap(void* address, size_t size)
{
    if (::munmap(address, size) < 0)
        return Error::from_syscall("munmap"sv, errno);
    return {};
}

int getpid()
{
    return GetCurrentProcessId();
}

ErrorOr<OwningPlatformHandle> dup(PlatformHandle const& handle)
{
    if (!handle.is_valid())
        return Error::from_windows_error(ERROR_INVALID_HANDLE);

    if (handle.is_socket()) {
        WSAPROTOCOL_INFO pi = {};
        if (WSADuplicateSocket(handle.socket(), GetCurrentProcessId(), &pi))
            return Error::from_windows_error();
        SOCKET socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, &pi, 0, WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
        if (socket == INVALID_SOCKET)
            return Error::from_windows_error();
        return OwningPlatformHandle { socket };
    }
    HANDLE new_handle = 0;
    if (!DuplicateHandle(GetCurrentProcess(), handle.file(), GetCurrentProcess(), &new_handle, 0, FALSE, DUPLICATE_SAME_ACCESS))
        return Error::from_windows_error();
    return OwningPlatformHandle { new_handle };
}

bool is_socket(PlatformHandle const& handle)
{
    // FILE_TYPE_PIPE is returned for sockets and pipes. We don't use Windows pipes.
    return GetFileType(handle.file()) == FILE_TYPE_PIPE;
}

ErrorOr<void> bind(PlatformHandle const& sock_handle, struct sockaddr const* name, socklen_t name_size)
{
    if (::bind(sock_handle.socket(), name, name_size) == SOCKET_ERROR)
        return Error::from_windows_error();
    return {};
}

ErrorOr<void> listen(PlatformHandle const& sock_handle, int backlog)
{
    if (::listen(sock_handle.socket(), backlog) == SOCKET_ERROR)
        return Error::from_windows_error();
    return {};
}

ErrorOr<int> accept(int sockfd, struct sockaddr* addr, socklen_t* addr_size)
{
    auto fd = ::accept(sockfd, addr, addr_size);
    if (fd == INVALID_SOCKET)
        return Error::from_windows_error();
    return fd;
}

ErrorOr<ssize_t> sendto(int sockfd, void const* source, size_t source_length, int flags, struct sockaddr const* destination, socklen_t destination_length)
{
    auto sent = ::sendto(sockfd, static_cast<char const*>(source), source_length, flags, destination, destination_length);
    if (sent == SOCKET_ERROR)
        return Error::from_windows_error();
    return sent;
}

ErrorOr<ssize_t> recvfrom(int sockfd, void* buffer, size_t buffer_length, int flags, struct sockaddr* address, socklen_t* address_length)
{
    auto received = ::recvfrom(sockfd, static_cast<char*>(buffer), buffer_length, flags, address, address_length);
    if (received == SOCKET_ERROR)
        return Error::from_windows_error();
    return received;
}

ErrorOr<void> getsockname(PlatformHandle const& sock_handle, struct sockaddr* name, socklen_t* name_size)
{
    if (::getsockname(sock_handle.socket(), name, name_size) == SOCKET_ERROR)
        return Error::from_windows_error();
    return {};
}

ErrorOr<void> setsockopt(PlatformHandle const& sock_handle, int level, int option, void const* value, socklen_t value_size)
{
    if (::setsockopt(sock_handle.socket(), level, option, static_cast<char const*>(value), value_size) == SOCKET_ERROR)
        return Error::from_windows_error();
    return {};
}

ErrorOr<void> socketpair(int domain, int type, int protocol, PlatformHandle sv[2])
{
    if (domain != AF_LOCAL || type != SOCK_STREAM || protocol != 0)
        return Error::from_string_literal("Unsupported argument value");

    SOCKET socks[2] = {};
    if (windows_socketpair(socks, true))
        return Error::from_windows_error();

    sv[0] = PlatformHandle::from_socket(socks[0]);
    sv[1] = PlatformHandle::from_socket(socks[1]);
    return {};
}

ErrorOr<void> sleep_ms(u32 milliseconds)
{
    Sleep(milliseconds);
    return {};
}

unsigned hardware_concurrency()
{
    SYSTEM_INFO si = {};
    GetSystemInfo(&si);
    // number of logical processors in the current group (max 64)
    return si.dwNumberOfProcessors;
}

u64 physical_memory_bytes()
{
    MEMORYSTATUSEX ms = {};
    ms.dwLength = sizeof ms;
    GlobalMemoryStatusEx(&ms);
    return ms.ullTotalPhys;
}

ErrorOr<ByteString> current_executable_path()
{
    return TRY(Process::get_name()).to_byte_string();
}

ErrorOr<void> set_close_on_exec(PlatformHandle const& handle, bool enabled)
{
    if (handle.is_file()) {
        if (!SetHandleInformation(handle.file(), HANDLE_FLAG_INHERIT, enabled ? 0 : HANDLE_FLAG_INHERIT))
            return Error::from_windows_error();
    } else if (handle.is_socket()) {
        if (!SetHandleInformation(reinterpret_cast<HANDLE>(handle.socket()), HANDLE_FLAG_INHERIT, enabled ? 0 : HANDLE_FLAG_INHERIT))
            return Error::from_windows_error();
    } else {
        return Error::from_windows_error(EINVAL);
    }
    return {};
}

ErrorOr<bool> isatty(int handle)
{
    return GetFileType(to_handle(handle)) == FILE_TYPE_CHAR;
}

ErrorOr<OwningPlatformHandle> socket(int domain, int type, int protocol)
{
    auto socket = ::socket(domain, type, protocol);
    if (socket == INVALID_SOCKET)
        return Error::from_windows_error();
    return OwningPlatformHandle { socket };
}

ErrorOr<AddressInfoVector> getaddrinfo(char const* nodename, char const* servname, struct addrinfo const& hints)
{
    struct addrinfo* results = nullptr;

    int rc = ::getaddrinfo(nodename, servname, &hints, &results);
    if (rc != 0)
        return Error::from_windows_error(rc);

    Vector<struct addrinfo> addresses;

    for (auto* result = results; result != nullptr; result = result->ai_next)
        TRY(addresses.try_append(*result));

    return AddressInfoVector { move(addresses), results };
}

ErrorOr<void> connect(PlatformHandle const& sock_handle, struct sockaddr const* address, socklen_t address_length)
{
    if (::connect(sock_handle.socket(), address, address_length) == SOCKET_ERROR)
        return Error::from_windows_error();
    return {};
}

}
