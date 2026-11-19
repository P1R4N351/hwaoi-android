// SPDX-License-Identifier: MIT
// Host unit tests for dm/host/android/companion_state.h and dm/host/android/oot_companion.h.
#include "../android/oot_companion.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define SIM_BASE 0x7000000000ULL
#define SIM_SIZE (80ULL * 1024ULL * 1024ULL)

enum {
    VA_GDM = 0x04900000ULL, VA_BOOL_ENT = 0x04902000ULL, VA_BOOL_TBL = 0x04903000ULL,
    VA_INT_ENT = 0x04904000ULL, VA_INT_TBL = 0x04905000ULL, VA_FLT_ENT = 0x04906000ULL,
    VA_FLT_TBL = 0x04907000ULL, VA_STR_ENT = 0x04908000ULL, VA_STR_TBL = 0x0490A000ULL,
    VA_STR_BUF = 0x0490C000ULL, VA_UI = 0x04910000ULL, VA_BANC = 0x04911000ULL,
    VA_EVENT = 0x04912000ULL, VA_SCENE = 0x04913000ULL, VA_STBL = 0x04914000ULL,
    VA_SARR = 0x04915000ULL, VA_PMGR = 0x04916000ULL, VA_LINK = 0x04917000ULL,
    VA_HOLD = 0x04918000ULL, VA_ACT = 0x04919000ULL, VA_COMP = 0x0491A000ULL,
    VA_ASC = 0x0491B000ULL, VA_ASI = 0x0491C000ULL, VA_CTRL = 0x0491D000ULL,
    VA_SAR = 0x0491E000ULL, VA_S0 = 0x0491F000ULL, VA_CMD = 0x04920000ULL,
    VA_MSG = 0x04922000ULL,
};

typedef struct {
    uint8_t buf[SIM_SIZE];
    uint32_t read_calls;
} SimSpace;

static SimSpace g_sim;
static uint32_t g_bool_n, g_int_n, g_str_n, g_str_cursor;

static int sim_read(void *ctx, uint64_t addr, void *out, size_t len) {
    SimSpace *s = (SimSpace *)ctx;
    s->read_calls++;
    if (!s || addr < SIM_BASE || addr + len > SIM_BASE + SIM_SIZE || addr + len < addr) return 0;
    memcpy(out, s->buf + (size_t)(addr - SIM_BASE), len);
    return 1;
}

static void w8(uint64_t off, uint8_t v) { assert(off + 1U <= SIM_SIZE); g_sim.buf[off] = v; }
static void w32(uint64_t off, uint32_t v) { assert(off + 4U <= SIM_SIZE); memcpy(g_sim.buf + off, &v, 4); }
static void w64(uint64_t off, uint64_t v) { assert(off + 8U <= SIM_SIZE); memcpy(g_sim.buf + off, &v, 8); }
static void wstr(uint64_t off, const char *s) {
    size_t n = strlen(s) + 1U;
    assert(off + n <= SIM_SIZE);
    memcpy(g_sim.buf + off, s, n);
}

static void add_gdm_slot(uint64_t tbl, uint32_t slots, uint32_t hash, uint32_t idx) {
    uint32_t s = hash % slots;
    for (uint32_t p = 0U; p < slots; p++) {
        uint32_t cur = 0U;
        memcpy(&cur, g_sim.buf + tbl + (size_t)s * 8U, 4);
        if (cur == 0U) {
            w32(tbl + (uint64_t)s * 8ULL, hash);
            w32(tbl + (uint64_t)s * 8ULL + 4ULL, idx);
            return;
        }
        s = (s + 1U < slots) ? (s + 1U) : 0U;
    }
    assert(0 && "hash table full");
}

static void init_box(uint32_t kind_off, uint32_t count, uint64_t ent, uint64_t tbl, uint32_t slots) {
    uint64_t b = VA_GDM + kind_off;
    w32(b + 8U, count);
    w64(b + 16U, SIM_BASE + ent);
    w64(b + 24U, SIM_BASE + tbl);
    w32(b + 36U, slots);
}

