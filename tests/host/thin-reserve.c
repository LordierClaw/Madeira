/* Source-extracted thin-reserve entry point, with deterministic VM/lock mocks. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

typedef uintptr_t ULONG_PTR;
typedef size_t SIZE_T;
typedef unsigned ULONG;
typedef int NTSTATUS;
typedef int BOOL;
#define FALSE 0
#define TRUE 1
#define PAGE_READWRITE 4
#define MEM_RESERVE 0x2000
#define STATUS_SUCCESS 0
#define ROUND_SIZE(addr, size, mask) (((size) + (mask)) & ~(mask))
static const SIZE_T granularity_mask = 0xffff;
static int virtual_mutex, lock_depth, unlocked_reads, mapped_heads;
static int ios_thin_placing;
static const char *mode, *slot_mb, *after_gb;

static void server_enter_uninterrupted_section(int *mutex, int *signals)
{ (void)mutex; (void)signals; lock_depth++; }
static void server_leave_uninterrupted_section(int *mutex, int *signals)
{ (void)mutex; (void)signals; lock_depth--; }
static int ignore_log(int fd, const char *fmt, ...)
{ (void)fd; (void)fmt; return 0; }
static char *test_getenv(const char *name)
{
    if (!lock_depth) unlocked_reads++;
    if (!strcmp(name, "MADEIRA_THIN_RESERVE")) return (char *)mode;
    if (!strcmp(name, "MADEIRA_THIN_RESERVE_MB")) return (char *)slot_mb;
    if (!strcmp(name, "MADEIRA_THIN_RESERVE_AFTER_GB")) return (char *)after_gb;
    return NULL;
}
#define getenv test_getenv
#define dprintf ignore_log
#include "thin-config.inc"

/* No real multi-GB mapping: test admission and lock ordering only. */
static int ios_thin_new_arena(void)
{
    if (ios_thin_arena_n == IOS_THIN_ARENAS) return 0;
    struct ios_thin_arena *arena = &ios_thin_arenas[ios_thin_arena_n++];
    arena->base = (ULONG_PTR)0x1000000000ULL + (ULONG_PTR)ios_thin_arena_n * IOS_THIN_HEADS;
    arena->nslots = 2;
    return 1;
}
static NTSTATUS allocate_virtual_memory(void **base, SIZE_T *size, ULONG type, ULONG protect,
                                       ULONG_PTR low, ULONG_PTR high, ULONG_PTR align, ULONG attrs)
{
    (void)base; (void)size; (void)type; (void)protect;
    (void)low; (void)high; (void)align; (void)attrs;
    if (!lock_depth || !ios_thin_placing) return -1;
    mapped_heads++;
    return STATUS_SUCCESS;
}
#include "thin-reserve.inc"
struct file_view { void *base; SIZE_T size; };
static struct file_view *find_view(void *base, SIZE_T size)
{ (void)base; (void)size; return NULL; } /* tail cases have no committed view */
#include "thin-decommit.inc"

static int failures;
static void check(int ok, const char *message)
{
    if (!ok) { fprintf(stderr, "FAIL: %s\n", message); failures++; }
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    const char *test = argv[1];
    if (!strncmp(test, "tail-", 5))
    {
        ios_thin_n = 1;
        ios_thin_head = 63 * 1024 * 1024;
        ios_thin[0].live = 1;
        ios_thin[0].base = (ULONG_PTR)0x1000000000ULL;
        ios_thin[0].size = IOS_THIN_MIN_SIZE;
        SIZE_T size = !strcmp(test, "tail-valid") ? 4096 :
                      !strcmp(test, "tail-end") ? IOS_THIN_MIN_SIZE / 2 :
                      !strcmp(test, "tail-overrun") ? IOS_THIN_MIN_SIZE :
                      !strcmp(test, "tail-wrap") ? SIZE_MAX : 0;
        SIZE_T original = size;
        BOOL expected = !strcmp(test, "tail-valid") || !strcmp(test, "tail-end");
        BOOL handled = ios_thin_decommit_fixup((void *)(ios_thin[0].base + IOS_THIN_MIN_SIZE / 2), &size);
        check(handled == expected, "only nonzero decommits inside the tail may bypass normal VM validation");
        check(size == original, "tail validation must leave the caller's extent intact");
        if (!failures) printf("PASS: %s\n", test);
        return failures ? 1 : 0;
    }
    int enabled = !strcmp(test, "opt-in") || !strcmp(test, "locking") ||
                  !strcmp(test, "threshold") || !strcmp(test, "minimum") || !strcmp(test, "maximum");
    mode = enabled ? "1" : !strcmp(test, "empty") ? "" : !strcmp(test, "off") ? "0" :
           !strcmp(test, "invalid") ? "yes" : NULL;
    if (!strcmp(test, "minimum")) slot_mb = "1";
    if (!strcmp(test, "maximum")) slot_mb = "1024";
    after_gb = !strcmp(test, "threshold") ? NULL : "0";
    for (int i = 0; i < 10; i++)
    {
        void *base = NULL;
        SIZE_T size = IOS_THIN_MIN_SIZE;
        NTSTATUS status = -1;
        BOOL done = ios_thin_reserve(&base, &size, PAGE_READWRITE, &status);
        if (!enabled)
        {
            check(!done && !base && status == -1, "unset/invalid mode must leave normal allocation untouched");
            check(!mapped_heads && !ios_thin_arena_n && !ios_thin_seen, "disabled mode must not create arenas or count reservations");
        }
        else if (!strcmp(test, "threshold") && i < 8)
            check(!done && !base, "first 8 GB follow normal reservation path");
        else if (i < 8 || !strcmp(test, "threshold"))
            check(done && base && status == STATUS_SUCCESS && size == IOS_THIN_MIN_SIZE,
                  "explicit opt-in admits thin reservation after threshold");
        check(lock_depth == 0, "all return paths release virtual_mutex");
    }
    if (!strcmp(test, "locking"))
        check(unlocked_reads == 0, "configuration must initialize under virtual_mutex before another allocator sees it");
    if (enabled)
    {
        ULONG_PTR mb = !strcmp(test, "minimum") ? 16 : !strcmp(test, "maximum") ? 512 : 64;
        check(ios_thin_stride == mb * 1024 * 1024 && ios_thin_head == ios_thin_stride - IOS_THIN_GUARD,
              "slot and guard initialized with supported bounds");
    }
    if (!failures) printf("PASS: %s\n", test);
    return failures ? 1 : 0;
}
