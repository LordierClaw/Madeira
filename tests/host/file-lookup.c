/* Production Wine lookup functions over a small, mutable filesystem model. */
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

typedef wchar_t WCHAR;
typedef unsigned int UINT, NTSTATUS;
typedef int BOOL, BOOLEAN;
typedef struct { WCHAR *Buffer; unsigned int Length, MaximumLength; } UNICODE_STRING;
typedef struct { UNICODE_STRING *ObjectName; } OBJECT_ATTRIBUTES;
#define TRUE 1
#define FALSE 0
#define MAX_DIR_ENTRY_LEN 255
#define INVALID_NT_CHARS '*', '?', '<', '>', '|', '"'
#define FILE_OPEN 1
#define FILE_CREATE 2
#define FILE_OVERWRITE 4
#define STATUS_SUCCESS 0
#define STATUS_OBJECT_NAME_INVALID 1
#define STATUS_OBJECT_NAME_COLLISION 2
#define STATUS_OBJECT_PATH_NOT_FOUND 3
#define STATUS_OBJECT_NAME_NOT_FOUND 4
#define STATUS_NO_MEMORY 5
#define STATUS_NO_SUCH_FILE 6
#define STATUS_REPARSE_RESULT 7
#define O_RDONLY 0
#define S_ISDIR(mode) ((mode) == 1)
struct stat { int st_mode; };
struct dirent { char d_name[512]; };
typedef struct { int node, next; struct dirent entry; } DIR;
struct node { const char *path; int directory; };
static const struct node *nodes;
static int node_count, stat_count, sensitive, convert_fail, reparse_pos, reparse_len;
static DIR directory;
static int root_seen;
static int lookup_root = 73;
static int alternate_root;

