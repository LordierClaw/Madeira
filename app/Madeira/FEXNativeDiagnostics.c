/* SPDX-License-Identifier: GPL-3.0-or-later
 * Madeira Converter Exception: see LICENSE-EXCEPTION.md
 *
 * FEX's pinned Apple configuration disables rpmalloc. Its CompileBlock log
 * nevertheless drains the PE allocator's optional CAS diagnostic snapshot.
 * No snapshot producer exists in this native app, so report none. The caller
 * only reads the output when the return value is nonzero. Allocation, atomic
 * operations and the PE allocator's own implementation are not affected.
 * This is Madeira's native linkage adapter; the FEX submodule is unchanged.
 */
#if defined(__APPLE__) && !defined(FEX_IOS_HOST)
struct rpm_cas_snapshot;

int rpm_cas_snapshot_take(struct rpm_cas_snapshot *snapshot)
{
    (void)snapshot;
    return 0;
}
#endif
