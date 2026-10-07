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
#include <stdint.h>
[[maybe_unused]] static constexpr uint64_t IosFfsBypassLog[4] = {};
[[maybe_unused]] static constexpr uint64_t IosCbEntryLog[8] = {};
#endif
