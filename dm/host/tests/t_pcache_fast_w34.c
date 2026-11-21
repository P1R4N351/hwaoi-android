// Unit, parity, TSan & benchmark harness for Wave 34 pipeline cache fast paths.
#include "../../gfx/vk_android/vka_pckeep.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <time.h>

_Atomic int g_vka_pcache_fast_state;
_Atomic int g_vka_w34_fault;

static uint64_t w34_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static int w34_pck_legacy_parse(const char *t, size_t n, vka_pck_stamp *s) {
    vka_pcache_fast_reset_for_test(0);
    vka_pck_parse(t, n, s);
    return s->table;
}

static void w34_make_stamp_ents(vka_pck_ent *keep, vka_pck_ent *run, size_t n) {
    for (size_t i = 0; i < n; i++) {
        keep[i] = (vka_pck_ent){0x100000ull + (uint64_t)i * 7u, 0xabcdef0000000001ull + i};
        run[i] = (vka_pck_ent){0x100000ull + (uint64_t)(n - 1u - i) * 7u + ((i & 3u) ? 1u : 0u),
                               0xf000000000000001ull + i};
    }
}

static void w34_check_pck_parity(void) {
    vka_pck_ent keep[128], run[128];
    w34_make_stamp_ents(keep, run, 128u);
    size_t l0 = 0, l1 = 0;
    vka_pcache_fast_reset_for_test(0);
    char *t0 = vka_pck_format(0x72c5210a46e5abcdull, 9u, keep, 128u, run, 128u, &l0);
    vka_pcache_fast_reset_for_test(1);
    char *t1 = vka_pck_format(0x72c5210a46e5abcdull, 9u, keep, 128u, run, 128u, &l1);
    assert(t0 && t1 && l0 == l1 && memcmp(t0, t1, l0) == 0);
    vka_pck_stamp s0 = {0}, s1 = {0};
    w34_pck_legacy_parse(t0, l0, &s0);
    vka_pcache_fast_reset_for_test(1);
    vka_pck_parse(t1, l1, &s1);
    assert(s0.lib == s1.lib && s0.table == s1.table && s0.dead == s1.dead && s0.n == s1.n);
    assert(memcmp(s0.ent, s1.ent, s0.n * sizeof *s0.ent) == 0);
    vka_pck_free(&s0); vka_pck_free(&s1); free(t0); free(t1);
}

static size_t w34_make_pw(uint8_t *buf, uint32_t count) {
    size_t kl = offsetof(vka_gkey, va);
    pwhdr2 h = {0};
    memcpy(h.magic, VKA_PW_MAGIC2, 8u);
    h.gkey_size = sizeof(vka_gkey); h.n = count; h.layout = vka_pw_layout();
    memcpy(buf, &h, sizeof h);
    size_t o = sizeof h;
    vka_gkey k = {0};
    k.cfmt[0] = 37u; k.dfmt = 126u; k.ncolor = 1u; k.samples = 1u; k.topo = 3u;
    for (uint32_t i = 0; i < count; i++) {
        pwrec r = {.kind = VKA_PW_GFX, .klen = (uint32_t)kl, .key = {0x100u + i, 0x200u + i}};
        k.att[0].mask = (uint8_t)((i & 15u) | 1u);
        memcpy(buf + o, &r, sizeof r);
        memset(buf + o + sizeof r, 0, vka_pw_size(&r) - sizeof r);
        memcpy(buf + o + sizeof r, &k, kl);
        o += vka_pw_size(&r);
    }
    return o;
}

static void w34_check_pw_and_lshort(void) {
    uint8_t buf[8192];
    size_t len = w34_make_pw(buf, 16u);
    size_t o0 = 0, o1 = 0, first0 = 0, first1 = 0;
    uint32_t n0 = 0, n1 = 0;
    assert(vka_pw_header(buf, len, &first0, &n0) == VKA_PW_OK);
    assert(vka_pw_header(buf, len, &first1, &n1) == VKA_PW_OK);
    o0 = first0; o1 = first1;
    pwrec r0, r1; const uint8_t *kb0 = NULL, *kb1 = NULL; int ok0 = 0, ok1 = 0;
    while (vka_pw_next(buf, len, &o0, &r0, &kb0, &ok0)) {
        assert(vka_pw_next_fast(buf, len, &o1, &r1, &kb1, &ok1, 0) == 1);
        assert(o0 == o1 && ok0 == ok1 && ok0 == 1 && r0.key[0] == r1.key[0]);
    }
    static vka_lshort_cache lc;
    uintptr_t sh0[8] = {11u, 22u, 0, 0, 0, 0, 0, 0};
    uint8_t in[24] = {1, 2, 3, 4}, out[24] = {0};
    uint64_t h = vka_pw_fnv(sh0, sizeof sh0, VKA_PW_FNV0);
    vka_lshort_put(&lc, h, sh0, 0u, in, sizeof in);
    assert(vka_lshort_get(&lc, h, sh0, 0u, out, sizeof out, 0) == 1 && memcmp(in, out, sizeof in) == 0);
    assert(vka_lshort_get(&lc, h, sh0, 1u, out, sizeof out, 0) == 0);
    struct { uint32_t n, pc; uint64_t b[8]; } a = {4u, 1u, {11, 22, 33, 44}}, b = {4u, 2u, {11, 22, 33, 44}};
    assert(vka_lkey_eq_fast(&a, &b, 4u, 8u + 4u * 8u, 0) == 0);
}

