/* Authoritative producer + deferred consumer KTGL UI widget tagging for AoI. */
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef KTGL_HOST_TEST
#include "dm.h"
#endif
#include "ktgl_wid_classify.h"
#include "ktgl_ui_tag.h"

void dm_nvn_enable_wid(void);
void dm_nvn_set_wid(uint8_t w);

#if defined(__aarch64__)
extern void ktgl_ui_bridge(void);
#else
static void ktgl_ui_bridge(void) {}
#endif

enum {
    AOI_MGR_CTX_OFF = 0x40u,
    AOI_CTX_BATCH_OFF = 0x28e8u,
    AOI_CTX_SUB_OFF = 0x3fb0u,
    AOI_CTX_CUR_OFF = 0x4048u,
    AOI_CTX_BASE_OFF = 0x4050u,
    AOI_FLUSH_OFF = 0x719dc4u,
    AOI_SLACK_LO = 0x1ed7200u,
    AOI_SLACK_HI = 0x1ed8000u,
    AOI_SPAN_CAP = 8192u,
    AOI_SPAN_MAX_BYTES = 16u << 20,
    AOI_SITE_COUNT = 26u
};

static const char AOI_BUILD_ID[] = "659cb288b5c5b2b49853add616c0d5bf";

typedef struct {
    uintptr_t ctx, lo, hi;
    uint8_t wid;
} KtglSpan;

typedef struct {
    KtglSpan items[AOI_SPAN_CAP];
    _Atomic uint32_t head;
    pthread_mutex_t mu;
} KtglSpanRing;

typedef struct {
    uintptr_t mgr, span_ctx, span_start, pend_ctx;
    const void *cur_pane;
    uint8_t wid, pend_wid, in_mgr, span_open;
    uint8_t cons_wid, cons_pend_wid, pend_kind;
    uintptr_t last_ctx, last_lo, last_hi;
    uintptr_t miss_ctx, miss_lo, miss_hi;
    uint32_t miss_head;
    uint8_t last_wid;
} KtglThreadState;

static KtglSpanRing g_ring = {.mu = PTHREAD_MUTEX_INITIALIZER};
static KtglSnapMap g_snap_map;
__attribute__((weak)) extern _Atomic uint32_t vka_env_gen;
static _Atomic uint32_t g_tag_env_gen = ~0u;
static _Atomic int g_tag_env_on = 1;
static _Atomic int g_logged_span = 0;
static _Atomic int g_logged_cons = 0;

static int ktgl_tag_live_on(void) {
    uint32_t g = (&vka_env_gen) ? atomic_load_explicit(&vka_env_gen, memory_order_relaxed) : 0u;
    if (atomic_load_explicit(&g_tag_env_gen, memory_order_relaxed) != g) {
        const char *e = getenv("DM_KTGL_TAG");
        int on = !(e && !strcmp(e, "0"));
        atomic_store_explicit(&g_tag_env_on, on, memory_order_relaxed);
        atomic_store_explicit(&g_tag_env_gen, g, memory_order_release);
        return on;
    }
    return atomic_load_explicit(&g_tag_env_on, memory_order_relaxed);
}

__attribute__((weak)) void svc_guest_range(uint8_t **base, size_t *size);
static KtglAddrBounds g_bounds;
static __thread KtglThreadState t_st;
static __thread KtglWidCache t_cache;

enum {
    HK_NODE = 0,
    HK_EXIT = 1,
    HK_ROLL = 2,
    HK_CONS = 3,
    HK_CONS_END = 4,
    HK_ENTER = 5,
    HK_PANE = 6,
    HK_SUBMIT = 7
};

