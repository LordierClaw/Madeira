#!/usr/bin/env python3
"""Exercise production audio submissions and summaries without Core Audio/Wine.

Detects silent flags being confused with client zeros, short-lived stream
statistics being lost, nonfinite samples poisoning levels, or diagnostics
changing submitted bytes. Compile with --cc when the host has no cc.
"""
from pathlib import Path
import argparse
import os
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
args = parser.parse_args()
src = (Path(__file__).resolve().parents[2] / "build/ntdll-unix/audio_null_ios.c").read_text()

def cut(start, end):
    a = src.index(start)
    return src[a:src.index(end, a)]

helpers = cut("/* Source diagnostics:", "static NTSTATUS ios_release_stream(") if "/* Source diagnostics:" in src else ""
harness = r"""
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdatomic.h>
#include <float.h>
#include <math.h>
#include <time.h>
typedef unsigned char BYTE;
typedef uint32_t UINT32, UINT, DWORD;
typedef int NTSTATUS, HRESULT;
typedef void *HANDLE;
typedef uint64_t stream_handle;
#define S_OK 0
#define STATUS_SUCCESS 0
#define FALSE 0
#define AUDCLNT_E_NOT_INITIALIZED ((HRESULT)0x88890001)
#define IOS_MAX_STREAMS 16
static time_t clock_now = 100;
static time_t test_time(time_t *out) { if (out) *out = clock_now; return clock_now; }
#define time test_time
""" + cut("struct release_render_buffer_params {", "struct get_capture_buffer_params {") + cut("struct release_stream_params {", "struct get_render_buffer_params {") + cut("struct ios_stream {", "/* ml739: one stream object per client") + r"""
static _Atomic(struct ios_stream *) g_mix[IOS_MAX_STREAMS];
static struct ios_stream *stream_from_handle(stream_handle h) { return (void *)(uintptr_t)h; }
static int ios_stream_is_live(const struct ios_stream *s) { return s->mixable; }
static void ios_dev_detach(struct ios_stream *s) { atomic_store(&g_mix[0], NULL); (void)s; }
static void ios_dev_stop_if_idle(void) {}
static void stream_unregister(struct ios_stream *s) { (void)s; }
static void ios_audio_free_scratch(BYTE *p, int guest) { (void)guest; free(p); }
static void NtWaitForSingleObject(HANDLE h, int alert, void *timeout) { (void)h; (void)alert; (void)timeout; }
static void NtClose(HANDLE h) { (void)h; }
""" + helpers + cut("static NTSTATUS ios_release_stream(", "static NTSTATUS ios_start(") + cut("static NTSTATUS ios_release_render_buffer(", "static NTSTATUS ios_get_capture_buffer(") + r"""
static struct ios_stream *make_stream(int bits, int is_float)
{
    struct ios_stream *s = calloc(1, sizeof(*s));
    s->valid = s->started = s->mixable = 1;
    s->sample_rate = 48000; s->channels = 2; s->sample_bits = bits; s->is_float = is_float;
    s->frame_bytes = bits / 4; s->buffer_frames = s->scratch_frames = 8;
    s->ring = calloc(8, s->frame_bytes); s->render_scratch = calloc(8, s->frame_bytes);
    atomic_store(&g_mix[0], s);
    return s;
}
static void submit(struct ios_stream *s, const void *bytes, UINT32 n, UINT flags)
{
    struct release_render_buffer_params p = {(uintptr_t)s, n, flags, 0};
    BYTE before[64];
    uint64_t wr = atomic_load(&s->write_pos);
    memcpy(s->render_scratch, bytes, (size_t)n * s->frame_bytes);
    memcpy(before, bytes, (size_t)n * s->frame_bytes);
    s->pending_frames = n;
    if (ios_release_render_buffer(&p) || p.result || atomic_load(&s->write_pos) != wr + n) exit(10);
    for (UINT32 f = 0; f < n; f++) {
        BYTE *actual = s->ring + ((wr + f) % 8) * s->frame_bytes;
        BYTE zero[8] = {0};
        if (memcmp(actual, flags & 2 ? zero : before + f * s->frame_bytes, s->frame_bytes)) exit(11);
    }
}
static void finish(struct ios_stream *s)
{
    struct release_stream_params p = {(uintptr_t)s, NULL, 0};
    if (ios_release_stream(&p) || p.result) exit(12);
}
int main(void)
{
    struct ios_stream *s = make_stream(32, 1);
    float flagged[4] = {1, -1, 0.5f, -0.5f}, zero[4] = {0, -0.0f, 0, 0};
    uint32_t tiny[2] = {1, 0x80000000u}; /* smallest float subnormal and negative zero */
    float bad[2] = {NAN, INFINITY};
    submit(s, flagged, 2, 2); submit(s, zero, 2, 0); submit(s, tiny, 1, 0);
    clock_now += 11; submit(s, bad, 1, 0); finish(s);
    s = make_stream(16, 0);
    { int16_t pcm[4] = {0, 1, -32768, 0}; submit(s, pcm, 2, 0); }
    finish(s);
    s = make_stream(32, 0);
    { int32_t pcm[2] = {0, -2147483647}; submit(s, pcm, 1, 0); }
    finish(s);
    puts("bytes preserved");
    return 0;
}
"""
with tempfile.TemporaryDirectory() as temp:
    c = Path(temp) / "audio-source.c"
    exe = Path(temp) / ("audio-source.exe" if os.name == "nt" else "audio-source")
    c.write_text(harness)
    subprocess.run([args.cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", str(c), "-o", str(exe)], check=True)
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    assert run.returncode == 0 and run.stdout.strip() == "bytes preserved", (run.returncode, run.stdout, run.stderr)
    release = [line for line in run.stderr.splitlines() if "reason=release" in line]
    assert len(release) == 3, f"Missing short-stream release summaries: {run.stderr}"
    expected = [(6, 2, 4, 5, 1, 2), (2, 0, 2, 2, 2, 0), (1, 0, 1, 1, 1, 0)]
    for line, values in zip(release, expected):
        fields = tuple(int(re.search(rf"\b{key}=(\d+)", line)[1]) for key in
                       ("frames", "silent-flagged", "unflagged", "zero", "nonzero", "nonfinite"))
        assert fields == values, (fields, values, line)
        assert "mean|x|=nan" not in line.lower() and "peak=inf" not in line.lower(), line
    periodic = [line for line in run.stderr.splitlines() if "reason=periodic" in line]
    assert any("frames=4 silent-flagged=0 unflagged=4" in line and "zero=5 nonzero=1 nonfinite=2" in line for line in periodic), run.stderr
print("PASS: silent flags, exact zeros, tiny signal and nonfinite float samples distinguished; PCM16/32 counted; short streams and interval resets reported; ring bytes preserved")
