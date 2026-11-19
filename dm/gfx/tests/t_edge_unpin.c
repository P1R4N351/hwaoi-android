// Host unit, benchmark, and fault-injection test for shared-page edge unpinning (vka_edge_unpin.h + nvn_dirty.c).
// Exercises real write-protection faults via crash.c + nvn_dirty.c on mmap pages shared between KTGL-style small/medium
// textures and per-frame CPU neighbor writes. Supports Delta #27 fault injection with T_EDGE_UNPIN_FAULT=1.
#include "../nvn/nvn_dirty.c"
#include "../vk_android/vka_edge_unpin.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

module_t *g_mod;
int g_nmod;
module_t *module_of(const void *p) { (void)p; return NULL; }
void crash_install(void);

static int g_fail;
static int g_fault_mode;

#define EXPECT_EQ(msg, want, got) do {     long long w_ = (long long)(want), g_ = (long long)(got);     if (w_ != g_) { printf("FAIL %-58s expected=%lld got=%lld\n", (msg), w_, g_); g_fail++; } } while (0)

typedef struct {
    const uint8_t *mem;
    size_t size;
    const void *span;
    uint64_t stamp;
    uint64_t edge_h[2], edge_in_h;
    int edged, edge_unpinned;
    uint8_t edge_streaks, edge_in_hashed;
    uint64_t seen_clk;
    uint64_t reuploads, edge_skips, full_scans, fast_checks;
} mock_tex;

