/* Wave 27 (Opt 29) unit, TSan stress, microbench, and fault-injection tests for DM_FS_LOOKUP_FAST. */
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define T_W27_FS_TEST_HOOKS 1
#include "../fs_lookup_fast.h"

#define W27_ROM_ENTRIES 512

static uint64_t w27_now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

static const char *const g_mnames[64] = {
    "rom", "aoc0", "aoc1", "aoc2", "save", "temp", "bis", "sdmc",
    "m08", "m09", "m10", "m11", "m12", "m13", "m14", "m15",
    "m16", "m17", "m18", "m19", "m20", "m21", "m22", "m23",
    "m24", "m25", "m26", "m27", "m28", "m29", "m30", "romfs",
    "dlc0", "dlc1", "dlc2", "dlc3", "dlc4", "dlc5", "dlc6", "dlc7",
    "dlc8", "dlc9", "dlca", "dlcb", "dlcc", "dlcd", "dlce", "dlcf",
    "ex0", "ex1", "ex2", "ex3", "ex4", "ex5", "ex6", "ex7",
    "ex8", "ex9", "exa", "exb", "exc", "exd", "exe", "target",
};

static char g_sorted_paths[W27_ROM_ENTRIES][96];
static const int g_hot_idx[8] = {12, 77, 143, 211, 298, 365, -1, 491};

static void init_sorted_rom(void) {
    for (int i = 0; i < W27_ROM_ENTRIES; i++) {
        if (i == 491) {
            snprintf(g_sorted_paths[i], sizeof g_sorted_paths[i],
                     "/%03d/Model/Weapon_MasterSword_LongPathWithinCache_VariantA.bfres.zs", i);
        } else {
            snprintf(g_sorted_paths[i], sizeof g_sorted_paths[i],
                     "/%03d/Pack/Actor/Entry_Data_%03d.pack.zs", i, i);
        }
    }
}

static const char *fixture_rel(int p) {
    if (p == 6) return "/999/Missing/Optional/DLC_Probe.byml";
    return g_sorted_paths[g_hot_idx[p]];
}

static int64_t bsearch_rom(const char *rest) {
    int lo = 0, hi = W27_ROM_ENTRIES;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        int c = strcmp(g_sorted_paths[mid], rest);
        if (c == 0) return (int64_t)mid;
        if (c < 0) lo = mid + 1;
        else hi = mid;
    }
    errno = ENOENT;
    return -1;
}

static int64_t slow_mnt_and_entry(const char *path, int active_cnt, int *mi_out) {
    const char *c = strchr(path, ':');
    if (!c) return -1;
    size_t ln = (size_t)(c - path);
    int mi = -1;
    for (int i = 0; i < active_cnt; i++) {
        if (strlen(g_mnames[i]) == ln && !strncmp(g_mnames[i], path, ln)) { mi = i; break; }
    }
    if (mi < 0) return -1;
    *mi_out = mi;
    int64_t idx = bsearch_rom(c + 1);
    return idx < 0 ? -1 : (int64_t)(mi * 1000 + idx + 1);
}

static void publish_mounts(fs_lookup_mnt_table *mt, fs_lookup_path_cache *pc, int cnt) {
    uint64_t ep = fs_lookup_mnt_begin(mt);
    for (int i = 0; i < cnt; i++)
        fs_lookup_mnt_insert(mt, ep, g_mnames[i], strlen(g_mnames[i]), i);
    fs_lookup_path_bump(pc);
}

