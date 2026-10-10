#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

typedef uint16_t WCHAR;
static uint64_t now_ns;
static unsigned int lines;
static char output[16384];
static void emit(const char *format, ...)
{
    va_list args;
    size_t n = strlen(output);
    va_start(args, format);
    vsnprintf(output + n, sizeof(output) - n, format, args);
    va_end(args);
    lines++;
    errno = EIO; /* Logging must not change the file operation's errno. */
}
#define IOS_FILE_PROFILE_NOW() (now_ns)
#define IOS_FILE_PROFILE_EMIT emit
#define IOS_FILE_PROFILE_TID() 0xe8u
#include "ios_file_profile.h"

static WCHAR path[400];
static unsigned int name(const char *s)
{
    unsigned int n = 0;
    while (s[n]) { path[n] = (unsigned char)s[n]; n++; }
    return n;
}

int main(void)
{
    struct ios_file_profile_call call = {0}, nested = {0};
    unsigned int len = name("C:\\game\\audio\\Master.BANK");
    ios_file_profile_enabled = 0;
    ios_file_profile_begin(&call, IOS_FILE_CREATE);
    IOS_FILE_COUNT(full_miss);
    ios_file_profile_lookup_done(&call);
    ios_file_profile_end(&call, path, len, 1, 0);
    assert(!ios_file_profile_active && !lines && !ios_file_profile_stats.calls);

    ios_file_profile_enabled = 1;
    now_ns = 1000000000;
    errno = ENOENT;
    ios_file_profile_begin(&call, IOS_FILE_CREATE);
    IOS_FILE_COUNT(full_miss);
    IOS_FILE_COUNT(parent_try);
    IOS_FILE_COUNT(parent_hit);
    IOS_FILE_COUNT(components);
    ios_file_profile_begin(&nested, IOS_FILE_CREATE);
    ios_file_profile_end(&nested, path, len, 1, 0);
    assert(ios_file_profile_active == &call); /* nested call cannot detach owner */
    now_ns += 2000000;
    ios_file_profile_lookup_done(&call);
    now_ns += 1000000;
    ios_file_profile_end(&call, path, len, 1, 0);
    assert(errno == ENOENT && !ios_file_profile_active && !lines);
    assert(ios_file_profile_stats.calls == 1);
    assert(ios_file_profile_stats.lookup_ns == 2000000);
    assert(ios_file_profile_stats.total_ns == 3000000);
    assert(ios_file_profile_stats.audio_ok == 1);

    len = name("C:\\game\\dlc\\missing.fsb");
    now_ns = 6000000000;
    ios_file_profile_begin(&call, IOS_FILE_CREATE);
    IOS_FILE_COUNT(full_miss);
    IOS_FILE_COUNT(parent_try);
    now_ns += 4000000;
    ios_file_profile_lookup_done(&call);
    now_ns += 1000000;
    ios_file_profile_end(&call, path, len, 1, 0xc0000034u);
    assert(lines == 4); /* one summary + worst/failure/audio, independent of count */
    assert(strstr(output, "calls=2 ok=1 fail=1"));
    assert(strstr(output, "lookup_us=6000 total_us=8000"));
    assert(strstr(output, "parent_try=2 parent_hit=1"));
    assert(strstr(output, "audio_ok=1 audio_fail=1"));
    assert(strstr(output, "status=c0000034"));
    assert(strstr(output, "missing.fsb"));
    assert(!ios_file_profile_stats.calls && errno == ENOENT);

    /* A busy burst cannot produce another report inside five seconds. */
    for (unsigned int i = 0; i < 10000; i++)
    {
        ios_file_profile_begin(&call, IOS_FILE_CREATE);
        now_ns += 1000;
        ios_file_profile_lookup_done(&call);
        ios_file_profile_end(&call, path, len, 1, 0);
    }
    assert(lines == 4 && ios_file_profile_stats.calls == 10000);
    assert(ios_file_profile_stats.audio_ok == 10000);

    /* A successful bank metadata query is not a successful bank open. */
    ios_file_profile_begin(&call, IOS_FILE_QUERY);
    ios_file_profile_lookup_done(&call);
    ios_file_profile_end(&call, path, len, 1, 0);
    ios_file_profile_begin(&call, IOS_FILE_QUERY_FULL);
    ios_file_profile_lookup_done(&call);
    ios_file_profile_end(&call, path, len, 1, 0);
    assert(ios_file_profile_stats.operations[IOS_FILE_CREATE] == 10000);
    assert(ios_file_profile_stats.operations[IOS_FILE_QUERY] == 1);
    assert(ios_file_profile_stats.operations[IOS_FILE_QUERY_FULL] == 1);
    assert(ios_file_profile_stats.audio_ok == 10000);

    /* Bounded names keep the suffix, cannot inject a log line, and tolerate UTF-16. */
    for (unsigned int i = 0; i < 399; i++) path[i] = 'x';
    path[394] = '\n'; path[395] = 0x00e9; path[396] = '.'; path[397] = 'x'; path[398] = 'y';
    now_ns += 5000000000;
    ios_file_profile_begin(&call, IOS_FILE_CREATE);
    now_ns += 20000000;
    ios_file_profile_lookup_done(&call);
    ios_file_profile_end(&call, path, 399, 1, 0);
    assert(lines == 7); /* no failed sample in the new interval */
    assert(strstr(output, "..."));
    assert(strstr(output, "??.xy"));
    assert(strstr(output, "create=10001 query=1 query_full=1"));
    assert(errno == ENOENT);
    puts("file profile: disabled/nested/timing/rate-limit/audio/status/path/errno checks passed");
    return 0;
}