static const KtglSiteSpec k_aoi_sites[AOI_SITE_COUNT] = {
    {0xae7ba8u, 0xb9010408u, 1u, 0u,  31u, 31u, HK_PANE},
    {0xae9b20u, 0xb9010408u, 1u, 0u,  31u, 31u, HK_PANE},
    {0xaf1e94u, 0xb9010409u, 1u, 0u,  31u, 31u, HK_PANE},
    {0xaf453cu, 0xb9010409u, 1u, 0u,  31u, 31u, HK_PANE},
    {0xaf56e4u, 0xb9010409u, 1u, 0u,  31u, 31u, HK_PANE},
    {0xaf5de4u, 0xb9010409u, 1u, 0u,  31u, 31u, HK_PANE},
    {0xb04548u, 0xb9010408u, 1u, 0u,  31u, 31u, HK_PANE},
    {0xb04d08u, 0xb9010408u, 1u, 0u,  31u, 31u, HK_PANE},
    {0xb05710u, 0xb90106a9u, 1u, 21u, 31u, 31u, HK_PANE},
    {0xb057e8u, 0xb9010408u, 1u, 0u,  31u, 31u, HK_PANE},
    {0xb07c58u, 0xb9010408u, 1u, 0u,  31u, 31u, HK_PANE},
    {0xb0bfe0u, 0xb9010409u, 1u, 0u,  31u, 31u, HK_PANE},
    {0x245974u, 0xf9401288u, 0u, 21u, 21u, 20u, HK_SUBMIT},
    {0xb2775cu, 0xf94002e8u, 0u, 23u, 21u, 20u, HK_SUBMIT},
    {0xb2e4d8u, 0xaa0003f5u, 1u, 0u,  31u, 31u, HK_ENTER},
    {0xb2e634u, 0xf9400b20u, 1u, 0u,  21u, 31u, HK_NODE},
    {0xb2e6ecu, 0xf9400b34u, 1u, 20u, 21u, 31u, HK_NODE},
    {0xb2ec28u, 0xf9400b20u, 1u, 0u,  21u, 31u, HK_NODE},
    {0xb2f658u, 0xf9400b37u, 1u, 23u, 21u, 31u, HK_NODE},
    {0xb32888u, 0xf9400b36u, 1u, 22u, 21u, 31u, HK_NODE},
    {0xb33f3cu, 0xf9400b36u, 1u, 22u, 21u, 31u, HK_NODE},
    {0xb34fb8u, 0xf940091cu, 1u, 28u, 21u, 31u, HK_NODE},
    {0xb34e58u, 0x6d4323e9u, 0u, 21u, 31u, 31u, HK_EXIT},
    {0x7234f4u, 0xa9006d28u, 1u, 27u, 24u, 19u, HK_ROLL},
    {0x725288u, 0x79400108u, 0u, 8u,  21u, 31u, HK_CONS},
    {0x7252ccu, 0xf94013f8u, 0u, 31u, 31u, 31u, HK_CONS_END},
};

static void ensure_guest_bounds(void) {
    if (g_bounds.mem_hi <= g_bounds.mem_lo && svc_guest_range) {
        uint8_t *gb = NULL;
        size_t gs = 0u;
        svc_guest_range(&gb, &gs);
        if (gb && gs) {
            g_bounds.mem_lo = (uintptr_t)gb;
            g_bounds.mem_hi = (uintptr_t)gb + gs;
        }
    }
}

static uintptr_t rd_ptr_at(uintptr_t base, size_t off) {
    uintptr_t v = 0;
    if (base >= 0x10000ULL && base + off >= base && ktgl_os_read(base + off, &v, sizeof v)) return v;
    return 0;
}

static uint32_t rd_u32_at(uintptr_t base, size_t off) {
    uint32_t v = 0;
    if (base >= 0x10000ULL && base + off >= base && ktgl_os_read(base + off, &v, sizeof v)) return v;
    return 0;
}

static uintptr_t resolve_cons_ctx(uintptr_t prod_ctx, int *deferred) {
    uintptr_t sub = rd_ptr_at(prod_ctx, AOI_CTX_SUB_OFF);
    if (sub >= 0x10000ULL && sub + 0x10ULL > sub) {
        *deferred = 1;
        return sub + 0x10ULL;
    }
    *deferred = 0;
    return prod_ctx;
}