static uint64_t test_now_ns(void) {
    struct timespec ts;
    int rc = clock_gettime(CLOCK_MONOTONIC, &ts);
    assert(rc == 0);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void tex_protect(mock_tex *t, size_t pg, int use_unpin) {
    assert(t != NULL && t->mem != NULL && t->size > 0);
    vka_edge_geom g;
    vka_edge_split((uintptr_t)t->mem, t->size, pg, &g);
    if (use_unpin && t->edge_unpinned) {
        if (g.ie > g.ia) nvn_dirty_protect((const void *)g.ia, g.ie - g.ia);
    } else {
        nvn_dirty_protect(t->mem, t->size);
    }
    vka_edge_snapshot(&g, t->edge_h);
    t->edge_in_hashed = (uint8_t)vka_edge_can_hash_interior(&g, t->edge_unpinned);
    t->edge_in_h = t->edge_in_hashed ? vka_edge_hash(g.ia, g.ie) : 0;
    if (use_unpin && t->edge_unpinned) {
        t->edged = !(g.ie > g.ia && nvn_dirty_since_span(t->span, (const void *)g.ia, g.ie - g.ia, t->stamp));
    } else {
        t->edged = !nvn_dirty_since_span(t->span, t->mem, t->size, t->stamp);
    }
}

static void tex_init(mock_tex *t, const uint8_t *mem, size_t size, size_t pg, int use_unpin) {
    assert(t != NULL && mem != NULL && size > 0);
    memset(t, 0, sizeof *t);
    t->mem = mem; t->size = size;
    t->span = nvn_dirty_span(mem, size);
    assert(t->span != NULL);
    t->stamp = nvn_dirty_now();
    tex_protect(t, pg, use_unpin);
    t->seen_clk = nvn_dirty_now();
}

static int tex_check_changes(mock_tex *t, size_t pg, int check_interior, int use_unpin, size_t *out_lo, size_t *out_hi) {
    vka_edge_geom g;
    vka_edge_split((uintptr_t)t->mem, t->size, pg, &g);
    int h0_bad = 0, h1_bad = 0;
    vka_edge_changed(&g, t->edge_h, &h0_bad, &h1_bad);
    if (g_fault_mode == 1) h1_bad = 0;   /* Delta #27 fault injection: ignore tail edge hash */
    int in_dirty = (check_interior && g.ie > g.ia) ? nvn_dirty_since_span(t->span, (const void *)g.ia, g.ie - g.ia, t->stamp) : 0;
    if (!h0_bad && !h1_bad && !in_dirty) return 0;
    size_t in_lo = 0, in_hi = 0;
    if (in_dirty && !nvn_dirty_bounds_since(t->span, (const void *)g.ia, g.ie - g.ia, t->stamp, &in_lo, &in_hi)) in_dirty = 0;
    if (!h0_bad && !h1_bad && !in_dirty) return 0;
    vka_edge_dirty_bounds(&g, (uintptr_t)t->mem, t->size, h0_bad, h1_bad, in_dirty, in_lo, in_hi, out_lo, out_hi);
    t->edge_streaks = 0;
    t->stamp = nvn_dirty_now();
    tex_protect(t, pg, use_unpin);
    t->reuploads++;
    return 1;
}

static int tex_prepare_sub(mock_tex *t, size_t pg, int use_unpin, size_t *out_lo, size_t *out_hi) {
    assert(t != NULL && out_lo != NULL && out_hi != NULL);
    uint64_t now = nvn_dirty_now();
    int full_scan = (now != t->seen_clk);
    t->seen_clk = now;
    if (full_scan) {
        t->full_scans++;
        if (use_unpin && t->edge_unpinned) return tex_check_changes(t, pg, 1, use_unpin, out_lo, out_hi);
        if (!nvn_dirty_since_span(t->span, t->mem, t->size, t->stamp)) return 0;
        uint64_t st = nvn_dirty_now();
        vka_edge_geom g; vka_edge_split((uintptr_t)t->mem, t->size, pg, &g);
        int h0_bad = 0, h1_bad = 0;
        vka_edge_changed(&g, t->edge_h, &h0_bad, &h1_bad);
        if (g_fault_mode == 1) h1_bad = 0;
        int in_dirty = (g.ie > g.ia && nvn_dirty_since_span(t->span, (const void *)g.ia, g.ie - g.ia, t->stamp));
        if (in_dirty && !h0_bad && !h1_bad && t->edge_in_hashed) {
            int ep = (g.r[0][1] > g.r[0][0] && nvn_dirty_since_span(t->span, (const void *)g.r[0][0], g.r[0][1] - g.r[0][0], t->stamp)) ||
                     (g.r[1][1] > g.r[1][0] && nvn_dirty_since_span(t->span, (const void *)g.r[1][0], g.r[1][1] - g.r[1][0], t->stamp));
            if (vka_edge_interior_collateral(&g, t->edge_in_h, t->edge_in_hashed, ep)) in_dirty = 0;
        }
        if (in_dirty || h0_bad || h1_bad) {
            return tex_check_changes(t, pg, 1, use_unpin, out_lo, out_hi);
        }
        if (t->edge_streaks < 255) t->edge_streaks++;
        if (use_unpin && g.has_edges && t->edge_streaks >= VKA_EDGE_UNPIN_AFTER) t->edge_unpinned = 1;
        t->stamp = st;
        tex_protect(t, pg, use_unpin);
        t->edge_skips++;
        return 0;
    }
    if (use_unpin && t->edge_unpinned) {
        t->fast_checks++;
        return tex_check_changes(t, pg, 0, use_unpin, out_lo, out_hi);
    }
    return 0;
}

static void run_shared_page_bench(size_t pg, int use_unpin) {
    enum { NFRAMES = 1000, SUBS = 6, TEX_OFF = 2048, TEX_SZ = 16 };
    uint8_t *pool = mmap(NULL, 2 * pg, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    assert(pool != MAP_FAILED);
    nvn_dirty_add_range(pool, 2 * pg);
    for (size_t i = 0; i < TEX_SZ; i++) pool[TEX_OFF + i] = (uint8_t)(i + 1);
    mock_tex t;
    tex_init(&t, pool + TEX_OFF, TEX_SZ, pg, use_unpin);

    uint64_t f0 = 0, p0 = 0, mp0 = 0, mpg0 = 0, mpns0 = 0, f1 = 0, p1 = 0, mp1 = 0, mpg1 = 0, mpns1 = 0;
    nvn_dirty_stats(&f0, &p0);
    nvn_dirty_mp_stats(&mp0, &mpg0, &mpns0);
    uint64_t t0 = test_now_ns();
    for (int frame = 0; frame < NFRAMES; frame++) {
        ((volatile uint32_t *)pool)[0] = (uint32_t)(frame + 1);   /* per-frame neighbor uniform write on same page */
        for (int s = 0; s < SUBS; s++) {
            size_t lo = 0, hi = 0;
            int up = tex_prepare_sub(&t, pg, use_unpin, &lo, &hi);
            EXPECT_EQ("neighbor write never triggers false reupload", 0, up);
        }
    }
    uint64_t dt = test_now_ns() - t0;
    nvn_dirty_stats(&f1, &p1);
    nvn_dirty_mp_stats(&mp1, &mpg1, &mpns1);
    uint64_t faults = f1 - f0, mp_calls = mp1 - mp0;
    printf("bench[%s]: frames=%d subs=%d faults=%llu mprotect_calls=%llu full_scans=%llu fast_edge_checks=%llu skips=%llu reups=%llu time_ms=%.3f ns_per_sub=%.1f\n",
           use_unpin ? "unpinned" : "baseline", NFRAMES, SUBS,
           (unsigned long long)faults, (unsigned long long)mp_calls,
           (unsigned long long)t.full_scans, (unsigned long long)t.fast_checks,
           (unsigned long long)t.edge_skips, (unsigned long long)t.reuploads,
           dt / 1e6, (double)dt / (double)(NFRAMES * SUBS));
    if (use_unpin) {
        EXPECT_EQ("unpinned stops faults after VKA_EDGE_UNPIN_AFTER", VKA_EDGE_UNPIN_AFTER, faults);
        EXPECT_EQ("unpinned eliminates full_scans beyond warmup", VKA_EDGE_UNPIN_AFTER, t.full_scans);
    } else {
        EXPECT_EQ("baseline repros 1 fault per frame", NFRAMES, faults);
        EXPECT_EQ("baseline forced full_scan on every faulty frame", NFRAMES, t.full_scans);
    }
    nvn_dirty_remove_range(pool);
    munmap(pool, 2 * pg);
}

static void run_straddling_detection_test(size_t pg) {
    /* 4 pages: [0, 4*pg). Texture sits at [pg/2, 3*pg + pg/2) = size 3*pg (r0 + 2 interior pages + r1). */
    uint8_t *pool = mmap(NULL, 4 * pg, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    assert(pool != MAP_FAILED);
    nvn_dirty_add_range(pool, 4 * pg);
    size_t tex_off = pg / 2, tex_sz = 3 * pg;
    memset(pool + tex_off, 0x3c, tex_sz);
    mock_tex t;
    tex_init(&t, pool + tex_off, tex_sz, pg, 1);

    /* Two neighbor writes beside head & tail edges unpin shared boundaries while leaving interior pages protected. */
    for (int k = 0; k < VKA_EDGE_UNPIN_AFTER; k++) {
        pool[0] = (uint8_t)(k + 1);
        pool[4 * pg - 1] = (uint8_t)(k + 1);
        size_t lo = 0, hi = 0;
        EXPECT_EQ("straddle neighbor write does not reupload", 0, tex_prepare_sub(&t, pg, 1, &lo, &hi));
    }
    EXPECT_EQ("straddle texture is now edge_unpinned", 1, t.edge_unpinned);

    /* Subsequent neighbor writes trigger 0 faults. */
    uint64_t f0 = 0, p0 = 0, f1 = 0, p1 = 0;
    nvn_dirty_stats(&f0, &p0);
    for (int k = 0; k < 20; k++) {
        pool[16] = (uint8_t)(k + 10);
        pool[4 * pg - 16] = (uint8_t)(k + 10);
        size_t lo = 0, hi = 0;
        EXPECT_EQ("post-unpin neighbor writes stay clean", 0, tex_prepare_sub(&t, pg, 1, &lo, &hi));
    }
    nvn_dirty_stats(&f1, &p1);
    EXPECT_EQ("post-unpin neighbor writes cause 0 page faults", 0, f1 - f0);

    /* Real write in head edge r[0] must be detected immediately with tight bounds [0, pg/2). */
    pool[tex_off + 8] = 0x77;
    size_t lo = 999999, hi = 0;
    EXPECT_EQ("write inside head edge r[0] detected", 1, tex_prepare_sub(&t, pg, 1, &lo, &hi));
    EXPECT_EQ("head edge dirty lo", 0, lo);
    EXPECT_EQ("head edge dirty hi", pg / 2, hi);

    /* Real write in exclusive interior [ia, ie) must fault and be detected with interior bounds [pg/2, 5*pg/2). */
    uint64_t fi0 = 0, pi0 = 0, fi1 = 0, pi1 = 0;
    nvn_dirty_stats(&fi0, &pi0);
    pool[pg + 64] = 0x99;
    lo = 999999; hi = 0;
    EXPECT_EQ("write inside exclusive interior detected", 1, tex_prepare_sub(&t, pg, 1, &lo, &hi));
    nvn_dirty_stats(&fi1, &pi1);
    EXPECT_EQ("interior pages remain write-protected (2 pages released)", 2, fi1 - fi0);
    EXPECT_EQ("interior dirty lo >= pg/2", 1, lo >= pg / 2);
    EXPECT_EQ("interior dirty hi <= 5*pg/2", 1, hi <= (2 * pg + pg / 2));

    /* Real write in tail edge r[1] must be detected immediately with tight bounds [5*pg/2, 3*pg). */
    pool[tex_off + tex_sz - 4] = 0xab;
    lo = 999999; hi = 0;
    EXPECT_EQ("write inside tail edge r[1] detected", 1, tex_prepare_sub(&t, pg, 1, &lo, &hi));
    EXPECT_EQ("tail edge dirty lo", 2 * pg + pg / 2, lo);
    EXPECT_EQ("tail edge dirty hi", tex_sz, hi);

    nvn_dirty_remove_range(pool);
    munmap(pool, 4 * pg);
}

int main(void) {
    crash_install();
    const char *fault = getenv("T_EDGE_UNPIN_FAULT");
    g_fault_mode = fault ? atoi(fault) : 0;
    size_t pg = (size_t)getpagesize();
    run_shared_page_bench(pg, 0);
    run_shared_page_bench(pg, 1);
    run_straddling_detection_test(pg);
    printf("t_edge_unpin: fault_mode=%d failures=%d\n", g_fault_mode, g_fail);
    return g_fail != 0;
}