static int64_t fast_mnt_and_entry(fs_lookup_mnt_table *mt, fs_lookup_path_cache *pc,
                                  const char *path, int active_cnt, uintptr_t img_tok, int *mi_out) {
    char fwd[1024];
    const char *mn = NULL, *rest = NULL;
    size_t ln = 0;
    int dot = 0;
    if (!fs_lookup_split_path(path, fwd, sizeof fwd, &mn, &ln, &rest, &dot)) return -1;
    int mi = -1;
    if (!fs_lookup_mnt_find(mt, mn, ln, &mi)) {
        for (int i = 0; i < active_cnt; i++) {
            if (strlen(g_mnames[i]) == ln && !strncmp(g_mnames[i], mn, ln)) { mi = i; break; }
        }
        if (mi < 0) return -1;
    }
    *mi_out = mi;
    size_t rlen = strlen(rest);
    int64_t k = -1;
    int err_no = 0;
    if (fs_lookup_path_get(pc, (uint32_t)mi, img_tok, rest, rlen, &k, &err_no)) {
        if (k < 0 && err_no) errno = err_no;
        return k;
    }
    int64_t idx = bsearch_rom(rest);
    k = idx < 0 ? -1 : (int64_t)(mi * 1000 + idx + 1);
    fs_lookup_path_put(pc, (uint32_t)mi, img_tok, rest, rlen, k, k < 0 ? errno : 0);
    return k;
}

static int run_fault(int f) {
    fs_lookup_reset_for_test(1);
    static fs_lookup_mnt_table mt;
    static fs_lookup_path_cache pc;
    memset(&mt, 0, sizeof mt);
    memset(&pc, 0, sizeof pc);
    pc.epoch = 1;
    publish_mounts(&mt, &pc, 64);
    uintptr_t tok = 0x1000u;
    int mi = -1;
    char buf[256];
    if (f == 1) {
        snprintf(buf, sizeof buf, "target:%.120s", fixture_rel(0));
        int64_t a = fast_mnt_and_entry(&mt, &pc, buf, 64, tok, &mi);
        int64_t b = fast_mnt_and_entry(&mt, &pc, buf, 64, tok, &mi);
        return (a == 63013 && b == 63013) ? 0 : 1;
    }
    if (f == 2) {
        snprintf(buf, sizeof buf, "target:%.120s", fixture_rel(3));
        assert(fast_mnt_and_entry(&mt, &pc, buf, 64, tok, &mi) == 63212);
        publish_mounts(&mt, &pc, 4);
        return fs_lookup_mnt_find(&mt, "target", 6u, &mi) == 0 ? 0 : 1;
    }
    if (f == 3) {
        snprintf(buf, sizeof buf, "rom:%.120s", fixture_rel(6));
        errno = 0;
        int64_t a = fast_mnt_and_entry(&mt, &pc, buf, 64, tok, &mi);
        errno = 0;
        int64_t b = fast_mnt_and_entry(&mt, &pc, buf, 64, tok, &mi);
        return (a == -1 && b == -1 && errno == ENOENT) ? 0 : 1;
    }
    if (f == 4) {
        snprintf(buf, sizeof buf, "rom:%.120s", fixture_rel(0));
        assert(fast_mnt_and_entry(&mt, &pc, buf, 64, tok, &mi) == 13);
        uint32_t target_slot = fs_lookup_path_idx(0u, fs_lookup_hash64(fixture_rel(0), strlen(fixture_rel(0))));
        char coll[128] = {0};
        for (int n = 0; n < 16384; n++) {
            char cand[64];
            int len = snprintf(cand, sizeof cand, "/999/Coll%d", n);
            if (fs_lookup_path_idx(0u, fs_lookup_hash64(cand, (size_t)len)) == target_slot) {
                snprintf(coll, sizeof coll, "rom:%s", cand);
                break;
            }
        }
        assert(coll[0] != 0);
        return fast_mnt_and_entry(&mt, &pc, coll, 64, tok, &mi) == -1 ? 0 : 1;
    }
    if (f == 5) {
        int idx = -1;
        int hit = fs_lookup_mnt_find(&mt, "target", 6u, &idx);
        return (hit == 1 && idx == 63) ? 0 : 1;
    }
    return 1;
}

typedef struct {
    fs_lookup_mnt_table *mt;
    fs_lookup_path_cache *pc;
} w27_stress_t;

