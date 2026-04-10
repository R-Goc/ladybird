/*
 * Copyright (c) 2026, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Arch {
    X86_64,
    Aarch64,
}

use crate::parser::ObjectFormat;

/// DSL register names and their platform mappings.
/// All of pc, pb, values, exec_ctx, dispatch are callee-saved so they survive C++ calls.
pub struct RegisterMapping {
    pub pc: &'static str,
    pub pb: &'static str,
    pub values: &'static str,
    pub exec_ctx: &'static str,
    pub dispatch: &'static str,
    pub temporaries: &'static [&'static str],
    pub fp_temporaries: &'static [&'static str],
    pub arguments: &'static [&'static str],
    pub sp: &'static str,
    pub fp: &'static str,
}

pub const X86_64_SYSV_REGS: RegisterMapping = RegisterMapping {
    pc: "r13",
    pb: "r14",
    values: "r15",
    exec_ctx: "rbx",
    dispatch: "r12",
    temporaries: &["rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11"],
    fp_temporaries: &["xmm0", "xmm1", "xmm2", "xmm3"],
    arguments: &["rdi", "rsi", "rdx", "rcx", "r8", "r9"],
    sp: "rsp",
    fp: "rbp",
};

pub const X86_64_MSVC_REGS: RegisterMapping = RegisterMapping {
    pc: "r13",
    pb: "r14",
    values: "r15",
    exec_ctx: "rbx",
    dispatch: "r12",
    temporaries: &["rax", "r10", "r11", "rdi", "rsi", "rdx", "rcx", "r8", "r9"],
    fp_temporaries: &["xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5"],
    arguments: &["rcx", "rdx", "r8", "r9"],
    sp: "rsp",
    fp: "rbp",
};

pub const AARCH64_REGS: RegisterMapping = RegisterMapping {
    pc: "x25",
    pb: "x26",
    values: "x27",
    exec_ctx: "x28",
    dispatch: "x19",
    temporaries: &[
        "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", "x13",
        "x14", "x15", "x16", "x17",
    ],
    fp_temporaries: &["d0", "d1", "d2", "d3"],
    arguments: &["x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7"],
    sp: "sp",
    fp: "x29",
};

pub fn mapping_for(arch: Arch, format: ObjectFormat) -> &'static RegisterMapping {
    match arch {
        Arch::X86_64 => {
            if format == ObjectFormat::COFF {
                &X86_64_MSVC_REGS
            } else {
                &X86_64_SYSV_REGS
            }
        }
        Arch::Aarch64 => {
            if format == ObjectFormat::COFF {
                todo!();
            } else {
                &AARCH64_REGS
            }
        }
    }
}

/// Resolve a DSL register name to a platform register name.
pub fn resolve_register(name: &str, arch: Arch, format: ObjectFormat) -> Option<String> {
    let m = mapping_for(arch, format);
    match name {
        "pc" => Some(m.pc.to_string()),
        "pb" => Some(m.pb.to_string()),
        "values" => Some(m.values.to_string()),
        "exec_ctx" => Some(m.exec_ctx.to_string()),
        "dispatch" => Some(m.dispatch.to_string()),
        "sp" => Some(m.sp.to_string()),
        "fp" => Some(m.fp.to_string()),
        _ => {
            // arg0-arg7 -> arguments
            if let Some(idx_str) = name.strip_prefix("arg") {
                if let Ok(idx) = idx_str.parse::<usize>() {
                    if idx < m.arguments.len() {
                        return Some(m.arguments[idx].to_string());
                    }
                }
            }
            // t0-t9 -> temporaries
            if let Some(idx_str) = name.strip_prefix('t') {
                if let Ok(idx) = idx_str.parse::<usize>() {
                    if idx < m.temporaries.len() {
                        return Some(m.temporaries[idx].to_string());
                    }
                }
            }
            // ft0-ft3 -> fp temporaries
            if let Some(idx_str) = name.strip_prefix("ft") {
                if let Ok(idx) = idx_str.parse::<usize>() {
                    if idx < m.fp_temporaries.len() {
                        return Some(m.fp_temporaries[idx].to_string());
                    }
                }
            }
            None
        }
    }
}