static void w34_check_fault(int fm) {
    vka_w34_set_fault(fm);
    if (fm == 1) {
        const char *u = "00000000000000aa\nkeep 1 dead 0 shaders 2\n0000000000000002 00000000000000b2\n0000000000000001 00000000000000b1\n";
        vka_pck_stamp s;
        vka_pcache_fast_reset_for_test(1);
        vka_pck_parse(u, strlen(u), &s);
        assert(s.table && s.n == 2 && s.ent[0].key == 1u);
        vka_pck_free(&s);
    } else if (fm == 2) {
        vka_pck_ent k[1] = {{5u, 0x11u}}, r[1] = {{5u, 0x99u}};
        size_t len = 0;
        vka_pcache_fast_reset_for_test(1);
        char *txt = vka_pck_format(7u, 0u, k, 1u, r, 1u, &len);
        vka_pck_stamp s; vka_pck_parse(txt, len, &s);
        assert(s.n == 1 && s.ent[0].hash == 0x99u);
        vka_pck_free(&s); free(txt);
    } else if (fm == 3) {
        uint8_t buf[1024]; w34_make_pw(buf, 1u);
        size_t off = sizeof(pwhdr2) + sizeof(pwrec);
        vka_gkey g = {0}; g.ncolor = 1u; g.samples = 1u; g.att[0].op = 9u;
        memcpy(buf + off, &g, offsetof(vka_gkey, va));
        size_t o = sizeof(pwhdr2); pwrec pr; const uint8_t *kb = NULL; int ok = 1;
        assert(vka_pw_next_fast(buf, sizeof(pwhdr2) + sizeof(pwrec) + 200u, &o, &pr, &kb, &ok, fm) == 1);
        assert(ok == 0);
    } else if (fm == 4) {
        static vka_lshort_cache lc;
        uintptr_t sh[8] = {1u, 2u, 0, 0, 0, 0, 0, 0};
        uint8_t in[24] = {9}, out[24] = {0};
        uint64_t h = vka_pw_fnv(sh, sizeof sh, VKA_PW_FNV0);
        vka_lshort_put(&lc, h, sh, 0u, in, sizeof in);
        assert(vka_lshort_get(&lc, h, sh, 1u, out, sizeof out, fm) == 0);
    } else if (fm == 5) {
        struct { uint32_t n, pc; uint64_t b[384]; } a = {4u, 1u, {11u, 22u, 33u, 44u}}, b = {4u, 2u, {11u, 22u, 33u, 44u}};
        assert(vka_lkey_eq_fast(&a, &b, 4u, 8u + 4u * 8u, fm) == 0);
    }
    vka_w34_set_fault(0);
}

