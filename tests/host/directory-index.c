/* Deterministic fault/mutation tests for the production iOS name index. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <time.h>

#define IOS_INDEX_TEST 1
#define MAX_DIR_ENTRY_LEN 255
typedef wchar_t WCHAR;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define O_RDONLY 0
#define O_DIRECTORY 1
#define O_CLOEXEC 2
#define F_SETFD 0
#define FD_CLOEXEC 0
#define S_ISDIR(mode) ((mode) == 1)
#define EVFILT_VNODE 1
#define EV_ADD 1
#define EV_CLEAR 2
#define NOTE_WRITE 1
#define NOTE_ATTRIB 2
#define NOTE_DELETE 4
#define NOTE_RENAME 8
#define NOTE_LINK 16
#define NOTE_REVOKE 32
typedef int pthread_mutex_t;
#define PTHREAD_MUTEX_INITIALIZER 0
static int pthread_mutex_trylock(int *p) { if (*p) return 1; *p = 1; return 0; }
static void pthread_mutex_unlock(int *p) { *p = 0; }
struct stat { int st_dev, st_ino, st_mode, st_uid, st_gid; struct timespec st_mtimespec, st_ctimespec; };
struct kevent { void *udata; int ident; };
#define EV_SET(e, id, filter, flags, notes, data, user) ((e)->udata = (user), (e)->ident = (id))
struct dirent { char d_name[256]; };
typedef struct { int node, next; struct dirent entry; } DIR;
static struct { char names[2000][128]; int count, generation, identity; } fs[80];
static int handles[200], handle_count, event_pending[80], poll_error, register_error, read_error, mutate_read;
static void *watches[80];
static int scans, builds, hits, stale, fallbacks, entries, case_queries;
#define IOS_FILE_COUNT(n) IOS_COUNT_##n++
#define IOS_COUNT_scans scans
#define IOS_COUNT_index_build builds
#define IOS_COUNT_index_hit hits
#define IOS_COUNT_index_stale stale
#define IOS_COUNT_index_fallback fallbacks
#define IOS_COUNT_entries entries
#define IOS_COUNT_case_query case_queries
static int kqueue(void) { return 199; }
static int fcntl(int fd, int cmd, int value) { (void)fd; (void)cmd; (void)value; return 0; }
static int kevent(int q, const struct kevent *changes, int n, struct kevent *events, int max, const struct timespec *t)
{
    (void)q; (void)n; (void)t;
    if (changes) { if (register_error) return -1; watches[handles[changes->ident]] = changes->udata; event_pending[handles[changes->ident]] = 0; return 0; }
    if (poll_error) return -1;
    int count = 0;
    for (int i = 0; i < 80 && count < max; i++) if (event_pending[i] && watches[i])
    { events[count++].udata = watches[i]; event_pending[i] = 0; }
    return count;
}
static int fstatat(int root, const char *path, struct stat *st, int flags)
{
    (void)flags;
    int node = !strcmp(path, ".") ? handles[root] : atoi(path) + root;
    if (node < 0 || node >= 80 || !fs[node].identity) return -1;
    memset(st, 0, sizeof(*st));
    st->st_dev = 1; st->st_ino = fs[node].identity; st->st_mode = 1;
    st->st_mtimespec.tv_nsec = st->st_ctimespec.tv_nsec = fs[node].generation;
    return 0;
}
static int fstat(int fd, struct stat *st) { return fstatat(fd, ".", st, 0); }
static int openat(int root, const char *path, int flags)
{
    (void)flags;
    int node = !strcmp(path, ".") ? handles[root] : atoi(path) + root;
    if (node < 0 || node >= 80 || !fs[node].identity) return -1;
    for (int i = 1; i < 199; i++) if (handles[i] == -1)
    { handles[i] = node; handle_count++; return i; }
    return -1;
}
static int close(int fd) { handles[fd] = -1; handle_count--; return 0; }
static DIR *fdopendir(int fd)
{ DIR *d = calloc(1, sizeof(*d)); d->node = fd; return d; }
static void closedir(DIR *d) { close(d->node); free(d); }
static struct dirent *readdir(DIR *d)
{
    int node = handles[d->node];
    if (read_error) { errno = EIO; return NULL; }
    if (mutate_read) { fs[node].generation++; event_pending[node] = 1; mutate_read = 0; }
    if (d->next == fs[node].count) return NULL;
    strcpy(d->entry.d_name, fs[node].names[d->next++]); return &d->entry;
}
static int get_dir_case_sensitivity(int root, const char *path) { (void)root; (void)path; return 1; }
static int ntdll_umbstowcs(const char *in, int n, WCHAR *out, int max)
{ if (n > max) return -1; for (int i = 0; i < n; i++) out[i] = (unsigned char)in[i]; return n; }
#define wcsnicmp mock_wcsnicmp
static int mock_wcsnicmp(const WCHAR *a, const WCHAR *b, int n)
{ for (int i = 0; i < n; i++) if (towupper(a[i]) != towupper(b[i])) return 1; return 0; }
#include "ios_directory_index.h"

static int query(int root, const char *path, const WCHAR *name, const char *expected, int reparse)
{
    char out[256]; BOOL rp = FALSE;
    int result = ios_index_lookup(root, path, name, wcslen(name), out, &rp);
    assert(result == (expected ? 1 : 0));
    if (expected) assert(!strcmp(out, expected)); else assert(rp == reparse);
    return result;
}
static void change(int node, const char *name)
{
    strcpy(fs[node].names[fs[node].count++], name);
    fs[node].generation++; event_pending[node] = 1;
}
int main(void)
{
    memset(handles, -1, sizeof(handles));
    for (int i = 0; i < 80; i++) fs[i].identity = i + 1;
    change(0, "Asset.JSON"); change(0, "asset.json"); change(0, "link?");
    query(0, "0", L"ASSET.JSON", "Asset.JSON", 0);
    query(0, "0", L"missing", NULL, 0);
    query(0, "0", L"link", NULL, 1);
    for (int i = 0; i < 10000; i++) query(0, "0", L"missing", NULL, 0);
    assert(scans == 1 && builds == 1 && hits == 10002 && handle_count == 1);
    /* A native event invalidates even when timestamps are indistinguishable. */
    int generation = fs[0].generation;
    change(0, "Mod.JSON"); fs[0].generation = generation;
    query(0, "0", L"mod.json", "Mod.JSON", 0);
    fs[0].count = 0; fs[0].generation++;
    query(0, "0", L"asset.json", NULL, 0);
    /* Same path, replacement inode. Root handle reuse also changes identity. */
    fs[0].identity = 1000; change(0, "New.JSON");
    query(0, "0", L"new.json", "New.JSON", 0);
    query(1, "0", L"new.json", NULL, 0);
    char out[256]; BOOL rp = FALSE;
    mutate_read = 1; change(1, "race");
    assert(ios_index_lookup(0, "1", L"race", 4, out, &rp) == -1);
    query(0, "1", L"race", "race", 0);
    change(1, "broken"); read_error = 1;
    assert(ios_index_lookup(0, "1", L"broken", 6, out, &rp) == -1);
    read_error = 0; query(0, "1", L"broken", "broken", 0);
    poll_error = 1;
    assert(ios_index_lookup(0, "1", L"absent", 6, out, &rp) == -1);
    poll_error = 0; query(0, "1", L"broken", "broken", 0);
    register_error = 1; change(1, "watchfail");
    assert(ios_index_lookup(0, "1", L"absent", 6, out, &rp) == -1);
    register_error = 0;
    pthread_mutex_trylock(&ios_index_mutex);
    assert(ios_index_lookup(0, "1", L"absent", 6, out, &rp) == -1);
    pthread_mutex_unlock(&ios_index_mutex);
    /* Oversized directories are not repeatedly rebuilt. Changes retry. */
    for (int i = 0; i < 2000; i++) { char name[128]; snprintf(name, sizeof(name), "entry-with-a-long-name-%04d.json", i); change(2, name); }
    assert(ios_index_lookup(0, "2", L"absent", 6, out, &rp) == -1);
    int old_scans = scans;
    for (int i = 0; i < 100; i++) assert(ios_index_lookup(0, "2", L"absent", 6, out, &rp) == -1);
    assert(scans == old_scans);
    fs[2].count = 0; fs[2].generation++; query(0, "2", L"absent", NULL, 0);
    for (int i = 3; i < 80; i++)
    { char path[16]; snprintf(path, sizeof(path), "%d", i); query(0, path, L"absent", NULL, 0); }
    assert(handle_count == IOS_INDEX_SLOTS);
    size_t bytes = sizeof(ios_index_dirs);
    assert(bytes < 65536);
    for (int i = 0; i < IOS_INDEX_SLOTS; i++) if (ios_index_dirs[i].data) bytes += IOS_INDEX_BYTES;
    assert(bytes <= 2 * 1024 * 1024 + 65536);
    for (int i = 0; i < IOS_INDEX_SLOTS; i++) ios_index_drop(&ios_index_dirs[i]);
    assert(handle_count == 0);
    printf("directory-index: 10000 warm misses, zero new scans; mutation/fault/eviction checks pass; max owned bytes=%zu\n", bytes);
}
