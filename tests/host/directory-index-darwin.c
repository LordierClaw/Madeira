/* Real vnode notifications and extracted find_file_in_dir, under ASan/UBSan. */
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>
#include <locale.h>

#define WINE_IOS 1
typedef wchar_t WCHAR;
typedef int BOOL, BOOLEAN, NTSTATUS;
#define TRUE 1
#define FALSE 0
#define MAX_DIR_ENTRY_LEN 255
#define STATUS_SUCCESS 0
#define STATUS_OBJECT_NAME_NOT_FOUND 1
#define IOS_FILE_COUNT(n) (++counts.n)
static struct { int scans, entries, index_hit, index_build, index_stale, index_fallback;
    int case_query, components, component_miss, reparse_saved; } counts;
#define wcsnicmp wcsncasecmp
static int ntdll_umbstowcs(const char *s, int n, WCHAR *out, int max)
{ char copy[256]; if (n > 255) return -1; memcpy(copy, s, n); copy[n] = 0; return mbstowcs(out, copy, max); }
static int ntdll_wcstoumbs(const WCHAR *s, int n, char *out, int max, int strict)
{ WCHAR copy[256]; (void)strict; if (n > 255) return -1; memcpy(copy, s, n * sizeof(WCHAR)); copy[n] = 0; return wcstombs(out, copy, max); }
static int get_dir_case_sensitivity(int root, const char *path) { (void)root; (void)path; return 1; }
static int is_legal_8dot3_name(const WCHAR *s, int n) { (void)s; (void)n; return 0; }
static int hash_short_file_name(const WCHAR *s, int n, WCHAR *out) { (void)s; (void)n; (void)out; return 0; }
static NTSTATUS errno_to_status(int e) { (void)e; return STATUS_OBJECT_NAME_NOT_FOUND; }
#include "ios_directory_index.h"
#include "find.inc"

static void write_name(const char *path) { int fd = open(path, O_CREAT | O_WRONLY, 0600); assert(fd >= 0); close(fd); }
static int probe(int root, const char *parent, const WCHAR *name, char *out)
{ struct file_lookup_reparse rp = {0}; strcpy(out, parent); return find_file_in_dir(root, out, strlen(out), name, wcslen(name), FALSE, &rp, FALSE); }

static void *writer(void *unused)
{
    (void)unused;
    for (int i = 0; i < 1000; i++) { write_name("dlc/racing"); unlink("dlc/racing"); }
    return NULL;
}
int main(void)
{
    assert(setlocale(LC_ALL, "en_US.UTF-8"));
    char directory[] = "/tmp/madeira-index-XXXXXX", out[1024];
    assert(mkdtemp(directory) && !chdir(directory));
    assert(!mkdir("dlc", 0700));
    write_name("dlc/Asset.JSON");
    assert(probe(AT_FDCWD, "dlc", L"missing", out) == STATUS_OBJECT_NAME_NOT_FOUND);
    int scans = counts.scans;
    for (int i = 0; i < 5000; i++) assert(probe(AT_FDCWD, "dlc", L"missing", out) == STATUS_OBJECT_NAME_NOT_FOUND);
    assert(counts.scans == scans && counts.index_hit >= 5000);
    struct stat before; assert(!stat("dlc", &before));
    write_name("dlc/Mod.JSON");
    struct timespec times[2] = {before.st_atimespec, before.st_mtimespec};
    assert(!utimensat(AT_FDCWD, "dlc", times, 0));
    assert(!probe(AT_FDCWD, "dlc", L"mod.json", out));
    assert(!unlink("dlc/Mod.JSON"));
    assert(probe(AT_FDCWD, "dlc", L"mod.json", out) == STATUS_OBJECT_NAME_NOT_FOUND);
    /* Retained descriptor points to the old directory, path points to new. */
    assert(!rename("dlc", "old") && !mkdir("dlc", 0700));
    write_name("dlc/New.JSON");
    assert(probe(AT_FDCWD, "dlc", L"asset.json", out) == STATUS_OBJECT_NAME_NOT_FOUND);
    assert(!probe(AT_FDCWD, "dlc", L"new.json", out));
    assert(!mkdir("other", 0700));
    int root = open(".", O_RDONLY | O_DIRECTORY);
    assert(!probe(root, "dlc", L"new.json", out));
    close(root); root = open("other", O_RDONLY | O_DIRECTORY);
    assert(probe(root, "dlc", L"new.json", out) == STATUS_OBJECT_NAME_NOT_FOUND);
    close(root);
    /* APFS/HFS normalization: cache miss must retain the literal native stat. */
    write_name("dlc/\xc3\xa9link?");
    assert(!probe(AT_FDCWD, "dlc", L"e\x0301link?", out));
    pthread_t thread; assert(!pthread_create(&thread, NULL, writer, NULL));
    for (int i = 0; i < 1000; i++) probe(AT_FDCWD, "dlc", L"racing", out);
    assert(!pthread_join(thread, NULL));
    assert(probe(AT_FDCWD, "dlc", L"racing", out) == STATUS_OBJECT_NAME_NOT_FOUND);
    for (int i = 0; i < IOS_INDEX_SLOTS; i++) ios_index_drop(&ios_index_dirs[i]);
    close(ios_index_queue);
    unlink("dlc/New.JSON"); unlink("dlc/\xc3\xa9link?"); unlink("old/Asset.JSON");
    rmdir("dlc"); rmdir("old"); rmdir("other"); chdir("/"); rmdir(directory);
    puts("Darwin directory index: warm scans, create/delete, restored mtime, rename, root reuse, normalization and concurrent writes pass");
}