static uint64_t w34_bench_once(const vka_pck_ent *keep, const vka_pck_ent *run,
                               const uint8_t *pw_buf, size_t pw_len, int fast) {
    uint64_t sink = 0;
    vka_pcache_fast_reset_for_test(fast);
    for (int it = 0; it < 20; it++) {
        size_t len = 0;
        char *txt = vka_pck_format(0xabcdu, 4u, keep, 2048u, run, 256u, &len);
        vka_pck_stamp s; vka_pck_parse(txt, len, &s);
        sink += s.n + s.ent[s.n - 1u].hash;
        vka_pck_free(&s); free(txt);
    }
    for (int it = 0; it < 8; it++) {
        size_t o = sizeof(pwhdr2); pwrec pr; const uint8_t *kb = NULL; int ok = 0;
        while (fast ? vka_pw_next_fast(pw_buf, pw_len, &o, &pr, &kb, &ok, 0) : vka_pw_next(pw_buf, pw_len, &o, &pr, &kb, &ok)) {
            sink += (uint64_t)ok + pr.key[0];
        }
    }
    static vka_lshort_cache lc;
    static struct { uint32_t n, pc; uint64_t b[384]; } lkA = {8u, 1u, {1, 2, 3, 4, 5, 6, 7, 8}}, lkB = {8u, 1u, {1, 2, 3, 4, 5, 6, 7, 8}};
    uintptr_t sh[8] = {0x10u, 0x20u, 0, 0, 0, 0, 0, 0};
    uint8_t val[24] = {0x7au, 0x11u}, out[24] = {0};
    uint64_t h = vka_pw_fnv(sh, sizeof sh, VKA_PW_FNV0);
    vka_lshort_put(&lc, h, sh, 0u, val, sizeof val);
    for (uint32_t q = 0; q < 100000u; q++) {
        if (fast) {
            if (vka_lshort_get(&lc, h, sh, 0u, out, sizeof out, 0)) sink += out[0];
            if ((q & 7u) == 0u) sink += (uint64_t)vka_lkey_eq_fast(&lkA, &lkB, 8u, 8u + 8u * 8u, 0);
        } else {
            uint64_t hh = vka_pw_fnv(sh, sizeof sh, VKA_PW_FNV0);
            uint64_t lk_h = vka_pw_fnv(&lkA, sizeof lkA, VKA_PW_FNV0);
            sink += (uint64_t)val[0] + (hh & 0u);
            if ((q & 7u) == 0u) sink += (uint64_t)(memcmp(&lkA, &lkB, sizeof lkA) == 0) + (lk_h & 0u);
        }
    }
    return sink;
}

static vka_lshort_cache g_tsan_lc;

static void *w34_tsan_worker(void *arg) {
    uint64_t id = (uint64_t)(uintptr_t)arg;
    uintptr_t sh[8] = {id + 1u, id + 9u, 0, 0, 0, 0, 0, 0};
    uint8_t in[24] = {(uint8_t)(id + 1u)}, out[24] = {0};
    uint64_t h = vka_pw_fnv(sh, sizeof sh, VKA_PW_FNV0);
    if (id == 0u) vka_lshort_put(&g_tsan_lc, h, sh, 0u, in, sizeof in);
    uint64_t s = 0;
    for (int i = 0; i < 4000; i++) {
        vka_lshort_get(&g_tsan_lc, h, sh, 0u, out, sizeof out, 0);
        s += out[0] + (uint64_t)vka_pcache_fast_on();
    }
    return (void *)(uintptr_t)s;
}

int main(void) {
    const char *env = getenv("DM_VK_PCACHE_FAST");
    const char *flt = getenv("T_W34_FAULT");
    int init_on = vka_pcache_fast_on();
    if (env && !strcmp(env, "0")) assert(!init_on); else assert(init_on);
    w34_check_pck_parity();
    w34_check_pw_and_lshort();
    if (flt && atoi(flt) > 0) w34_check_fault(atoi(flt));
    pthread_t th[4];
    uintptr_t sh0[8] = {1u, 9u, 0, 0, 0, 0, 0, 0};
    uint8_t v0[24] = {1u};
    vka_lshort_put(&g_tsan_lc, vka_pw_fnv(sh0, sizeof sh0, VKA_PW_FNV0), sh0, 0u, v0, sizeof v0);
    for (int i = 0; i < 4; i++) assert(pthread_create(&th[i], NULL, w34_tsan_worker, (void *)(uintptr_t)i) == 0);
    for (int i = 0; i < 4; i++) { void *r = NULL; assert(pthread_join(th[i], &r) == 0); }

    vka_pck_ent *keep = (vka_pck_ent *)calloc(2048u, sizeof *keep), *run = (vka_pck_ent *)calloc(256u, sizeof *run);
    uint8_t *pw_buf = (uint8_t *)calloc(1u, 2000u * 300u);
    assert(keep && run && pw_buf);
    w34_make_stamp_ents(keep, keep, 2048u);
    for (size_t i = 0; i < 256u; i++) run[i] = (vka_pck_ent){0x500000ull + i * 3u, 0x77770001ull + i};
    size_t pw_len = w34_make_pw(pw_buf, 1500u);

    uint64_t t0 = w34_ns(), s0 = w34_bench_once(keep, run, pw_buf, pw_len, 0); uint64_t off_ns = w34_ns() - t0;
    uint64_t t1 = w34_ns(), s1 = w34_bench_once(keep, run, pw_buf, pw_len, 1); uint64_t on_ns = w34_ns() - t1;
    assert(s0 == s1);
    printf("BENCH aoi_w34_off pcache=%.3f ms sink=%llu\n", off_ns / 1e6, (unsigned long long)s0);
    printf("BENCH aoi_w34_on pcache=%.3f ms sink=%llu\n", on_ns / 1e6, (unsigned long long)s1);
    free(keep); free(run); free(pw_buf);
    return 0;
}