static void put_bool(uint32_t hash, uint8_t val) {
    uint32_t idx = g_bool_n++;
    w8(VA_BOOL_ENT + (uint64_t)idx * OOT_GDM_BOOL_STRIDE + OOT_GDM_BOOL_VAL, val);
    add_gdm_slot(VA_BOOL_TBL, 64U, hash, idx);
}

static void put_int(uint32_t hash, uint32_t val) {
    uint32_t idx = g_int_n++;
    w32(VA_INT_ENT + (uint64_t)idx * OOT_GDM_INT_STRIDE + OOT_GDM_NUM_VAL, val);
    add_gdm_slot(VA_INT_TBL, 64U, hash, idx);
}

static void put_str(uint32_t hash, const char *val) {
    uint32_t idx = g_str_n++;
    uint64_t buf_off = VA_STR_BUF + g_str_cursor;
    g_str_cursor += 64U;
    wstr(buf_off, val);
    w64(VA_STR_ENT + (uint64_t)idx * OOT_GDM_STR64_STRIDE + OOT_GDM_STR64_PTR, SIM_BASE + buf_off);
    add_gdm_slot(VA_STR_TBL, 32U, hash, idx);
}

static void setup_world(uint32_t life, uint32_t max_life, const char *scene, uint32_t loading, uint32_t trans, const char *as_cmd) {
    memset(&g_sim, 0, sizeof(g_sim));
    g_bool_n = g_int_n = g_str_n = g_str_cursor = 0U;
    w64(OOT_OFF_GAMEDATA_MGR_PTR, SIM_BASE + VA_GDM);
    w64(OOT_OFF_GAMEUI_MGR_PTR, SIM_BASE + VA_UI);
    w64(OOT_OFF_BANC_MGR_PTR, SIM_BASE + VA_BANC);
    w64(OOT_OFF_EVENT_MGR_PTR, SIM_BASE + VA_EVENT);
    w64(OOT_OFF_SCENE_MGR_PTR, SIM_BASE + VA_SCENE);
    w64(OOT_OFF_MSG_HOLDER_PTR, SIM_BASE + VA_MSG);

    put_bool(OOT_KEY_LOADING_BANC, (uint8_t)loading);
    put_bool(0xF5DEEF0AU, (uint8_t)trans);
    put_int(OOT_KEY_LIFE, life);
    put_int(OOT_KEY_MAX_LIFE, max_life);
    put_str(OOT_KEY_CURRENT_BANC, scene);

    init_box(OOT_GDM_BOOL_OFF, g_bool_n, VA_BOOL_ENT, VA_BOOL_TBL, 64U);
    init_box(OOT_GDM_INT_OFF, g_int_n, VA_INT_ENT, VA_INT_TBL, 64U);
    init_box(OOT_GDM_FLOAT_OFF, 0U, VA_FLT_ENT, VA_FLT_TBL, 16U);
    init_box(OOT_GDM_STR64_OFF, g_str_n, VA_STR_ENT, VA_STR_TBL, 32U);

    w64(VA_SCENE + OOT_SCENE_TABLE, SIM_BASE + VA_STBL);
    w32(VA_STBL + OOT_TABLE_COUNT, 16U);
    w64(VA_STBL + OOT_TABLE_ARRAY, SIM_BASE + VA_SARR);
    w64(VA_SARR + OOT_PLAYER_SLOT_OFF, SIM_BASE + VA_PMGR);
    w64(VA_PMGR + OOT_MGR_MUST_SET, 1U);
    w64(VA_PMGR + OOT_MGR_LINK0, SIM_BASE + VA_LINK);
    w64(VA_LINK + OOT_LINK_HOLDER, SIM_BASE + VA_HOLD);
    w32(VA_LINK + OOT_LINK_ID, 77U);
    w64(VA_HOLD + OOT_HOLDER_ACTOR, SIM_BASE + VA_ACT);
    w32(VA_HOLD + OOT_HOLDER_ID, 77U);
    w32(VA_ACT + OOT_ACTOR_STATE, 4U);
    w64(VA_ACT + OOT_ACTOR_COMPS, SIM_BASE + VA_COMP);
    w64(VA_COMP + OOT_COMP_AS, SIM_BASE + VA_ASC);
    w64(VA_ASC + OOT_AS_INNER, SIM_BASE + VA_ASI);
    w64(VA_ASI + OOT_AS_CTRL, SIM_BASE + VA_CTRL);
    w32(VA_CTRL + OOT_AS_SLOT_COUNT, 1U);
    w64(VA_CTRL + OOT_AS_SLOT_ARRAY, SIM_BASE + VA_SAR);
    w64(VA_SAR, SIM_BASE + VA_S0);
    w64(VA_S0 + OOT_AS_SLOT0_CMD, SIM_BASE + VA_CMD);
    wstr(VA_CMD, as_cmd);
    w32(VA_MSG + OOT_MSG_ID, 0xFFFFFFFFU);
}