static void *w27_worker(void *arg) {
    w27_stress_t *c = (w27_stress_t *)arg;
    for (int i = 0; i < 4000; i++) {
        char buf[256];
        int m = (i * 7) & 63;
        int p = i & 7;
        snprintf(buf, sizeof buf, "%.31s:%.120s", g_mnames[m], fixture_rel(p));
        int mi = -1;
        int64_t got = fast_mnt_and_entry(c->mt, c->pc, buf, 64, 0x2000u, &mi);
        int64_t want = (p == 6) ? -1 : (int64_t)(m * 1000 + g_hot_idx[p] + 1);
        assert(got == want && mi == m);
    }
    return NULL;
}

int main(void) {
    init_sorted_rom();
    int f = fs_lookup_w27_fault();
    if (f > 0) {
        int rc = run_fault(f);
        printf("t_fs_lookup_fast_w27: fault=%d caught=%d\n", f, rc != 0 ? 1 : 0);
        return rc;
    }
    static fs_lookup_mnt_table mt;
    static fs_lookup_path_cache pc;
    pc.epoch = 1;
    publish_mounts(&mt, &pc, 64);

    for (int mode = 0; mode < 2; mode++) {
        fs_lookup_reset_for_test(mode);
        char fwd[1024];
        const char *mn = NULL, *rest = NULL;
        size_t ln = 0;
        int dot = 0;
        assert(fs_lookup_split_path("rom:\\Pack\\Actor/./Enemy.pack.zs", fwd, sizeof fwd, &mn, &ln, &rest, &dot));
        assert(ln == 3u && !strncmp(mn, "rom", 3u) && dot == 1 && !strcmp(rest, "/Pack/Actor/./Enemy.pack.zs"));
        for (int pass = 0; pass < 2; pass++) {
            for (int m = 0; m < 64; m++) {
                for (int p = 0; p < 8; p++) {
                    char buf[256];
                    snprintf(buf, sizeof buf, "%.31s:%.120s", g_mnames[m], fixture_rel(p));
                    int mi = -1;
                    int64_t got = fast_mnt_and_entry(&mt, &pc, buf, 64, 0x2000u, &mi);
                    int64_t want = (p == 6) ? -1 : (int64_t)(m * 1000 + g_hot_idx[p] + 1);
                    assert(got == want && mi == m);
                }
            }
        }
        char long_p[220];
        memset(long_p, 'a', 190);
        memcpy(long_p, "rom:/", 5);
        long_p[190] = 0;
        int mi = -1;
        assert(fast_mnt_and_entry(&mt, &pc, long_p, 64, 0x2000u, &mi) == -1 && mi == 0);
    }

    fs_lookup_reset_for_test(1);
    w27_stress_t ctx = {.mt = &mt, .pc = &pc};
    pthread_t th[4];
    for (int i = 0; i < 4; i++) assert(pthread_create(&th[i], NULL, w27_worker, &ctx) == 0);
    for (int i = 0; i < 4; i++) assert(pthread_join(th[i], NULL) == 0);

    static char bench_paths[16][256];
    for (int i = 0; i < 16; i++) {
        int m = (i & 1) ? 63 : 31;
        snprintf(bench_paths[i], sizeof bench_paths[i], "%.31s:%.120s", g_mnames[m], fixture_rel(i & 7));
    }
    for (int mode = 0; mode < 2; mode++) {
        fs_lookup_reset_for_test(mode);
        volatile uint64_t sink = 0;
        uint64_t t0 = w27_now_ns();
        for (int i = 0; i < 200000; i++) {
            int mi = -1;
            const char *bp = bench_paths[i & 15];
            int64_t k = mode ? fast_mnt_and_entry(&mt, &pc, bp, 64, 0x2000u, &mi)
                             : slow_mnt_and_entry(bp, 64, &mi);
            sink += (uint64_t)(k + mi + 1000);
        }
        printf("BENCH aoi_w27_%s fs_lookup=%.3f ms sink=%llu\n", mode ? "on" : "off",
               (double)(w27_now_ns() - t0) / 1e6, (unsigned long long)sink);
    }
    return 0;
}
