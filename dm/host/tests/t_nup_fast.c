// Host multi-threaded stress, benchmark, and Delta #27 fault-injection test for hle_fs.c / nup_image.h Wave 3.
#if !defined(__APPLE__)
#define _GNU_SOURCE
#endif
#include "../hle_fs.c"
#include "nup_fixture_zst.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct dm_flags g_flag;
uint64_t g_t0_ns;
uint64_t now_ns(void) { return 0; }
void dm_guest_write_prepare(void *a, size_t n) { (void)a; (void)n; }

static uint64_t nup_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static size_t build_fixture(uint8_t *o, int nz_count) {
    assert(nz_count == 4);
    uint8_t toc[4096] = {0};
    char pool[256];
    size_t pl = 0, no[7];
    const char *names[7] = {"", "romfs", "z0", "z1", "z2", "z3", "raw.txt"};
    for (int i = 0; i < 7; i++) { no[i] = pl; memcpy(pool + pl, names[i], strlen(names[i])); pl += strlen(names[i]); }
    size_t o_pk = 0x30, o_dir = o_pk + 0x40, o_file = o_dir + 2 * 0x18, o_kids = o_file + 5 * 0x50;
    size_t nkids = 6, o_pool = o_kids + 4 * nkids, toc_size = o_pool + pl, blob = (0x80 + toc_size + 15) & ~(size_t)15, at = blob;
    #define W32(b, off, v) do { uint32_t _v = (uint32_t)(v); memcpy((b) + (off), &_v, 4); } while (0)
    #define W64(b, off, v) do { uint64_t _v = (uint64_t)(v); memcpy((b) + (off), &_v, 8); } while (0)
    memcpy(toc, "TOC\x01", 4); W32(toc, 4, 1); W64(toc, 8, 2); W64(toc, 0x10, 5); W64(toc, 0x18, o_pool); W64(toc, 0x20, pl);
    uint8_t *pk = toc + o_pk;
    W64(pk, 0, 0x04003F40232DE000ull); W32(pk, 8, 0); W32(pk, 0xC, NUP_NONE); W32(pk, 0x10, 1); W32(pk, 0x14, 0); pk[0x18] = 0;
    uint8_t *d0 = toc + o_dir, *d1 = toc + o_dir + 0x18;
    W32(d0, 0, no[0]); W32(d0, 4, 0); W32(d0, 8, 0); W32(d0, 12, 1); W32(toc, o_kids, 1);
    W32(d1, 0, no[1]); W32(d1, 4, 5); W32(d1, 8, 1); W32(d1, 12, 5);
    /* Kids must be byte-sorted by name: raw.txt < z0 < z1 < z2 < z3 */
    W32(toc, o_kids + 4, 6);
    for (int i = 0; i < 4; i++) W32(toc, o_kids + 8 + 4 * i, (uint32_t)(2 + i));
    const char *raw = "RAW_OK";
    uint64_t xo = at; memcpy(o + at, nup_zst, nup_zst_len); at = (at + nup_zst_len + 15) & ~(size_t)15;
    for (int i = 0; i < 4; i++) {
        uint8_t *r = toc + o_file + 0x50 * i;
        W32(r, 0, no[2 + i]); W32(r, 4, 2); W64(r, 8, 102000); W64(r, 0x10, xo); W64(r, 0x18, nup_zst_len); r[0x40] = 1;
    }
    uint8_t *rr = toc + o_file + 0x50 * 4;
    W32(rr, 0, no[6]); W32(rr, 4, 7); W64(rr, 8, strlen(raw)); W64(rr, 0x10, at); W64(rr, 0x18, strlen(raw)); rr[0x40] = 0;
    memcpy(o + at, raw, strlen(raw)); at = (at + strlen(raw) + 15) & ~(size_t)15;
    memcpy(toc + o_pool, pool, pl);
    uint8_t *s = o; memset(s, 0, 0x80); memcpy(s, "NUP\x01", 4); s[4] = 0x80; s[6] = 1;
    W64(s, 8, 0x80); W64(s, 0x10, toc_size); plat_sha256(toc, toc_size, s + 0x18);
    W64(s, 0x38, blob); W64(s, 0x40, at); W32(s, 0x48, 1); memcpy(o + 0x80, toc, toc_size);
    #undef W32
    #undef W64
    return at;
}

typedef struct { uint64_t h; int tid, reads, fast; const char *expect; } worker_arg;
static void *reader_worker(void *p) {
    worker_arg *a = p;
    char buf[128];
    for (int i = 0; i < a->reads; i++) {
        int mi = -1; int64_t k = -1;
        assert(img_path("rom:/z0", &mi, &k) == 1 && k >= 0);
        size_t got = 0;
        assert(h_fs_ReadFile(&got, a->h, 0, buf, 34) == RES_OK && got == 34);
        if (i == 0 || i == a->reads - 1) assert(!memcmp(buf, a->expect, 34));
    }
    return NULL;
}