static void span_push(uintptr_t ctx, uintptr_t lo, uintptr_t hi, uint8_t wid) {
    if (ctx < 0x10000ULL || hi <= lo || (hi - lo) > AOI_SPAN_MAX_BYTES) return;
    pthread_mutex_lock(&g_ring.mu);
    uint32_t h = atomic_load_explicit(&g_ring.head, memory_order_relaxed);
    g_ring.items[h & (AOI_SPAN_CAP - 1u)] = (KtglSpan){ctx, lo, hi, wid};
    atomic_store_explicit(&g_ring.head, h + 1u, memory_order_release);
    pthread_mutex_unlock(&g_ring.mu);
#ifndef KTGL_HOST_TEST
    if (wid != 0u && !atomic_exchange_explicit(&g_logged_span, 1, memory_order_relaxed)) {
        LOG("ktgl: first producer UI span ctx=%#llx wid=%u",
            (unsigned long long)ctx, (unsigned)(wid & 0x7fu));
    }
#endif
}

static uint8_t span_lookup(uintptr_t ctx, uintptr_t va) {
    if (ctx < 0x10000ULL || va < 0x10000ULL) return 0u;
    if (t_st.last_ctx == ctx && va >= t_st.last_lo && va < t_st.last_hi) return t_st.last_wid;
    uint32_t ch = atomic_load_explicit(&g_ring.head, memory_order_acquire);
    if (ch == 0u) return 0u;
    if (t_st.miss_head == ch && t_st.miss_ctx == ctx && va >= t_st.miss_lo && va < t_st.miss_hi) return 0u;
    uintptr_t gap_lo = 0x10000ULL, gap_hi = ~(uintptr_t)0;
    uint8_t wid = 0u;
    pthread_mutex_lock(&g_ring.mu);
    uint32_t h = atomic_load_explicit(&g_ring.head, memory_order_relaxed);
    for (uint32_t k = 0; k < AOI_SPAN_CAP && k < h; k++) {
        const KtglSpan *s = &g_ring.items[(h - 1u - k) & (AOI_SPAN_CAP - 1u)];
        if (s->ctx != ctx) continue;
        if (va >= s->lo && va < s->hi) {
            t_st.last_ctx = s->ctx; t_st.last_lo = s->lo;
            t_st.last_hi = s->hi; t_st.last_wid = wid = s->wid;
            break;
        }
        if (s->hi <= va && s->hi > gap_lo) gap_lo = s->hi;
        if (s->lo > va && s->lo < gap_hi) gap_hi = s->lo;
    }
    pthread_mutex_unlock(&g_ring.mu);
    if (!wid) { t_st.miss_ctx = ctx; t_st.miss_lo = gap_lo; t_st.miss_hi = gap_hi; t_st.miss_head = h; }
    return wid;
}

static void close_open_span(uintptr_t prod_ctx, uintptr_t cons_ctx, uintptr_t cur) {
    uintptr_t cbase = rd_ptr_at(prod_ctx, AOI_CTX_BASE_OFF);
    uintptr_t lo = (t_st.span_open && t_st.span_ctx == prod_ctx) ? t_st.span_start : cbase;
    if (t_st.in_mgr && cur > lo && cur >= 0x10000ULL && (cur - lo) <= AOI_SPAN_MAX_BYTES) {
        span_push(cons_ctx, lo, cur, t_st.wid);
    }
    t_st.span_ctx = prod_ctx;
    t_st.span_start = cur;
    t_st.span_open = (cur >= 0x10000ULL) ? 1u : 0u;
}

uintptr_t ktgl_aoi_on_pane(uintptr_t pane, uintptr_t u1, uintptr_t u2) {
    (void)u1;
    (void)u2;
    if (!ktgl_tag_live_on()) return 0u;
    t_st.cur_pane = pane ? (const void *)pane : NULL;
    return 0u;
}

uintptr_t ktgl_aoi_on_submit(uintptr_t snap_node, uintptr_t live_node, uintptr_t mgr) {
    (void)mgr;
    if (!ktgl_tag_live_on() || !ktgl_ptr_aligned((const void *)snap_node)) return 0u;
    ensure_guest_bounds();
    const void *pane = (t_st.cur_pane && ktgl_valid_pane(t_st.cur_pane, &g_bounds))
        ? t_st.cur_pane
        : ktgl_resolve_live_pane((const void *)live_node, &g_bounds);
    int cls = pane ? ktgl_classify_pane_cached(pane, &g_bounds, &t_cache) : 0;
    uint8_t wid = cls >= 0 ? (uint8_t)cls : 0u;
    ktgl_snap_put(&g_snap_map, (const void *)snap_node, wid);
    return 0u;
}

