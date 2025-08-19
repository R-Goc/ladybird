/*
 * Copyright (c) 2025, stasoid <stasoid@yahoo.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibCore/PlatformHandle.h>
#include <LibIPC/Decoder.h>
#include <LibIPC/File.h>
#include <LibIPC/HandleType.h>

#include <AK/Windows.h>
#include <cstdint>

namespace IPC {

template<>
ErrorOr<File> decode(Decoder& decoder)
{
    auto handle_type = TRY(decoder.decode<HandleType>());
    Core::PlatformHandle handle {};
    if (handle_type == HandleType::Generic) {
        uintptr_t temp;
        TRY(decoder.decode_into(temp));
        handle = Core::PlatformHandle::from_file(reinterpret_cast<Core::NativeFileType>(temp));
    } else if (handle_type == HandleType::Socket) {
        WSAPROTOCOL_INFO pi = {};
        TRY(decoder.decode_into({ reinterpret_cast<u8*>(&pi), sizeof(pi) }));
        handle = Core::PlatformHandle::from_socket(WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, &pi, 0, WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT));
        if (!handle.is_valid())
            return Error::from_windows_error();
    } else {
        return Error::from_string_literal("Invalid handle type");
    }
    return File::adopt_handle(handle);
}

}
