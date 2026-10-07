/* SPDX-License-Identifier: GPL-3.0-or-later
 * Madeira Converter Exception: see LICENSE-EXCEPTION.md
 *
 * The pinned FEX fork reads these two PE-only diagnostic counters from
 * CompileBlock even when FEX_IOS_HOST is not defined. Their definitions and
 * writers are guarded by FEX_IOS_HOST. Native Mach-O has neither PE callback
 * entry instrumentation nor the ARM64EC fast-forward bypass. Constant zero
 * counters keep those reports inactive without enabling PE-specific hooks in
 * the native library. This adapter belongs to Madeira; FEX remains unmodified.
 */
#pragma once
#if defined(__APPLE__) && defined(__cplusplus) && !defined(FEX_IOS_HOST)
#include <stddef.h>
#include <stdint.h>
[[maybe_unused]] static constexpr uint64_t IosFfsBypassLog[4] = {};
[[maybe_unused]] static constexpr uint64_t IosCbEntryLog[8] = {};

/* Arm64.cpp also queries Windows memory attributes solely for its unsupported
 * CASPAL diagnostic. A Mach-O library has no Windows address-space query. The
 * failed query leaves the existing log's type="?"; it does not change atomic
 * emulation, permission checks, or the caller's unsupported-instruction result.
 * This is only the report's field set, not a structure passed across a Wine ABI.
 */
using LPCVOID = const void*;
struct MEMORY_BASIC_INFORMATION {
    void* BaseAddress;
    size_t RegionSize;
    uint32_t Protect;
    uint32_t Type;
    uint32_t State;
};
[[maybe_unused]] static constexpr uint32_t MEM_IMAGE = 0x1000000;
[[maybe_unused]] static constexpr uint32_t MEM_MAPPED = 0x40000;
[[maybe_unused]] static inline size_t VirtualQuery(LPCVOID, MEMORY_BASIC_INFORMATION*, size_t) {
    return 0;
}
#endif