uintptr_t ktgl_aoi_mgr_enter(uintptr_t mgr, uintptr_t u1, uintptr_t u2) {
    (void)u1;
    (void)u2;
    t_st.mgr = mgr;
    if (!ktgl_tag_live_on()) {
        if (t_st.wid != 0u) { t_st.wid = 0u; dm_nvn_set_wid(0u); }
        t_st.in_mgr = 0u;
        t_st.span_open = 0u;
        return 0u;
    }
    uintptr_t ctx = rd_ptr_at(mgr, AOI_MGR_CTX_OFF);
    if (ctx < 0x10000ULL) return 0u;
    uintptr_t cur = rd_ptr_at(ctx, AOI_CTX_CUR_OFF);
    int deferred = 0;
    uintptr_t cons_ctx = resolve_cons_ctx(ctx, &deferred);
    if (t_st.in_mgr) close_open_span(ctx, cons_ctx, cur);
    t_st.in_mgr = 1u;
    t_st.span_ctx = ctx;
    t_st.span_start = cur;
    t_st.span_open = (cur >= 0x10000ULL) ? 1u : 0u;
    t_st.wid = 0u;
    if (!deferred && rd_u32_at(ctx, AOI_CTX_BATCH_OFF) == 0u) dm_nvn_set_wid(0u);
    return 0u;
}

uintptr_t ktgl_aoi_pre_node(uintptr_t node_obj, uintptr_t mgr, uintptr_t unused) {
    (void)unused;
    if (!ktgl_tag_live_on()) {
        if (t_st.wid != 0u) { t_st.wid = 0u; dm_nvn_set_wid(0u); }
        return 0u;
    }
    ensure_guest_bounds();
    int cls = ktgl_snap_get(&g_snap_map, (const void *)node_obj);
    uint8_t wid = cls >= 0 ? (uint8_t)cls : 0u;
    uintptr_t eff_mgr = t_st.mgr ? t_st.mgr : mgr;
    uintptr_t ctx = rd_ptr_at(eff_mgr, AOI_MGR_CTX_OFF);
    if (ctx < 0x10000ULL) return 0u;
    uintptr_t cur = rd_ptr_at(ctx, AOI_CTX_CUR_OFF);
    int deferred = 0;
    uintptr_t cons_ctx = resolve_cons_ctx(ctx, &deferred);
    if (!t_st.in_mgr) {
        t_st.in_mgr = 1u;
        t_st.span_ctx = ctx;
        t_st.span_start = cur;
        t_st.span_open = (cur >= 0x10000ULL) ? 1u : 0u;
    } else if (wid != t_st.wid) {
        close_open_span(ctx, cons_ctx, cur);
    }
    if (wid == t_st.wid) return 0u;
    if (deferred) { t_st.wid = wid; return 0u; }
    if (rd_u32_at(ctx, AOI_CTX_BATCH_OFF) != 0u) {
        t_st.pend_kind = 1u;
        t_st.pend_wid = wid;
        t_st.pend_ctx = ctx;
        return ctx;
    }
    t_st.wid = wid;
    dm_nvn_set_wid(wid);
    return 0u;
}

uintptr_t ktgl_aoi_mgr_exit(uintptr_t mgr, uintptr_t u1, uintptr_t u2) {
    (void)u1;
    (void)u2;
    uintptr_t eff_mgr = t_st.mgr ? t_st.mgr : mgr;
    uintptr_t ctx = rd_ptr_at(eff_mgr, AOI_MGR_CTX_OFF);
    int deferred = 0;
    if (ctx >= 0x10000ULL) {
        uintptr_t cons_ctx = resolve_cons_ctx(ctx, &deferred);
        close_open_span(ctx, cons_ctx, rd_ptr_at(ctx, AOI_CTX_CUR_OFF));
    }
    t_st.in_mgr = 0u;
    t_st.span_open = 0u;
    t_st.mgr = 0u;
    t_st.cur_pane = NULL;
    if (deferred) { t_st.wid = 0u; return 0u; }
    if (ctx >= 0x10000ULL && rd_u32_at(ctx, AOI_CTX_BATCH_OFF) != 0u && t_st.wid != 0u) {
        t_st.pend_kind = 1u;
        t_st.pend_wid = 0u;
        t_st.pend_ctx = ctx;
        return ctx;
    }
    t_st.wid = 0u;
    dm_nvn_set_wid(0u);
    return 0u;
}