static void test_status_states(void) {
    OotTapMem mem = {sim_read, &g_sim};
    CompanionState st;

    // 1. DM_COMPANION=0 -> NONE
    setup_world(24U, 36U, "KokiriForest_Child", 0U, 0U, "Walk");
    oot_companion_eval(0, OOT_MAIN_BUILD, &mem, SIM_BASE, 0U, 1, &st);
    assert(st.status == COMPANION_STATE_NONE && st.in_game == 0);

    // 2. Empty build / zero main_addr -> STARTING
    oot_companion_eval(1, "", &mem, SIM_BASE, 0U, -1, &st);
    assert(st.status == COMPANION_STATE_STARTING && st.in_game == 0);
    oot_companion_eval(1, OOT_MAIN_BUILD, &mem, 0ULL, 0U, -1, &st);
    assert(st.status == COMPANION_STATE_STARTING && st.in_game == 0);

    // 3. Wrong build -> UNSUPPORTED
    oot_companion_eval(1, "33fa6c2c23848282a77e8b88a0a93f48", &mem, SIM_BASE, 0U, -1, &st);
    assert(st.status == COMPANION_STATE_UNSUPPORTED && st.in_game == 0);

    // 4. GAMEDATA_MGR_PTR == 0 -> STARTING
    w64(OOT_OFF_GAMEDATA_MGR_PTR, 0ULL);
    oot_companion_eval(1, OOT_MAIN_BUILD, &mem, SIM_BASE, 0U, -1, &st);
    assert(st.status == COMPANION_STATE_STARTING && st.in_game == 0);

    // 5. Title screen conditions: max_life == 0, scene == "", scene == "Title" -> TITLE
    setup_world(0U, 0U, "KokiriForest_Child", 0U, 0U, "Walk");
    oot_companion_eval(1, OOT_MAIN_BUILD, &mem, SIM_BASE, 0U, -1, &st);
    assert(st.status == COMPANION_STATE_TITLE && st.in_game == 0);

    setup_world(24U, 36U, "", 0U, 0U, "Walk");
    oot_companion_eval(1, OOT_MAIN_BUILD, &mem, SIM_BASE, 0U, -1, &st);
    assert(st.status == COMPANION_STATE_TITLE && st.in_game == 0);

    setup_world(24U, 36U, "Title", 0U, 0U, "Walk");
    oot_companion_eval(1, OOT_MAIN_BUILD, &mem, SIM_BASE, 0U, -1, &st);
    assert(st.status == COMPANION_STATE_TITLE && st.in_game == 0);

    // 6. Loading and transition -> LOADING
    setup_world(24U, 36U, "KokiriForest_Child", 1U, 0U, "Walk");
    oot_companion_eval(1, OOT_MAIN_BUILD, &mem, SIM_BASE, 0U, -1, &st);
    assert(st.status == COMPANION_STATE_LOADING && st.in_game == 0);

    setup_world(24U, 36U, "KokiriForest_Child", 0U, 1U, "Walk");
    oot_companion_eval(1, OOT_MAIN_BUILD, &mem, SIM_BASE, 0U, -1, &st);
    assert(st.status == COMPANION_STATE_LOADING && st.in_game == 0);

    // 7. Ready world state
    setup_world(24U, 36U, "KokiriForest_Child", 0U, 0U, "Walk");
    oot_companion_eval(1, OOT_MAIN_BUILD, &mem, SIM_BASE, 0U, 2, &st);
    assert(st.status == COMPANION_STATE_READY && st.in_game == 1);
    assert(st.life == 24 && st.max_life == 36 && st.tablet_owned == 1);
    assert(st.menu_open == 0 && st.page == 2);
}

