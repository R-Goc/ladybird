/*
 * Copyright (c) 2026-present, Ryszard Goc <ryszardgoc@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Assertions.h>
#include <AK/EnumBits.h>
#include <AK/Forward.h>
#include <AK/Noncopyable.h>
#include <AK/Platform.h>
#include <AK/Types.h>
#include <LibCore/Export.h>
#include <LibCore/File.h>

// TODO:: Add NUMA and huge page support
class CORE_API VirtualMemory final {
    AK_MAKE_NONCOPYABLE(VirtualMemory);
    AK_MAKE_DEFAULT_MOVABLE(VirtualMemory);

public:
    enum class Protection : u8 {
        NO_ACCESS = 1 << 0,
        READ = 1 << 1,
        WRITE = 1 << 2,
        READ_WRITE = READ | WRITE,
        EXECUTE = 1 << 3,
        EXECTUTE_READ = READ | EXECUTE,
        ALL_ACCESS = READ | WRITE | EXECUTE,
    };
    AK_ENUM_BITWISE_FRIEND_OPERATORS(Protection);

    // These are hints to the OS, if you are unsure of the workload, leave as None
    enum class Hint : u8 {
        None,
        Sequential,
        Random,
    };

    enum class State : u8 {
        Free,
        Reserved,
        Commited,
    };

    // NOTE: for more detailed explanations refer to man pages for mmap(2)
    enum class Flags : u8 {
        SHARED = 1 << 0,          // Create memory shared between processes
        PRIVATE = 1 << 1,         // Create a COW region
        FIXED_NOREPLACE = 1 << 2, // Fail if requested address is not available
        POPULATE = 1 << 3,        // Prefault the region
        JIT = 1 << 4,             // Needed on MacOS for WX mappings, no-op elsewhere
    };
    AK_ENUM_BITWISE_FRIEND_OPERATORS(Flags);

    // NOTE: If a null base address is provided to any of these functions the OS decides on the address
    // If size is zero the whole file is mapped. On Windows a file mapping or section handle may not be passed to the
    // create_from_file function. Use create_from_section instead.

    static ErrorOr<VirtualMemory> create_from_file(
        int fd,
        void* base_address,
        u64 size,
        u64 offset,
        State state = State::Commited,
        Protection protection = Protection::READ_WRITE,
        Hint hint = Hint::None);

    static ErrorOr<VirtualMemory> create_from_file(
        Core::File&& file,
        void* base_address,
        u64 size,
        u64 offset,
        State state = State::Commited,
        Protection protection = Protection::READ_WRITE,
        Hint hint = Hint::None);

#ifdef AK_OS_WINDOWS
    static ErrorOr<VirtualMemory> create_from_section(
        Core::File&& file,
        void* base_address,
        u64 size,
        u64 offset,
        State state = State::Commited,
        Protection protection = Protection::READ_WRITE,
        Hint hint = Hint::None);
#endif

    static ErrorOr<VirtualMemory> allocate(
        void* base_address,
        u64 size,
        State state = State::Commited,
        Protection protection = Protection::READ_WRITE);

    ErrorOr<void> release();

    ErrorOr<void> decommit();

    ErrorOr<void> flush_to_disk();
    ErrorOr<void> flush_icache();

    // NOTE: The caller has to keep track of the protection
    // The region covered by VirtualMemory doesn't need to have the same protection as such the size must be a multiple
    // of page size. If no size is provided protection is changed for the whole region. The old protection is returned
    ErrorOr<Protection> modify_protection(Protection desired_protection, Optional<u64> size);

    void* data() { return m_address; }

    // NOTE: This is never a file handle on windows. This is the handle to the section.
    // On POSIX this is either the fd for the file or an memfd/anon fd.
    int backing_fd() const
    {
        VERIFY(m_backing);
        return m_backing;
    }

private:
    void* m_address { nullptr };
    u64 m_size { 0 };
    int m_backing { 0 };
    State m_state { State::Free };
};