uintptr_t ktgl_aoi_rollover(uintptr_t base, uintptr_t end, uintptr_t ctx) {
    if (t_st.in_mgr && end > base && (end - base) <= AOI_SPAN_MAX_BYTES) {
        int deferred = 0;
        uintptr_t cons_ctx = resolve_cons_ctx(ctx, &deferred);
        uintptr_t lo = (t_st.span_open && t_st.span_ctx == ctx &&
                        t_st.span_start >= base && t_st.span_start < end)
                           ? t_st.span_start
                           : base;
        span_push(cons_ctx, lo, end, t_st.wid);
    }
    t_st.span_open = 0u;
    t_st.span_start = 0u;
    return 0u;
}

uintptr_t ktgl_aoi_cons_cmd(uintptr_t cmd_va, uintptr_t ctx, uintptr_t u2) {
    (void)u2;
    if (!ktgl_tag_live_on()) {
        if (t_st.cons_wid != 0u) { t_st.cons_wid = 0u; dm_nvn_set_wid(0u); }
        return 0u;
    }
    uint8_t wid = span_lookup(ctx, cmd_va);
#ifndef KTGL_HOST_TEST
    if (wid != 0u && !atomic_exchange_explicit(&g_logged_cons, 1, memory_order_relaxed)) {
        LOG("ktgl: first consumer UI tag ctx=%#llx wid=%u",
            (unsigned long long)ctx, (unsigned)(wid & 0x7fu));
    }
#endif
    if (wid == t_st.cons_wid) return 0u;
    if (ctx >= 0x10000ULL && rd_u32_at(ctx, AOI_CTX_BATCH_OFF) != 0u) {
        t_st.pend_kind = 2u;
        t_st.cons_pend_wid = wid;
        t_st.pend_ctx = ctx;
        return ctx;
    }
    t_st.cons_wid = wid;
    dm_nvn_set_wid(wid);
    return 0u;
}

uintptr_t ktgl_aoi_cons_end(uintptr_t u0, uintptr_t u1, uintptr_t u2) {
    (void)u0;
    (void)u1;
    (void)u2;
    t_st.cons_wid = 0u;
    dm_nvn_set_wid(0u);
    return 0u;
}

uintptr_t ktgl_aoi_post_flush(uintptr_t ctx, uintptr_t u1, uintptr_t u2) {
    (void)u1;
    (void)u2;
    if (t_st.pend_kind == 1u) {
        t_st.wid = t_st.pend_wid;
        dm_nvn_set_wid(t_st.wid);
        uintptr_t cur = rd_ptr_at(ctx, AOI_CTX_CUR_OFF);
        if (cur >= 0x10000ULL) {
            t_st.span_ctx = ctx;
            t_st.span_start = cur;
            t_st.span_open = 1u;
        }
    } else if (t_st.pend_kind == 2u) {
        t_st.cons_wid = t_st.cons_pend_wid;
        dm_nvn_set_wid(t_st.cons_wid);
    }
    t_st.pend_kind = 0u;
    return 0u;
}

static uintptr_t hook_va_for(uint8_t kind) {
    switch (kind) {
    case HK_NODE:     return (uintptr_t)ktgl_aoi_pre_node;
    case HK_EXIT:     return (uintptr_t)ktgl_aoi_mgr_exit;
    case HK_ROLL:     return (uintptr_t)ktgl_aoi_rollover;
    case HK_CONS:     return (uintptr_t)ktgl_aoi_cons_cmd;
    case HK_CONS_END: return (uintptr_t)ktgl_aoi_cons_end;
    case HK_ENTER:    return (uintptr_t)ktgl_aoi_mgr_enter;
    case HK_PANE:     return (uintptr_t)ktgl_aoi_on_pane;
    case HK_SUBMIT:   return (uintptr_t)ktgl_aoi_on_submit;
    default:          return 0u;
    }
}

