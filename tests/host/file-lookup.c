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
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
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
static int normalize_names;
static int read_fail_once, read_fail_remaining;
static const char *lookup_base = "root";
static struct {
    unsigned int full_hit, full_miss, parent_try, parent_hit, components;
    unsigned int component_miss, case_query, scans, entries;
    unsigned int prefix_hint, reparse_saved;
} profile_counts;
#define IOS_FILE_COUNT(n) (++profile_counts.n)

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
    /* This fixture models APFS canonical equivalence for stat/open only.
     * Directory enumeration retains the NFC spelling stored in its node. */
    if (normalize_names && !strcmp(trimmed, "root/normalized/e\xcc\x81link?"))
        strcpy(trimmed, "root/normalized/\xc3\xa9link?");
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
    if (read_fail_remaining)
    {
        read_fail_remaining = 0;
        errno = EIO;
        return NULL;
    }
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
    strcpy(buffer, lookup_base);
    stat_count = 0; reparse_pos = reparse_len = -1; root_seen = -1;
    read_fail_remaining = read_fail_once;
    memset(&profile_counts, 0, sizeof(profile_counts));
    result.status = fn(lookup_root, &attr, &name, offset, &buffer, size, strlen(lookup_base), disp, open_reparse, is_unix, 0);
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
        L"Games\\DD\\absent\\file", L"Games\\DD\\absent\\deep\\file",
        L"Games\\DD\\dlc\\assets\\Found.json\\file",
        L"Games\\DD\\dlc\\assets\\link", L"Games\\DD\\dlc\\folder\\file",
        L"Games\\DD\\LONG~001\\found.json", L"Games\\DD\\dlc\\assets\\LONG~001",
        L"Games\\DD\\dlc\\assets\\", L"Games\\DD\\absent\\", L"Games\\DD\\dlc\\\x00e9\\found.json",
        L"Games\\DD\\dlc\\\x00e9\\missing\\file", L"Games\\DD\\DLC\\assets\\missing\\file",
        L"Games\\DD\\LONG~001\\missing\\file", L"Games\\DD\\dlc\\folder\\deep\\file",
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
    /* No cached result: adding/removing a file is seen by the next lookup. */
    a = run(lookup_unix_name, paths[0], 0, FILE_OPEN, 0, 0, 1024);
    assert(a.status == STATUS_SUCCESS);
    assert(profile_counts.full_hit == 1 && !profile_counts.full_miss && !profile_counts.components);
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
    assert(profile_counts.full_miss == 1 && profile_counts.parent_try == 1 && profile_counts.parent_hit == 1);
    assert(profile_counts.components == 1 && profile_counts.component_miss == 1);
    assert(profile_counts.case_query == 1 && profile_counts.scans == 1 && profile_counts.entries == 1);
    b = run(lookup_unix_name, paths[2], 0, FILE_OPEN, 0, 0, 1024);
    assert(b.status == STATUS_OBJECT_NAME_NOT_FOUND && profile_counts.parent_hit == 1);
    if (profile_counts.components != 2 || profile_counts.scans != 1 || profile_counts.entries != 2)
    { puts("FAIL: normal/reparse miss scans the same directory twice"); return 1; }
    /* Device build 25: thousands of missing intermediate DLC paths, including
     * directory probes with a trailing slash. No full-parent shortcut hits. */
    const struct node dlc_tree[] = {
        {"root",1}, {"root/Program Files (x86)",1}, {"root/Program Files (x86)/Steam",1},
        {"root/Program Files (x86)/Steam/steamapps",1},
        {"root/Program Files (x86)/Steam/steamapps/common",1},
        {"root/Program Files (x86)/Steam/steamapps/common/DD",1},
        {"root/Program Files (x86)/Steam/steamapps/common/DD/dlc",1},
        {"root/Program Files (x86)/Steam/steamapps/common/DD/dlc/CC",1},
        {"root/Program Files (x86)/Steam/steamapps/common/DD/dlc/CC/features",1},
        {"root/Program Files (x86)/Steam/steamapps/common/DD/dlc/CC/features/districts",1},
        {"root/Program Files (x86)/Steam/steamapps/common/DD/dlc/CC/features/districts/modes",1},
        {"root/Program Files (x86)/Steam/steamapps/common/DD/dlc/CC/features/districts/modes/radiant",1}};
    const WCHAR *dlc_paths[] = {
        L"Program Files (x86)\\Steam\\steamapps\\common\\DD\\dlc\\CC\\features\\districts\\modes\\radiant\\monsters\\enemy\\enemy.info.darkest",
        L"Program Files (x86)\\Steam\\steamapps\\common\\DD\\dlc\\CC\\features\\districts\\modes\\radiant\\monsters\\enemy\\fx\\"};
    nodes = dlc_tree; node_count = sizeof(dlc_tree) / sizeof(dlc_tree[0]);
    /* An unrelated missing sibling must not pin all later probes to a shallow
     * ancestor, bypassing the deeper search on every subsequent request. */
    b = run(lookup_unix_name, L"Program Files (x86)\\Steam\\absent\\file", 0, FILE_OPEN, 0, 0, 1024);
    assert(b.status == STATUS_OBJECT_PATH_NOT_FOUND);
    for (unsigned int i = 0; i < sizeof(dlc_paths) / sizeof(dlc_paths[0]); i++)
    {
        a = run(reference_lookup_unix_name, dlc_paths[i], 0, FILE_OPEN, 0, 0, 1024);
        b = run(lookup_unix_name, dlc_paths[i], 0, FILE_OPEN, 0, 0, 1024);
        assert(a.status == STATUS_OBJECT_PATH_NOT_FOUND && a.status == b.status);
        printf("stat calls for missing DLC parent%s: reference=%d optimized=%d\n", i ? " (trailing separator)" : "", a.stats,b.stats);
        if (b.stats > 8) { puts("FAIL: missing-parent probes still walk existing ancestors"); return 1; }
        assert(profile_counts.components == 2 && profile_counts.scans == 1);
        b = run(lookup_unix_name, dlc_paths[i], 0, FILE_OPEN, 0, 0, 1024);
        printf("stat calls for repeated missing DLC parent: %d\n", b.stats);
        if (b.stats != 4) { puts("FAIL: repeated parent search remains"); return 1; }
        assert(profile_counts.prefix_hint == 1 && profile_counts.reparse_saved == 1);
    }
    /* Hints are starting points, never existence/absence results. Change the
     * parent itself, case spelling, and add a reparse file between lookups. */
    struct node mutable_tree[] = {{"root",1}, {"root/mutable",1},
        {"root/mutable/parent",1}, {"root/mutable/parent/NEW.json",0}};
    const WCHAR *mutable_path = L"mutable\\parent\\new.json";
    nodes = mutable_tree;
    for (int scenario = 0; scenario < 7; scenario++)
    {
        node_count = scenario == 0 ? 3 : scenario == 1 ? 2 : 4;
        mutable_tree[2] = (struct node){scenario == 3 ? "root/mutable/PARENT" : "root/mutable/parent", scenario == 2 ? 0 : 1};
        mutable_tree[3] = (struct node){scenario == 4 ? "root/mutable/parent/new.json?" : "root/mutable/parent/NEW.json",0};
        if (scenario == 2) node_count = 3; /* replace directory with a file */
        if (scenario == 3) mutable_tree[3].path = "root/mutable/PARENT/NEW.json";
        a = run(reference_lookup_unix_name, mutable_path, 0, FILE_OPEN, 0, 0, 1024);
        b = run(lookup_unix_name, mutable_path, 0, FILE_OPEN, 0, 0, 1024);
        assert(a.status == b.status && !strcmp(a.path,b.path) && a.pos == b.pos && a.length == b.length);
        checks++;
    }
    /* A partial directory read must retain the original second search. Both
     * an exact reparse spelling and its case-folded sibling must remain usable. */
    const struct node reparse_tree[] = {{"root",1}, {"root/reparse",1},
        {"root/reparse/link?",0}, {"root/reparse/LINK?",0}, {"root/reparse/LINK",0}};
    nodes = reparse_tree;
    for (node_count = 3; node_count <= 5; node_count++)
    for (int rp = 0; rp < 2; rp++)
    for (read_fail_once = 0; read_fail_once < 2; read_fail_once++)
    {
        a = run(reference_lookup_unix_name, L"reparse\\link", 0, FILE_OPEN, rp, 0, 1024);
        b = run(lookup_unix_name, L"reparse\\link", 0, FILE_OPEN, rp, 0, 1024);
        assert(a.status == b.status && !strcmp(a.path,b.path) && a.pos == b.pos && a.length == b.length);
        checks++;
    }
    read_fail_once = 0;
    /* Same resulting Unix prefix but a different root buffer/NT start offset. */
    nodes = tree; node_count = ARRAY_SIZE(tree);
    lookup_base = "root/Games";
    a = run(reference_lookup_unix_name, L"DD\\dlc\\assets\\link", 0, FILE_OPEN, 0, 0, 1024);
    b = run(lookup_unix_name, L"DD\\dlc\\assets\\link", 0, FILE_OPEN, 0, 0, 1024);
    assert(a.status == b.status && !strcmp(a.path,b.path) && a.pos == b.pos && a.length == b.length);
    lookup_base = "root";
    checks++;
    /* A previously missing child can become a deep tree. Relearn it after
     * the first successful child walk, instead of retaining a shallow hint. */
    const struct node growing_tree[] = {{"root",1}, {"root/growing",1},
        {"root/growing/a",1}, {"root/growing/a/b",1}, {"root/growing/a/b/c",1}};
    nodes = growing_tree; node_count = 2;
    b = run(lookup_unix_name, L"growing\\a\\b\\c\\missing", 0, FILE_OPEN, 0, 0, 1024);
    assert(b.status == STATUS_OBJECT_PATH_NOT_FOUND);
    node_count = ARRAY_SIZE(growing_tree);
    for (int attempt = 0; attempt < 3; attempt++)
    {
        a = run(reference_lookup_unix_name, L"growing\\a\\b\\c\\missing", 0, FILE_OPEN, 0, 0, 1024);
        b = run(lookup_unix_name, L"growing\\a\\b\\c\\missing", 0, FILE_OPEN, 0, 0, 1024);
        assert(a.status == b.status && !strcmp(a.path,b.path));
        if (attempt == 2) assert(b.stats == 4 && profile_counts.prefix_hint == 1);
        checks++;
    }
    /* More independent directory keys than the bounded hint table. Eviction
     * must change cost only, including when an old key is revisited. */
    char rotating_name[80];
    WCHAR rotating_path[80];
    struct node rotating_tree[] = {{"root",1}, {rotating_name,1}};
    nodes = rotating_tree; node_count = ARRAY_SIZE(rotating_tree);
    for (int index = 0; index <= 40; index++)
    {
        snprintf(rotating_name, sizeof(rotating_name), "root/rotation%02d", index % 40);
        swprintf(rotating_path, ARRAY_SIZE(rotating_path), L"rotation%02d\\missing", index % 40);
        a = run(reference_lookup_unix_name, rotating_path, 0, FILE_CREATE, 0, 0, 1024);
        b = run(lookup_unix_name, rotating_path, 0, FILE_CREATE, 0, 0, 1024);
        assert(a.status == b.status && !strcmp(a.path,b.path));
        checks++;
    }
    /* Long prefixes bypass fixed hint storage without truncation or a stale
     * byte/component offset. Each component is still below the Wine limit. */
    struct node long_tree[13];
    char long_names[13][1024];
    WCHAR long_path[1024];
    strcpy(long_names[0], "root");
    long_tree[0] = (struct node){long_names[0],1};
    for (int index = 1; index < 13; index++)
    {
        snprintf(long_names[index], sizeof(long_names[index]), "%s/abcdefghij0123456789abcdefghij0123456789abcdefghij%02d", long_names[index-1], index);
        long_tree[index] = (struct node){long_names[index],1};
    }
    size_t long_len = 0;
    for (const char *p = long_names[12] + 5; *p; p++) long_path[long_len++] = *p == '/' ? '\\' : *p;
    wcscpy(long_path + long_len, L"\\missing");
    nodes = long_tree; node_count = ARRAY_SIZE(long_tree);
    for (int attempt = 0; attempt < 2; attempt++)
    {
        a = run(reference_lookup_unix_name, long_path, 0, FILE_OPEN, 0, 0, 2048);
        b = run(lookup_unix_name, long_path, 0, FILE_OPEN, 0, 0, 2048);
        assert(a.status == b.status && !strcmp(a.path,b.path) && !profile_counts.prefix_hint);
        checks++;
    }
    /* Apple directory decoding yields NFC, but an NT request may use NFD.
     * A complete scan cannot replace the literal reparse stat on APFS. */
    const struct node normalized_tree[] = {{"root",1}, {"root/normalized",1},
        {"root/normalized/\xc3\xa9link?",0}};
    nodes = normalized_tree; node_count = ARRAY_SIZE(normalized_tree);
    normalize_names = 1;
    for (int rp = 0; rp < 2; rp++)
    {
        a = run(reference_lookup_unix_name, L"normalized\\e\x0301link", 0, FILE_OPEN, rp, 0, 1024);
        b = run(lookup_unix_name, L"normalized\\e\x0301link", 0, FILE_OPEN, rp, 0, 1024);
        assert(a.status == (rp ? STATUS_SUCCESS : STATUS_REPARSE_RESULT));
        assert(a.status == b.status && !strcmp(a.path,b.path) && a.pos == b.pos && a.length == b.length);
        checks++;
    }
    normalize_names = 0;
    /* Move the missing ancestor across every search boundary, then create
     * and remove it between calls. Vary depth, root offset, disposition,
     * trailing separator and buffer size; compare the actual resolver. */
    struct node depth_tree[34];
    char depth_names[34][256];
    WCHAR depth_path[256];
    strcpy(depth_names[0], "root");
    depth_tree[0] = (struct node){depth_names[0],1};
    nodes = depth_tree;
    for (unsigned int depth = 1; depth <= 32; depth++)
    {
        snprintf(depth_names[depth], sizeof(depth_names[depth]), "%s/d%02u", depth_names[depth-1],depth);
        depth_tree[depth] = (struct node){depth_names[depth],1};
        strcpy(depth_names[depth+1], depth_names[depth]);
        strcat(depth_names[depth+1], "/file");
        depth_tree[depth+1] = (struct node){depth_names[depth+1],0};
        for (unsigned int trailing = 0; trailing < 2; trailing++)
        {
            const char *target = depth_names[depth+1] + 5; /* exclude root/ */
            unsigned int len = 3;
            memcpy(depth_path, L"C:\\", 3 * sizeof(WCHAR));
            while (*target) { depth_path[len++] = *target == '/' ? '\\' : *target; target++; }
            if (trailing) depth_path[len++] = '\\';
            depth_path[len] = 0;
            for (unsigned int d = 0; d < 3; d++)
            for (int present = 1; present <= (int)depth + 2; present++)
            {
                UINT disp[] = {FILE_OPEN, FILE_CREATE, FILE_OVERWRITE};
                node_count = present;
                a = run(reference_lookup_unix_name, depth_path, 3, disp[d], 0, 0, 300);
                b = run(lookup_unix_name, depth_path, 3, disp[d], 0, 0, 300);
                assert(a.status == b.status && !strcmp(a.path,b.path) && a.pos == b.pos && a.length == b.length);
                checks++;
            }
        }
    }
    printf("PASS: %d differential fixtures, reparse offsets, buffer growth and mutation; mock filesystem only\n", checks);
    return 0;
}