static int equal_name(const char *a, const char *b)
{
    while (*a && *b)
    {
        if (sensitive ? *a != *b : tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++; b++;
    }
    return !*a && !*b;
}

static int node_index(const char *path)
{
    char trimmed[2048];
    size_t n = strlen(path);
    assert(n < sizeof(trimmed));
    strcpy(trimmed, path);
    while (n > 1 && trimmed[n - 1] == '/') trimmed[--n] = 0;
    for (int i = 0; i < node_count; i++) if (equal_name(nodes[i].path, trimmed)) return i;
    return -1;
}

static int fstatat(int root, const char *path, struct stat *st, int flags)
{
    int i = root == 73 ? node_index(path) : -1;
    (void)flags;
    root_seen = root;
    stat_count++;
    if (root == 74 && alternate_root && !strcmp(path, "root")) i = 0;
    if (i < 0) return -1;
    st->st_mode = nodes[i].directory;
    return 0;
}

static int openat(int root, const char *path, int flags)
{
    (void)flags;
    root_seen = root;
    int i = root == 73 ? node_index(path) : -1;
    return i < 0 ? -1 : i + 10;
}
static int close(int fd) { (void)fd; return 0; }
static DIR *fdopendir(int fd)
{
    if (!nodes[fd - 10].directory) return NULL;
    directory.node = fd - 10; directory.next = 0;
    return &directory;
}
static struct dirent *readdir(DIR *d)
{
    size_t n = strlen(nodes[d->node].path);
    for (; d->next < node_count; d->next++)
    {
        const char *p = nodes[d->next].path;
        if (!strncmp(p, nodes[d->node].path, n) && p[n] == '/' && !strchr(p + n + 1, '/'))
        {
            strcpy(d->entry.d_name, p + n + 1); d->next++;
            return &d->entry;
        }
    }
    return NULL;
}
static void closedir(DIR *d) { (void)d; }
static int get_dir_case_sensitivity(int root, const char *path)
{ (void)root; (void)path; return sensitive; }
static NTSTATUS errno_to_status(int error) { (void)error; return STATUS_OBJECT_PATH_NOT_FOUND; }

static int ntdll_wcstoumbs(const WCHAR *s, int length, char *out, int size, int strict)
{
    int n = 0;
    (void)strict;
    if (convert_fail && length > 10) return -1;
    for (int i = 0; i < length; i++)
    {
        unsigned int c = s[i];
        if (n + (c < 128 ? 1 : 2) > size) return -1;
        if (c < 128) out[n++] = (char)c;
        else { out[n++] = (char)(0xc0 | (c >> 6)); out[n++] = (char)(0x80 | (c & 63)); }
    }
    return n;
}
static int ntdll_umbstowcs(const char *s, int length, WCHAR *out, int size)
{
    int n = 0;
    for (int i = 0; i < length && n < size; i++)
    {
        unsigned int c = (unsigned char)s[i];
        if (c >= 0xc0 && i + 1 < length) c = ((c & 31) << 6) | ((unsigned char)s[++i] & 63);
        out[n++] = c;
    }
    return n;
}
#define wcsnicmp mock_wcsnicmp
static int mock_wcsnicmp(const WCHAR *a, const WCHAR *b, int n)
{
    for (int i = 0; i < n; i++)
    {
        WCHAR x = a[i], y = b[i];
        if (x >= L'A' && x <= L'Z') x += 32;
        if (y >= L'A' && y <= L'Z') y += 32;
        if (x != y) return 1;
    }
    return 0;
}
static int is_legal_8dot3_name(const WCHAR *s, int n)
{ for (int i = 0; i < n; i++) if (s[i] == '~') return 1; return 0; }
static int hash_short_file_name(const WCHAR *name, int n, WCHAR *out)
{ (void)name; (void)n; memcpy(out, L"LONG~001", 8 * sizeof(WCHAR)); return 8; }

static NTSTATUS resolve_reparse_point(int fd, int root, OBJECT_ATTRIBUTES *attr,
    UNICODE_STRING *nt, unsigned int pos, unsigned int length, char **buffer,
    int size, int unix_pos, UINT disp, BOOL open_reparse, BOOL is_unix, unsigned int count)
{
    (void)fd; (void)root; (void)attr; (void)nt; (void)buffer; (void)size;
    (void)unix_pos; (void)disp; (void)open_reparse; (void)is_unix; (void)count;
    reparse_pos = pos; reparse_len = length;
    return STATUS_REPARSE_RESULT;
}

#include "lookup.inc"

typedef NTSTATUS (*lookup_fn)(int, OBJECT_ATTRIBUTES *, UNICODE_STRING *, unsigned int,
    char **, int, int, UINT, BOOL, BOOL, unsigned int);
struct result { NTSTATUS status; int stats, pos, length, root; char path[2048]; };
static struct result run(lookup_fn fn, const WCHAR *path, unsigned int offset,
                        UINT disp, BOOL open_reparse, BOOL is_unix, int size)
{
    UNICODE_STRING name = {(WCHAR *)path, (unsigned int)(wcslen(path) * sizeof(WCHAR)), 0};
    OBJECT_ATTRIBUTES attr = {&name};
    char *buffer = malloc(size);
    struct result result;
    assert(buffer && size > 5);
    strcpy(buffer, "root");
    stat_count = 0; reparse_pos = reparse_len = -1; root_seen = -1;
    result.status = fn(lookup_root, &attr, &name, offset, &buffer, size, 4, disp, open_reparse, is_unix, 0);
    result.stats = stat_count; result.pos = reparse_pos; result.length = reparse_len; result.root = root_seen;
    result.path[0] = 0;
    if (result.status == STATUS_SUCCESS || result.status == STATUS_NO_SUCH_FILE || result.status == STATUS_REPARSE_RESULT)
        strcpy(result.path, buffer);
    free(buffer);
    return result;
}

int main(void)
{
    const struct node tree[] = {{"root",1}, {"root/Games",1}, {"root/Games/DD",1},
        {"root/Games/DD/dlc",1}, {"root/Games/DD/dlc/assets",1},
        {"root/Games/DD/dlc/assets/Found.json",0}, {"root/Games/DD/dlc/assets/link?",0},
        {"root/Games/DD/dlc/folder?",0}, {"root/Games/DD/LongDirectoryName",1},
        {"root/Games/DD/LongDirectoryName/Found.json",0}, {"root/Games/DD/dlc/\xc3\xa9",1},
        {"root/Games/DD/dlc/\xc3\xa9/Found.json",0}, {"root/Games/DD/dlc/\xc3\xa9/link?",0}};
    const WCHAR *paths[] = {L"Games\\DD\\dlc\\assets\\Found.json", L"Games\\DD\\dlc\\assets\\found.json",
        L"Games\\DD\\dlc\\assets\\missing.json", L"Games\\DD\\DLC\\assets\\found.json",
        L"Games\\DD\\absent\\file", L"Games\\DD\\dlc\\assets\\Found.json\\file",
        L"Games\\DD\\dlc\\assets\\link", L"Games\\DD\\dlc\\folder\\file",
        L"Games\\DD\\LONG~001\\found.json", L"Games\\DD\\dlc\\assets\\LONG~001",
        L"Games\\DD\\dlc\\assets\\", L"Games\\DD\\absent\\", L"Games\\DD\\dlc\\\x00e9\\found.json",
        L"missing", L"", L"Games\\DD\\.\\file", L"Games\\DD\\..\\file",
        L"Games\\DD\\\\file", L"Games\\DD\\bad*file", L"Games/DD/dlc/assets/missing.json"};
    nodes = tree; node_count = sizeof(tree) / sizeof(tree[0]);
    int checks = 0;
    for (sensitive = 0; sensitive < 2; sensitive++)
    for (convert_fail = 0; convert_fail < 2; convert_fail++)
    for (unsigned int d = 0; d < 3; d++)
    for (int rp = 0; rp < 2; rp++)
    for (int unix_path = 0; unix_path < 2; unix_path++)
    for (unsigned int p = 0; p < sizeof(paths) / sizeof(paths[0]); p++)
    {
        UINT disp[] = {FILE_OPEN, FILE_CREATE, FILE_OVERWRITE};
        struct result a = run(reference_lookup_unix_name, paths[p], 0, disp[d], rp, unix_path, 1024);
        struct result b = run(lookup_unix_name, paths[p], 0, disp[d], rp, unix_path, 1024);
        if (a.status != b.status || strcmp(a.path, b.path) || a.pos != b.pos || a.length != b.length || a.root != b.root)
        {
            printf("FAIL equivalence p=%u sensitive=%d conversion=%d disp=%u reparse=%d unix=%d: %u/%u %s/%s offsets %d/%d\n",
                   p,sensitive,convert_fail,disp[d],rp,unix_path,a.status,b.status,a.path,b.path,a.pos,b.pos);
            return 1;
        }
        checks++;
    }
    sensitive = 1; convert_fail = 0;
    /* UTF-16 offsets and reallocation must still describe the original NT path. */
    const WCHAR *prefixed = L"C:\\Games\\DD\\dlc\\assets\\link";
    struct result a = run(reference_lookup_unix_name, prefixed, 3, FILE_OPEN, 0, 0, 96);
    struct result b = run(lookup_unix_name, prefixed, 3, FILE_OPEN, 0, 0, 96);
    assert(a.status == STATUS_REPARSE_RESULT && a.status == b.status && a.pos == b.pos && a.length == b.length);
    assert(!strcmp(a.path,b.path));
    prefixed = L"C:\\Games\\DD\\dlc\\\x00e9\\link";
    a = run(reference_lookup_unix_name, prefixed, 3, FILE_OPEN, 0, 0, 96);
    b = run(lookup_unix_name, prefixed, 3, FILE_OPEN, 0, 0, 96);
    assert(a.status == STATUS_REPARSE_RESULT && a.status == b.status && a.pos == b.pos && a.length == b.length);
    assert(a.pos == 18 && !strcmp(a.path,b.path));
    /* An identically spelled path under a different directory handle is absent. */
    lookup_root = 74; alternate_root = 1;
    a = run(reference_lookup_unix_name, paths[1], 0, FILE_OPEN, 0, 0, 1024);
    b = run(lookup_unix_name, paths[1], 0, FILE_OPEN, 0, 0, 1024);
    assert(a.status == b.status && b.status != STATUS_SUCCESS && a.root == 74 && b.root == 74);
    lookup_root = 73;
    /* No persistent cache: adding/removing a file is seen by the next lookup. */
    a = run(lookup_unix_name, paths[0], 0, FILE_OPEN, 0, 0, 1024);
    assert(a.status == STATUS_SUCCESS);
    node_count = 5;
    b = run(lookup_unix_name, paths[0], 0, FILE_OPEN, 0, 0, 1024);
    assert(b.status == STATUS_OBJECT_NAME_NOT_FOUND);
    node_count = sizeof(tree) / sizeof(tree[0]);
    b = run(lookup_unix_name, paths[0], 0, FILE_OPEN, 0, 0, 1024);
    assert(b.status == STATUS_SUCCESS);
    /* Deep, exact parent + mismatched leaf is the redundant-walk reproducer. */
    a = run(reference_lookup_unix_name, paths[1], 0, FILE_OPEN, 0, 0, 1024);
    b = run(lookup_unix_name, paths[1], 0, FILE_OPEN, 0, 0, 1024);
    assert(a.status == STATUS_SUCCESS && b.status == STATUS_SUCCESS);
    printf("stat calls for deep parent + case-mismatched leaf: reference=%d optimized=%d\n",a.stats,b.stats);
    if (b.stats >= a.stats || b.stats != 3) { puts("FAIL: redundant parent-component stats remain"); return 1; }
    printf("PASS: %d differential fixtures, reparse offsets, buffer growth and mutation; mock filesystem only\n", checks);
    return 0;
}