static int hook_needs_flush(uint8_t kind) {
    return kind == HK_NODE || kind == HK_EXIT || kind == HK_CONS;
}

void ktgl_ui_tag_reset_for_test(void) {
    pthread_mutex_lock(&g_ring.mu);
    memset(g_ring.items, 0, sizeof g_ring.items);
    atomic_store_explicit(&g_ring.head, 0u, memory_order_relaxed);
    pthread_mutex_unlock(&g_ring.mu);
    ktgl_snap_reset(&g_snap_map);
    memset(&t_st, 0, sizeof t_st);
    atomic_store_explicit(&g_tag_env_gen, ~0u, memory_order_relaxed);
    atomic_store_explicit(&g_tag_env_on, 1, memory_order_relaxed);
    atomic_store_explicit(&g_logged_span, 0, memory_order_relaxed);
    atomic_store_explicit(&g_logged_cons, 0, memory_order_relaxed);
    memset(&t_cache, 0, sizeof t_cache);
}

void ktgl_ui_tag_set_bounds_for_test(KtglAddrBounds b) {
    g_bounds = b;
}

int ktgl_ui_tag_install_raw(
    uint8_t *base, uint64_t text_end, uint64_t ro_off, uint64_t data_off,
    uint64_t mod_end, const char *build_id) {
    const char *env = getenv("DM_KTGL_TAG");
    if (env && !strcmp(env, "0")) return 0;
    if (!base || !build_id || strcmp(build_id, AOI_BUILD_ID)) return 0;
    if (!ktgl_check_zero_slack(base, AOI_SLACK_LO, AOI_SLACK_HI)) return -1;
    if (!ktgl_check_sites(base, text_end, k_aoi_sites, AOI_SITE_COUNT)) return -1;
    uint64_t mhi = mod_end > data_off ? mod_end : data_off;
    g_bounds.ro_lo = (uintptr_t)base + ro_off;
    g_bounds.ro_hi = (uintptr_t)base + ktgl_nso_vtable_end(base, data_off, mhi);
    g_bounds.mod_lo = (uintptr_t)base;
    g_bounds.mod_hi = (uintptr_t)base + mhi;
    ensure_guest_bounds();
    KtglEmitCtx ctx = {
        .pool_lo = AOI_SLACK_LO, .pool_hi = AOI_SLACK_HI, .cur = AOI_SLACK_LO,
        .flush_off = AOI_FLUSH_OFF,
        .bridge_va = (uintptr_t)ktgl_ui_bridge,
        .post_flush_va = (uintptr_t)ktgl_aoi_post_flush,
    };
    if (!ktgl_emit_flush_trampoline(base, &ctx)) return -1;
    for (size_t i = 0; i < AOI_SITE_COUNT; i++) {
        uint8_t hk = k_aoi_sites[i].hook_kind;
        int ok = hook_needs_flush(hk)
            ? ktgl_emit_site_stub(base, &ctx, &k_aoi_sites[i], hook_va_for(hk))
            : ktgl_emit_call_stub(base, &ctx, &k_aoi_sites[i], hook_va_for(hk));
        if (!ok) return -1;
    }
    dm_nvn_enable_wid();
    return 1;
}

#ifndef KTGL_HOST_TEST
int ktgl_ui_tag_install(module_t *m) {
    if (!m) return 0;
    uint64_t mend = m->key_page_off ? m->key_page_off : m->size;
    int r = ktgl_ui_tag_install_raw(m->base, m->text_end, m->ro_off, m->data_off, mend, m->build_id);
    if (r > 0) LOG("ktgl: UI widget tagger enabled (%u hooks)", AOI_SITE_COUNT);
    else if (r < 0) LOG("[RED] ktgl: UI widget tagger skipped (site/slack mismatch)");
    return r;
}
#endif