static int verify_lru_and_pin(int fault) {
    nup_aux *a = g_mnt[0].img->aux;
    assert(a->nz == 4);
    a->budget = 350000;   /* fits 3 x 102000 B entries (306000 B), evicts 1 when opening the 4th */
    uint64_t h0, h1, h2;
    assert(h_fs_OpenFile(&h0, "rom:/z0", MODE_READ) == RES_OK);
    assert(h_fs_OpenFile(&h1, "rom:/z1", MODE_READ) == RES_OK);
    h_fs_CloseFile(h0);
    assert(h_fs_OpenFile(&h2, "rom:/z2", MODE_READ) == RES_OK);
    h_fs_CloseFile(h1); h_fs_CloseFile(h2);
    /* Touch z0 via open/close so z1 becomes oldest in LRU ring; then open z3 to force trim of 1 entry. */
    assert(h_fs_OpenFile(&h0, "rom:/z0", MODE_READ) == RES_OK);
    h_fs_CloseFile(h0);
    if (fault == 2) {
        /* Delta #27 fault mode 2: break LRU order by pushing z1 behind z0 before trim. */
        pthread_mutex_lock(&a->lock);
        nup_lru_touch(a, 1);
        pthread_mutex_unlock(&a->lock);
    }
    uint64_t h3;
    assert(h_fs_OpenFile(&h3, "rom:/z3", MODE_READ) == RES_OK);
    h_fs_CloseFile(h3);
    if (a->z[1].buf != NULL || a->z[0].buf == NULL || a->z[2].buf == NULL || a->z[3].buf == NULL) {
        fprintf(stderr, "FAIL: LRU trim order wrong (z0=%p z1=%p z2=%p z3=%p fault=%d)\n",
                (void *)a->z[0].buf, (void *)a->z[1].buf, (void *)a->z[2].buf, (void *)a->z[3].buf, fault);
        return 1;
    }
    /* Pin-under-close safety test (DM_NUP_CACHE_MB=0 equivalent). */
    a->budget = 0;
    assert(h_fs_OpenFile(&h0, "rom:/z0", MODE_READ) == RES_OK);
    const uint8_t *pinned = nup_z_pin_read(g_mnt[0].img, 0, fault == 1 ? 0 : 1, NULL, 0);
    if (fault == 1) atomic_fetch_sub(&a->z[0].refs, 1u);   /* simulate missing read pin */
    h_fs_CloseFile(h0);
    if (a->z[0].buf == NULL || pinned == NULL) {
        fprintf(stderr, "FAIL: cached buffer freed while reader held pin (buf=%p fault=%d)\n", (void *)a->z[0].buf, fault);
        return 1;
    }
    nup_z_unpin_read(g_mnt[0].img, 0, 1);
    if (a->z[0].buf != NULL) {
        fprintf(stderr, "FAIL: unpinned zero-budget buffer was not trimmed after reader left\n");
        return 1;
    }
    a->budget = 128u << 20;
    return 0;
}

static double bench_threads(uint64_t h, int nth, int reads_per_thr, int fast, const char *expect) {
    setenv("DM_FS_FAST_IMG", fast ? "1" : "0", 1);
    fs_fast_img_sync();
    pthread_t *th = calloc((size_t)nth, sizeof *th);
    worker_arg *args = calloc((size_t)nth, sizeof *args);
    assert(th && args);
    uint64_t t0 = nup_now_ns();
    for (int i = 0; i < nth; i++) {
        args[i] = (worker_arg){h, i, reads_per_thr, fast, expect};
        assert(pthread_create(&th[i], NULL, reader_worker, &args[i]) == 0);
    }
    for (int i = 0; i < nth; i++) pthread_join(th[i], NULL);
    uint64_t dt = nup_now_ns() - t0;
    free(th); free(args);
    return dt / 1e6;
}

int main(void) {
    const char *fe = getenv("T_NUP_FAST_FAULT");
    int fault = fe ? atoi(fe) : 0;
    const char *td = getenv("TMPDIR");
    char np[1024]; snprintf(np, sizeof np, "%s/t_nup_fast_%ld.nup", td && *td ? td : "/tmp", (long)getpid());
    static uint8_t img_bytes[16384];
    size_t n = build_fixture(img_bytes, 4);
    FILE *f = fopen(np, "wb"); assert(f && fwrite(img_bytes, 1, n, f) == n && !fclose(f));
    unsetenv("DM_ROMFS_OVERLAY");
    setenv("DM_ROMFS", np, 1);
    setenv("DM_FS_FAST_IMG", "1", 1);
    assert(h_fs_MountRom("rom", NULL, 0) == RES_OK);
    if (verify_lru_and_pin(fault) != 0) { unlink(np); return 1; }
    uint64_t h = 0;
    assert(h_fs_OpenFile(&h, "rom:/z0", MODE_READ) == RES_OK);
    const char *expect = "line 00000 of the compressed file\n";
    enum { NTH = 4, READS = 100000 };
    double base_ms = bench_threads(h, NTH, READS, 0, expect);
    double fast_ms = bench_threads(h, NTH, READS, 1, expect);
    h_fs_CloseFile(h);
    unlink(np);
    printf("nup_fast_bench: threads=%d ops=%d (img_path+ReadFile)\n", NTH, NTH * READS);
    printf("  DM_FS_FAST_IMG=0: %.2f ms (%.1f ns/op)\n", base_ms, base_ms * 1e6 / (NTH * READS));
    printf("  DM_FS_FAST_IMG=1: %.2f ms (%.1f ns/op) speedup=%.2fx\n", fast_ms, fast_ms * 1e6 / (NTH * READS), base_ms / (fast_ms > 0 ? fast_ms : 0.001));
    printf("t_nup_fast PASS (fault=%d)\n", fault);
    return 0;
}