static void test_pause_ocarina_rate_and_words(void) {
    OotTapMem mem = {sim_read, &g_sim};
    OotCompanionCtx ctx;
    oot_companion_ctx_init(&ctx);
    ctx.user_page = 1;

    setup_world(20U, 36U, "KokiriForest_Child", 0U, 0U, "Walk");
    uint64_t t = 1000000000ULL;

    // Non-pause canvas does not assert menu_open.
    oot_companion_ctx_note_canvas(&ctx, 0x4BED098ULL);
    oot_companion_ctx_present(&ctx, 1, OOT_MAIN_BUILD, &mem, SIM_BASE, t);
    assert(ctx.state.status == COMPANION_STATE_READY && ctx.state.menu_open == 0);

    // Pause canvas holds menu_open across 3 presents, clears on 4th.
    oot_companion_ctx_note_canvas(&ctx, OOT_SCREEN_VT_PAUSE);
    for (int f = 0; f < 3; f++) {
        t += 16000000ULL;
        oot_companion_ctx_present(&ctx, 1, OOT_MAIN_BUILD, &mem, SIM_BASE, t);
        assert(ctx.state.menu_open == 1);
    }
    t += 16000000ULL;
    oot_companion_ctx_present(&ctx, 1, OOT_MAIN_BUILD, &mem, SIM_BASE, t);
    assert(ctx.state.menu_open == 0);

    // 20 Hz rate limit: calls within 50 ms do not trigger extra memory reads.
    uint32_t before = g_sim.read_calls;
    oot_companion_ctx_present(&ctx, 1, OOT_MAIN_BUILD, &mem, SIM_BASE, t + 10000000ULL);
    assert(g_sim.read_calls == before);

    // Switch to DemoOcarina, advance >= 50 ms -> sample updates and page becomes 4.
    wstr(VA_CMD, "DemoOcarina");
    t += OOT_COMPANION_SAMPLE_INTERVAL_NS;
    oot_companion_ctx_present(&ctx, 1, OOT_MAIN_BUILD, &mem, SIM_BASE, t);
    assert(g_sim.read_calls > before);
    assert(ctx.state.page == COMPANION_PAGE_OCARINA);

    // Returning to Walk restores user_page.
    wstr(VA_CMD, "Walk");
    t += OOT_COMPANION_SAMPLE_INTERVAL_NS;
    oot_companion_ctx_present(&ctx, 1, OOT_MAIN_BUILD, &mem, SIM_BASE, t);
    assert(ctx.state.page == 1);

    // 8-word packing order check.
    int64_t words[COMPANION_STATE_FIELDS];
    companion_state_words(&ctx.state, 42, words);
    assert(words[0] == COMPANION_STATE_READY && words[1] == 1 && words[2] == 0 && words[3] == 1);
    assert(words[4] == 20 && words[5] == 36 && words[6] == 1 && words[7] == 42);
}

int main(void) {
    test_status_states();
    test_pause_ocarina_rate_and_words();
    printf("t_oot_companion_waist: PASS\n");
    return 0;
}
