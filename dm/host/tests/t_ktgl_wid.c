#define _GNU_SOURCE
/* Host unit tests for ktgl_wid_classify.h across AoI and AoC ScreenLayout/Pane graphs. */
#include <assert.h>
#include <sys/mman.h>
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#ifndef KTGL_CLASSIFY_HDR
#include "../ktgl_wid_classify.h"
#endif
#include <stdlib.h>
#include "../ktgl_ui_tag.h"
#include <stdatomic.h>
_Atomic uint32_t vka_env_gen;

int ktgl_ui_tag_install_raw(uint8_t *base, uint64_t text_end, uint64_t ro_off, uint64_t data_off, uint64_t mod_end, const char *build_id);
void ktgl_ui_tag_reset_for_test(void);
void ktgl_ui_tag_set_bounds_for_test(KtglAddrBounds b);
uintptr_t ktgl_aoi_pre_node(uintptr_t node_obj, uintptr_t mgr, uintptr_t unused);
uintptr_t ktgl_aoi_mgr_exit(uintptr_t mgr, uintptr_t u1, uintptr_t u2);
uintptr_t ktgl_aoi_rollover(uintptr_t base, uintptr_t end, uintptr_t ctx);
uintptr_t ktgl_aoi_cons_cmd(uintptr_t cmd_va, uintptr_t ctx, uintptr_t u2);
uintptr_t ktgl_aoi_cons_pkt_begin(uintptr_t ctx, uintptr_t lo, uintptr_t hi);
uintptr_t ktgl_aoi_cons_pkt_end(uintptr_t ctx, uintptr_t u1, uintptr_t u2);
uintptr_t ktgl_aoi_cons_end(uintptr_t ctx, uintptr_t u1, uintptr_t u2);
uintptr_t ktgl_aoi_cons_exit(uintptr_t u0, uintptr_t u1, uintptr_t u2);
uintptr_t ktgl_aoi_post_flush(uintptr_t ctx, uintptr_t u1, uintptr_t u2);
uintptr_t ktgl_aoi_on_pane(uintptr_t pane, uintptr_t u1, uintptr_t u2);
uintptr_t ktgl_aoi_on_submit(uintptr_t snap_node, uintptr_t live_node, uintptr_t mgr);
uintptr_t ktgl_aoi_mgr_enter(uintptr_t mgr, uintptr_t u1, uintptr_t u2);

static int g_mock_enabled = 0;
static uint8_t g_mock_wid = 0;
void dm_nvn_enable_wid(void) { g_mock_enabled++; }
void dm_nvn_set_wid(uint8_t w) { g_mock_wid = w; }

typedef struct {
    _Alignas(16) uint8_t raw[0x140];
    const void *kids[16];
} SynPane;

typedef struct {
    _Alignas(16) uintptr_t vt;
    const void *pane;
} SynNode;

static const KtglAddrBounds g_tb = {.ro_lo = 0x1f000ULL, .ro_hi = 0x28000ULL, .mem_lo = 0x10000ULL, .mem_hi = UINTPTR_MAX};

static void syn_init(SynPane *p, const char *name, const void *parent, const void *sl) {
    memset(p, 0, sizeof *p);
    uintptr_t vt = 0x20000ULL;
    if (!sl) sl = p;
    memcpy(p->raw + 0u, &vt, sizeof vt);
    memcpy(p->raw + KTGL_PANE_OFF_OWNER_SL, &sl, sizeof sl);
    memcpy(p->raw + KTGL_PANE_OFF_NAME, &name, sizeof name);
    memcpy(p->raw + KTGL_PANE_OFF_PARENT, &parent, sizeof parent);
}

static void syn_add_child(SynPane *parent, const SynPane *child) {
    uint32_t cnt = 0;
    memcpy(&cnt, parent->raw + KTGL_PANE_OFF_CHILDCNT, sizeof cnt);
    assert(cnt < 16u);
    parent->kids[cnt++] = child;
    const void *arr = parent->kids;
    memcpy(parent->raw + KTGL_PANE_OFF_CHILDCNT, &cnt, sizeof cnt);
    memcpy(parent->raw + KTGL_PANE_OFF_CHILDREN, &arr, sizeof arr);
}

static void test_all_widget_classes(void) {
    SynPane root, mm, obj, obj_dest, hrt, bat, gge, bg, lo, sp, cmb, wm, menu, leaf;
    syn_init(&root, "NULL_root", NULL, NULL);
    syn_init(&hrt, "common_parts_heart_000", &root, &root);
    syn_init(&gge, "battle_parts_main_gauge_00", &root, &root);
    syn_init(&bat, "battle_parts_main_gauge_zonaubattery_set", &root, &root);
    syn_init(&bg, "battle_parts_main_guide_cmn_action", &root, &root);
    syn_init(&obj, "battle_parts_main_misson_info", &root, &root);
    syn_init(&obj_dest, "btl_icon_marker_destination", &obj, &root);
    syn_init(&mm, "PANE_NULL_MINIMAP_L", &root, &root);
    syn_init(&lo, "battle_parts_main_ko_count", &root, &root);
    syn_init(&sp, "battle_parts_main_vmes", &root, &root);
    syn_init(&cmb, "battle_parts_main_comboflow", &root, &root);
    syn_init(&wm, "btl_gauge_hp_boss_unit", &root, &root);
    syn_init(&menu, "Parts_btn_pause_menu", &root, &root);
    syn_init(&leaf, "PIC_icon_leaf", &mm, &root);

    assert(ktgl_classify_pane(&hrt, &g_tb) == KTGL_WID_HEARTS);
    assert(ktgl_classify_pane(&gge, &g_tb) == KTGL_WID_GAUGE);
    assert(ktgl_classify_pane(&bat, &g_tb) == KTGL_WID_BATTERIES);
    assert(ktgl_classify_pane(&bg, &g_tb) == KTGL_WID_BTN_GUIDE);
    assert(ktgl_classify_pane(&obj, &g_tb) == KTGL_WID_OBJECTIVE);
    assert(ktgl_classify_pane(&obj_dest, &g_tb) == KTGL_WID_OBJECTIVE);
    assert(ktgl_classify_pane(&mm, &g_tb) == KTGL_WID_MINIMAP);
    assert(ktgl_classify_pane(&lo, &g_tb) == KTGL_WID_KO_COUNT);
    assert(ktgl_classify_pane(&sp, &g_tb) == KTGL_WID_SPEECH);
    assert(ktgl_classify_pane(&cmb, &g_tb) == KTGL_WID_COMBO);
    assert(ktgl_classify_pane(&wm, &g_tb) == KTGL_WID_WORLD_MARKER);
    assert(ktgl_classify_pane(&menu, &g_tb) == KTGL_WID_MENU);
    assert(ktgl_classify_pane(&leaf, &g_tb) == KTGL_WID_MINIMAP);

    /* KO-counter keys end the word: the menu's "komoku" text panes are not the KO counter. */
    SynPane ko_a, ko_b, ko_c, ko_d, km_a, km_b, kr;
    syn_init(&ko_a, "tb_text_ko_F_ENG", &root, &root);
    syn_init(&ko_b, "btl_parts_num_ko_flare_00", &root, &root);
    syn_init(&ko_c, "tb_num_kocount_F", &root, &root);
    syn_init(&ko_d, "tb_num_ko", &root, &root);
    syn_init(&km_a, "tb_text_komoku_0", &root, &root);
    syn_init(&km_b, "tb_text_komoku_4_F", &root, &root);
    syn_init(&kr, "tb_num_korok", &root, &root);
    assert(ktgl_classify_pane(&ko_a, &g_tb) == KTGL_WID_KO_COUNT);
    assert(ktgl_classify_pane(&ko_b, &g_tb) == KTGL_WID_KO_COUNT);
    assert(ktgl_classify_pane(&ko_c, &g_tb) == KTGL_WID_KO_COUNT);
    assert(ktgl_classify_pane(&ko_d, &g_tb) == KTGL_WID_KO_COUNT);
    assert(ktgl_classify_pane(&km_a, &g_tb) == KTGL_WID_NONE);
    assert(ktgl_classify_pane(&km_b, &g_tb) == KTGL_WID_NONE);
    assert(ktgl_classify_pane(&kr, &g_tb) == KTGL_WID_MENU);
}

static void test_menu_override_and_fallbacks(void) {
    SynPane sl_allies, win_st, heart_in_menu;
    syn_init(&sl_allies, "NULL_root", NULL, NULL);
    syn_init(&win_st, "WIN_base_status_menu", &sl_allies, &sl_allies);
    syn_init(&heart_in_menu, "common_parts_heart_00", &sl_allies, &sl_allies);
    syn_add_child(&sl_allies, &win_st);
    syn_add_child(&sl_allies, &heart_in_menu);
    assert(ktgl_classify_pane(&heart_in_menu, &g_tb) == KTGL_WID_MENU);

    SynPane sl_wmap, mm_in_map, map_info;
    syn_init(&sl_wmap, "NULL_root", NULL, NULL);
    syn_init(&mm_in_map, "battle_parts_minimap", &sl_wmap, &sl_wmap);
    syn_init(&map_info, "menu_parts_map_info", &sl_wmap, &sl_wmap);
    syn_add_child(&sl_wmap, &mm_in_map);
    syn_add_child(&sl_wmap, &map_info);
    assert(ktgl_classify_pane(&mm_in_map, &g_tb) == KTGL_WID_MENU);

    SynPane sl_obj, t_mis, pic_dest;
    syn_init(&sl_obj, "NULL_root", NULL, NULL);
    syn_init(&t_mis, "TEXT22_mission", &sl_obj, &sl_obj);
    syn_init(&pic_dest, "PIC_base", &sl_obj, &sl_obj);
    syn_add_child(&sl_obj, &t_mis);
    syn_add_child(&sl_obj, &pic_dest);
    assert(ktgl_classify_pane(&pic_dest, &g_tb) == KTGL_WID_OBJECTIVE);

    SynPane sl_hud, h_hud, g_hud, m_hud, generic_hud;
    syn_init(&sl_hud, "NULL_root", NULL, NULL);
    syn_init(&h_hud, "battle_parts_main_player_info", &sl_hud, &sl_hud);
    syn_init(&g_hud, "battle_parts_main_gauge_0", &sl_hud, &sl_hud);
    syn_init(&m_hud, "PANE_NULL_MINIMAP", &sl_hud, &sl_hud);
    syn_init(&generic_hud, "PIC_line", &sl_hud, &sl_hud);
    syn_add_child(&sl_hud, &h_hud);
    syn_add_child(&sl_hud, &g_hud);
    syn_add_child(&sl_hud, &m_hud);
    assert(ktgl_classify_pane(&m_hud, &g_tb) == KTGL_WID_MINIMAP);
    assert(ktgl_classify_pane(&generic_hud, &g_tb) == KTGL_WID_NONE);
}

static void test_node_guard_and_cache(void) {
    KtglWidCache cache = {0};
    SynPane mm;
    syn_init(&mm, "btl_parts_minimap", NULL, NULL);
    SynNode good = {.vt = 0x18000ULL, .pane = &mm};
    SynNode bad_null = {.vt = 0x18000ULL, .pane = NULL};
    SynNode bad_vt = {.vt = 0x18000ULL, .pane = &good};
    assert(ktgl_classify_node(&good, &g_tb, &cache) == KTGL_WID_MINIMAP);
    assert(ktgl_classify_node(&good, &g_tb, &cache) == KTGL_WID_MINIMAP);
    assert(ktgl_classify_node(&bad_null, &g_tb, &cache) == -1);
    assert(ktgl_classify_node(&bad_vt, &g_tb, &cache) == -1);
    assert(ktgl_classify_node((const void *)0x123ULL, &g_tb, &cache) == -1);

    SynPane sl_camp_mm, camp_fort, bad_parent;
    syn_init(&sl_camp_mm, "PANE_NULL_MINIMAP", NULL, NULL);
    syn_init(&camp_fort, "PIC_btl_icon_fort_ally_camp", &sl_camp_mm, &sl_camp_mm);
    assert(ktgl_classify_pane(&camp_fort, &g_tb) == KTGL_WID_MINIMAP);
    SynPane mm_pause;
    syn_init(&mm_pause, "NULL_minimap_pause", &sl_camp_mm, &sl_camp_mm);
    syn_add_child(&sl_camp_mm, &mm_pause);
    assert(ktgl_classify_pane(&camp_fort, &g_tb) == KTGL_WID_MINIMAP);
    SynPane mm_pause_pic;
    syn_init(&mm_pause_pic, "PIC_S000", &mm_pause, &sl_camp_mm);
    assert(ktgl_classify_pane(&mm_pause_pic, &g_tb) == KTGL_WID_MENU);

    SynPane sl_carry, carry_pane, carry_anim;
    syn_init(&sl_carry, "btl_mask_carrying_item_rt", NULL, NULL);
    syn_init(&carry_pane, "btl_win_base_carrying_item", &sl_carry, &sl_carry);
    syn_init(&carry_anim, "special_ability_select_parts_shortcut_item", &sl_carry, &sl_carry);
    syn_add_child(&sl_carry, &carry_pane);
    syn_add_child(&sl_carry, &carry_anim);
    assert(ktgl_classify_pane(&carry_pane, &g_tb) == KTGL_WID_ITEMS);

    /* Review 2C: weapon-seal icon sets use icon_weaponskill + btl_parts_mark_eff, not gauge. */
    SynPane sl_seal, seal_root, seal_eff, impa_g, impa_eff, mark_g;
    syn_init(&sl_seal, "NULL_root", NULL, NULL);
    syn_init(&seal_root, "NULL_icon_weaponskill", &sl_seal, &sl_seal);
    syn_init(&seal_eff, "PIC_btl_parts_mark_eff", &seal_root, &sl_seal);
    syn_init(&impa_g, "btl_gauge_impa", &sl_seal, &sl_seal);
    syn_init(&impa_eff, "btl_parts_mark_eff", &impa_g, &sl_seal);
    syn_init(&mark_g, "btl_parts_mark_g", &sl_seal, &sl_seal);
    assert(ktgl_classify_pane(&seal_eff, &g_tb) == KTGL_WID_MENU);
    assert(ktgl_classify_pane(&impa_eff, &g_tb) == KTGL_WID_GAUGE);
    assert(ktgl_classify_pane(&mark_g, &g_tb) == KTGL_WID_GAUGE);

    syn_init(&bad_parent, "btl_parts_minimap", NULL, NULL);
    uintptr_t bad_p_vt = 0x10000ULL;
    memcpy(bad_parent.raw, &bad_p_vt, sizeof bad_p_vt);
    SynPane child_of_bad;
    syn_init(&child_of_bad, "PIC_icon", &bad_parent, NULL);
    assert(ktgl_classify_pane(&child_of_bad, &g_tb) == KTGL_WID_NONE);

}


static void check_aoi_miss_and_live_toggle(uintptr_t c1, uintptr_t ctx_p) {
    assert(ktgl_aoi_cons_cmd(c1 + 0x400u, ctx_p, 0u) == 0u);
    assert(ktgl_aoi_cons_cmd(c1 + 0x440u, ctx_p, 0u) == 0u);
    assert(ktgl_aoi_cons_cmd(0x91002000ULL, 0x80000000ULL, 0u) == 0u);
    assert(g_mock_wid == KTGL_WID_NONE);
    assert(ktgl_aoi_cons_cmd(c1 + 16u, ctx_p, 0u) == 0u);
    assert(g_mock_wid == KTGL_WID_MINIMAP);
    setenv("DM_KTGL_TAG", "0", 1);
    atomic_fetch_add(&vka_env_gen, 1u);
    assert(ktgl_aoi_cons_cmd(c1 + 16u, ctx_p, 0u) == 0u);
    assert(g_mock_wid == KTGL_WID_NONE);
    unsetenv("DM_KTGL_TAG");
    atomic_fetch_add(&vka_env_gen, 1u);
    assert(ktgl_aoi_cons_cmd(c1 + 16u, ctx_p, 0u) == 0u);
    assert(g_mock_wid == KTGL_WID_MINIMAP);
}

static void check_aoi_consumer_replay(
    uintptr_t c1, uintptr_t c2, uintptr_t ctx_p, uint8_t *ctx_buf) {
    assert(ktgl_aoi_cons_cmd(c1 + 16u, ctx_p, 0u) == 0u);
    assert(g_mock_wid == KTGL_WID_MINIMAP);
    uint32_t batch_nz = 8u;
    memcpy(ctx_buf + 0x28e8u, &batch_nz, sizeof batch_nz);
    assert(ktgl_aoi_cons_cmd(c1 + 80u, ctx_p, 0u) == ctx_p);
    assert(g_mock_wid == KTGL_WID_MINIMAP);
    ktgl_aoi_post_flush(ctx_p, 0u, 0u);
    assert(g_mock_wid == KTGL_WID_OBJECTIVE);
    batch_nz = 0u;
    memcpy(ctx_buf + 0x28e8u, &batch_nz, sizeof batch_nz);
    assert(ktgl_aoi_cons_cmd(c2 + 8u, ctx_p, 0u) == 0u);
    assert(g_mock_wid == KTGL_WID_OBJECTIVE);
    assert(ktgl_aoi_cons_end(0u, 0u, 0u) == 0u);
    assert(g_mock_wid == KTGL_WID_NONE);
}

static void test_producer_consumer_and_flush_order(void) {
    ktgl_ui_tag_reset_for_test();
    ktgl_ui_tag_set_bounds_for_test(g_tb);
    g_mock_wid = 0u;

    _Alignas(16) uint8_t mgr_buf[0x80] = {0}, ctx_buf[0x4100] = {0}, chunk1[256], chunk2[256];

    uintptr_t ctx_p = (uintptr_t)ctx_buf;
    uintptr_t c1 = (uintptr_t)chunk1, c2 = (uintptr_t)chunk2;
    memcpy(mgr_buf + 0x40u, &ctx_p, sizeof ctx_p);
    memcpy(ctx_buf + 0x4048u, &c1, sizeof c1);
    memcpy(ctx_buf + 0x4050u, &c1, sizeof c1);

    SynPane p_mm, p_obj;
    syn_init(&p_mm, "PANE_NULL_MINIMAP", NULL, NULL);
    syn_init(&p_obj, "btl_win_base_mission_info", NULL, NULL);
    SynNode n_mm = {.vt = 0x20000ULL, .pane = &p_mm}, n_obj = {.vt = 0x20000ULL, .pane = &p_obj}, n_bad = {.vt = 0x10000ULL, .pane = NULL};
    const void *ps[2] = {&p_mm, &p_obj}, *ns[2] = {&n_mm, &n_obj};
    for (size_t i = 0; i < 2u; i++) {
        ktgl_aoi_on_pane((uintptr_t)ps[i], 0u, 0u);
        ktgl_aoi_on_submit((uintptr_t)ns[i], (uintptr_t)ns[i], (uintptr_t)mgr_buf);
    }

    /* Unsubmitted / invalid node must stay wid == 0 */
    assert(ktgl_aoi_pre_node((uintptr_t)&n_bad, (uintptr_t)mgr_buf, 0u) == 0u);
    assert(g_mock_wid == 0u);

    /* Minimap starts at chunk1 + 0, batch == 0 switches immediately */
    assert(ktgl_aoi_pre_node((uintptr_t)&n_mm, (uintptr_t)mgr_buf, 0u) == 0u);
    assert(g_mock_wid == KTGL_WID_MINIMAP);

    /* Advance cursor into chunk1 by 64 bytes, then switch to Objective with batch != 0 */
    uintptr_t c1_64 = c1 + 64u;
    uint32_t batch_nz = 12u;
    memcpy(ctx_buf + 0x4048u, &c1_64, sizeof c1_64);
    memcpy(ctx_buf + 0x28e8u, &batch_nz, sizeof batch_nz);
    assert(ktgl_aoi_pre_node((uintptr_t)&n_obj, (uintptr_t)mgr_buf, 0u) == ctx_p);
    assert(g_mock_wid == KTGL_WID_MINIMAP); /* MUST NOT switch before flush! */
    ktgl_aoi_post_flush(ctx_p, 0u, 0u);
    assert(g_mock_wid == KTGL_WID_OBJECTIVE);

    /* Mid-node chunk rollover [c1, c1 + 128), then continue into chunk2 [c2, c2 + 32) */
    uintptr_t c1_128 = c1 + 128u;
    memcpy(ctx_buf + 0x4048u, &c1_128, sizeof c1_128);
    assert(ktgl_aoi_rollover(c1, c1_128, ctx_p) == 0u);
    uintptr_t c2_32 = c2 + 32u;
    memcpy(ctx_buf + 0x4050u, &c2, sizeof c2);
    memcpy(ctx_buf + 0x4048u, &c2_32, sizeof c2_32);
    batch_nz = 0u;
    memcpy(ctx_buf + 0x28e8u, &batch_nz, sizeof batch_nz);
    assert(ktgl_aoi_mgr_exit((uintptr_t)mgr_buf, 0u, 0u) == 0u);
    assert(g_mock_wid == KTGL_WID_NONE);

    check_aoi_consumer_replay(c1, c2, ctx_p, ctx_buf);
    check_aoi_miss_and_live_toggle(c1, ctx_p);
}

static void test_aoi_binary_patch_install(void) {
    const char *p = getenv("DM_AOI_MAIN_BIN");
    FILE *fp = (p && p[0]) ? fopen(p, "rb") : NULL;
    if (!fp) {
        printf("  skipping real main.patched.bin check (missing)\n");
        return;
    }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    assert(sz > 0x2700000L);
    uint8_t *buf = malloc((size_t)sz);
    assert(buf && fread(buf, 1u, (size_t)sz, fp) == (size_t)sz);
    fclose(fp);

    const uint64_t text_end = 0x1ed71c0u, ro_off = 0x1ed8000u, data_off = 0x26af000u, mod_end = data_off + 0x8000u;
    const char *bid = "659cb288b5c5b2b49853add616c0d5bf";
    if ((uint64_t)sz < mod_end) { buf = realloc(buf, (size_t)mod_end); assert(buf); memset(buf + sz, 0, (size_t)(mod_end - (uint64_t)sz)); }
    uint32_t m0 = (uint32_t)(ro_off + 0x4cu); int32_t bss_rel = (int32_t)((int64_t)(data_off + 0x4000u) - (int64_t)m0);
    memcpy(buf + 4u, &m0, sizeof m0);
    memcpy(buf + m0, "MOD0", 4u);
    memcpy(buf + m0 + 8u, &bss_rel, sizeof bss_rel);

    /* Negative control 1: non-zero byte in slack must fail safely without patching */
    g_mock_enabled = 0;
    buf[0x1ed7200u] = 1u;
    assert(ktgl_ui_tag_install_raw(buf, text_end, ro_off, data_off, mod_end, bid) == -1);
    assert(g_mock_enabled == 0);
    assert(ktgl_rd32(buf, 0xb2e634u) == 0xf9400b20u);
    buf[0x1ed7200u] = 0u;

    /* Negative control 2: corrupted OLD_INSN must fail safely without patching */
    ktgl_wr32(buf, 0x725288u, 0xd503201fu);
    assert(ktgl_ui_tag_install_raw(buf, text_end, ro_off, data_off, mod_end, bid) == -1);
    assert(g_mock_enabled == 0);
    assert(ktgl_rd32(buf, 0xb2e634u) == 0xf9400b20u);
    ktgl_wr32(buf, 0x725288u, 0x79400108u);

    /* Positive control: clean install patches all 38 sites within 3.5 KiB slack */
    assert(ktgl_ui_tag_install_raw(buf, text_end, ro_off, data_off, mod_end, bid) == 1);
    assert(g_mock_enabled == 1);
    static const uint64_t sites[38] = {
        0xae7ba8u, 0xae9b20u, 0xaf1e94u, 0xaf453cu, 0xaf52b8u, 0xaf56e4u, 0xaf5de4u, 0xafe498u,
        0xb04548u, 0xb04d08u, 0xb05710u, 0xb057e8u, 0xb07c58u, 0xb0bfe0u, 0x245974u, 0xb2775cu,
        0xb2e4d8u, 0xb34ee8u, 0xb2e634u, 0xb2e6ecu, 0xb2ec28u, 0xb2ed14u, 0xb2f658u, 0xb31084u,
        0xb32084u, 0xb325b0u, 0xb32888u, 0xb33f3cu, 0xb34fb8u, 0xb34e58u, 0xb37b20u, 0x70bb34u,
        0x7234f4u, 0x725280u, 0x725288u, 0x7252acu, 0x7252ccu, 0x7253e4u,
    };
    for (size_t i = 0; i < 38u; i++) {
        uint32_t w = ktgl_rd32(buf, sites[i]);
        assert((w & 0xfc000000u) == 0x14000000u);
        int32_t simm = ((int32_t)(w << 6)) >> 6;
        uint64_t t = (uint64_t)((int64_t)sites[i] + ((int64_t)simm * 4LL));
        assert(t >= 0x1ed7200u && t < 0x1ed8000u);
    }
    for (uint64_t o = 0x1ed7200u + 3400u; o < 0x1ed8000u; o++) assert(buf[o] == 0u);
    free(buf);
}

static void test_relro_vtable_and_mod0_bss(void) {
    const uint64_t ro_off = 0x2000u, data_off = 0x4000u;
    const uint64_t bss_off = 0x8000u, mod_end = 0xc000u;
    uint8_t *mod = calloc(1u, mod_end);
    assert(mod);
    uint32_t m0 = (uint32_t)(ro_off + 0x4cu);
    int32_t bss_rel = (int32_t)((int64_t)bss_off - (int64_t)m0);
    memcpy(mod + 4u, &m0, sizeof m0);
    memcpy(mod + m0, "MOD0", 4u);
    memcpy(mod + m0 + 8u, &bss_rel, sizeof bss_rel);
    assert(ktgl_nso_vtable_end(mod, data_off, mod_end) == bss_off);
    KtglAddrBounds b = {
        .ro_lo = (uintptr_t)mod + ro_off, .ro_hi = (uintptr_t)mod + bss_off,
        .mod_lo = (uintptr_t)mod, .mod_hi = (uintptr_t)mod + mod_end,
    };
    uintptr_t relro_vt = (uintptr_t)mod + data_off + 0x1000u;
    uintptr_t bss_vt = (uintptr_t)mod + bss_off + 0x100u;
    char *mm_s = (char *)(mod + bss_off + 0x180u);
    char *obj_s = (char *)(mod + bss_off + 0x200u);
    memcpy(mm_s, "btl_parts_minimap", 18u);
    memcpy(obj_s, "battle_parts_main_misson_info", 30u);

    SynPane *p_mm = (SynPane *)(mod + bss_off + 0x400u);
    SynPane *p_bss = (SynPane *)(mod + bss_off + 0x700u);
    SynPane *p_bad = (SynPane *)(mod + bss_off + 0xa00u);
    syn_init(p_mm, mm_s, NULL, NULL);
    memcpy(p_mm->raw, &relro_vt, sizeof relro_vt);
    SynNode *n_mm = (SynNode *)(mod + bss_off + 0xd00u);
    n_mm->vt = 0x18000ULL; n_mm->pane = p_mm;
    assert(ktgl_classify_node(n_mm, &b, NULL) == KTGL_WID_MINIMAP);

    syn_init(p_bss, obj_s, NULL, NULL);
    memcpy(p_bss->raw, &relro_vt, sizeof relro_vt);
    SynNode *n_obj = (SynNode *)(mod + bss_off + 0xd20u);
    n_obj->vt = 0x18000ULL; n_obj->pane = p_bss;
    assert(ktgl_classify_node(n_obj, &b, NULL) == KTGL_WID_OBJECTIVE);

    syn_init(p_bad, mm_s, NULL, NULL);
    memcpy(p_bad->raw, &bss_vt, sizeof bss_vt);
    SynNode *n_bad = (SynNode *)(mod + bss_off + 0xd40u);
    n_bad->vt = 0x18000ULL; n_bad->pane = p_bad;
    assert(ktgl_classify_node(n_bad, &b, NULL) == -1);
    free(mod);
}

static void test_prot_none_guard(void) {
    KtglWidCache cache = {0};
    void *guard = mmap(NULL, 8192u, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(guard != MAP_FAILED);

    SynNode n_guard_pane = {.vt = 0x18000ULL, .pane = guard};
    assert(ktgl_classify_node(&n_guard_pane, &g_tb, &cache) == -1);
    assert(ktgl_classify_node(guard, &g_tb, &cache) == -1);

    SynPane p_bad_par, p_bad_name, p_bad_kids;
    syn_init(&p_bad_par, "btl_parts_minimap", guard, NULL);
    assert(ktgl_classify_pane(&p_bad_par, &g_tb) == KTGL_WID_MINIMAP);

    syn_init(&p_bad_name, "tmp", NULL, NULL);
    memcpy(p_bad_name.raw + KTGL_PANE_OFF_NAME, &guard, sizeof guard);
    assert(ktgl_classify_pane(&p_bad_name, &g_tb) == KTGL_WID_NONE);

    syn_init(&p_bad_kids, "NULL_root", NULL, NULL);
    uint32_t cnt4 = 4u;
    memcpy(p_bad_kids.raw + KTGL_PANE_OFF_CHILDCNT, &cnt4, sizeof cnt4);
    memcpy(p_bad_kids.raw + KTGL_PANE_OFF_CHILDREN, &guard, sizeof guard);
    assert(ktgl_classify_pane(&p_bad_kids, &g_tb) == KTGL_WID_NONE);

    assert(munmap(guard, 8192u) == 0);
}

static void test_deferred_sub_ctx_and_combo_bg(void) {
    ktgl_ui_tag_reset_for_test();
    ktgl_ui_tag_set_bounds_for_test(g_tb);
    g_mock_wid = 0u;
    _Alignas(16) uint8_t mgr_buf[0x80], prod_buf[0x4100], cons_buf[0x4100], chunk[256];
    memset(mgr_buf, 0, sizeof mgr_buf);
    memset(prod_buf, 0, sizeof prod_buf);
    memset(cons_buf, 0, sizeof cons_buf);
    uintptr_t prod_p = (uintptr_t)prod_buf, sub_raw = (uintptr_t)cons_buf - 0x10u;
    uintptr_t cons_p = (uintptr_t)cons_buf, c0 = (uintptr_t)chunk, c64 = c0 + 64u, c128 = c0 + 128u;
    uint32_t bad_batch = 99u;
    memcpy(mgr_buf + 0x40u, &prod_p, sizeof prod_p);
    memcpy(prod_buf + 0x3fb0u, &sub_raw, sizeof sub_raw);
    memcpy(prod_buf + 0x28e8u, &bad_batch, sizeof bad_batch);
    memcpy(prod_buf + 0x4050u, &c0, sizeof c0);
    memcpy(prod_buf + 0x4048u, &c0, sizeof c0);

    SynPane sl_c, p_cbg, p_ico;
    syn_init(&sl_c, "battle_parts_main_comboflow", NULL, NULL);
    syn_init(&p_cbg, "WIN_comboflow_square_0000", &sl_c, &sl_c);
    syn_init(&p_ico, "PIC_btl_parts_comboflow", &sl_c, &sl_c);
    SynNode n_cbg = {.vt = 0x20000ULL, .pane = &p_cbg};
    SynNode n_ico = {.vt = 0x20000ULL, .pane = &p_ico};
    ktgl_aoi_on_pane((uintptr_t)&p_cbg, 0u, 0u);
    ktgl_aoi_on_submit((uintptr_t)&n_cbg, (uintptr_t)&n_cbg, (uintptr_t)mgr_buf);
    ktgl_aoi_on_pane((uintptr_t)&p_ico, 0u, 0u);
    ktgl_aoi_on_submit((uintptr_t)&n_ico, (uintptr_t)&n_ico, (uintptr_t)mgr_buf);
    assert(ktgl_aoi_pre_node((uintptr_t)&n_cbg, (uintptr_t)mgr_buf, 0u) == 0u);
    assert(g_mock_wid == 0u);
    memcpy(prod_buf + 0x4048u, &c64, sizeof c64);
    assert(ktgl_aoi_pre_node((uintptr_t)&n_ico, (uintptr_t)mgr_buf, 0u) == 0u);
    assert(g_mock_wid == 0u);
    memcpy(prod_buf + 0x4048u, &c128, sizeof c128);
    assert(ktgl_aoi_mgr_exit((uintptr_t)mgr_buf, 0u, 0u) == 0u);
    assert(g_mock_wid == 0u);

    assert(ktgl_aoi_cons_cmd(c0 + 8u, cons_p, 0u) == 0u);
    assert(g_mock_wid == (KTGL_WID_COMBO | KTGL_WID_FLAG_BG));
    assert(ktgl_aoi_cons_cmd(c64 + 8u, cons_p, 0u) == 0u);
    assert(g_mock_wid == KTGL_WID_COMBO);
    assert(ktgl_aoi_cons_end(0u, 0u, 0u) == 0u);
    assert(g_mock_wid == 0u);

    KtglWidCache sc = {0};
    SynPane mut;
    syn_init(&mut, "btl_parts_minimap", NULL, NULL);
    SynNode n_mut = {.vt = 0x20000ULL, .pane = &mut};
    assert(ktgl_classify_node(&n_mut, &g_tb, &sc) == KTGL_WID_MINIMAP);
    const char *obj_nm = "battle_parts_main_misson_info";
    memcpy(mut.raw + KTGL_PANE_OFF_NAME, &obj_nm, sizeof obj_nm);
    assert(ktgl_classify_node(&n_mut, &g_tb, &sc) == KTGL_WID_OBJECTIVE);
}

static void test_snapshot_map_and_cur_pane_priority(void) {
    ktgl_ui_tag_reset_for_test();
    ktgl_ui_tag_set_bounds_for_test(g_tb);
    g_mock_wid = 0u;
    _Alignas(16) uint8_t mgr[0x80] = {0}, prod[0x4100] = {0}, cons[0x4100] = {0}, chunk[512];
    _Alignas(16) uint8_t sub_obj[0x90] = {0};
    uintptr_t pp = (uintptr_t)prod, sr = (uintptr_t)cons - 0x10u, cp = (uintptr_t)cons, c0 = (uintptr_t)chunk;
    memcpy(mgr + 0x40u, &pp, sizeof pp);
    memcpy(prod + 0x3fb0u, &sr, sizeof sr);
    memcpy(prod + 0x4050u, &c0, sizeof c0);
    memcpy(prod + 0x4048u, &c0, sizeof c0);

    SynPane p_mm, p_obj, p_inv;
    syn_init(&p_mm, "PANE_NULL_MINIMAP", NULL, NULL);
    syn_init(&p_obj, "btl_win_base_mission_info", NULL, NULL);
    syn_init(&p_inv, "btl_mask_invisible", NULL, NULL);
    uintptr_t vt = 0x20000ULL;
    const void *p_obj_ptr = &p_obj;
    memcpy(sub_obj, &vt, sizeof vt);
    _Alignas(16) uint8_t glyph_obj[0x90] = {0};
    memcpy(sub_obj + 0x78u, &p_obj_ptr, sizeof p_obj_ptr);
    memcpy(glyph_obj + 0x78u, &p_obj_ptr, sizeof p_obj_ptr);
    SynNode live_sub = {.vt = 0x20000ULL, .pane = sub_obj};
    SynNode live_gly = {.vt = 0x20000ULL, .pane = glyph_obj};
    SynNode snap_mm = {.vt = 0x20000ULL, .pane = NULL};
    SynNode snap_fb = {.vt = 0x20000ULL, .pane = NULL};
    SynNode snap_vis = {.vt = 0x20000ULL, .pane = NULL};
    SynNode unsub = {.vt = 0x20000ULL, .pane = &p_obj};

    /* 1. cur_pane wins over non-Pane sub-object; 2. live_node fallback when cur_pane == NULL */
    ktgl_aoi_on_pane((uintptr_t)&p_mm, 0u, 0u);
    ktgl_aoi_on_submit((uintptr_t)&snap_mm, (uintptr_t)&live_sub, (uintptr_t)mgr);
    ktgl_aoi_on_pane(0u, 0u, 0u);
    ktgl_aoi_on_submit((uintptr_t)&snap_fb, (uintptr_t)&live_gly, (uintptr_t)mgr);
    /* 3. Invisible MaskPane early-returning without submit must not poison next visible Pane */
    ktgl_aoi_on_pane((uintptr_t)&p_inv, 0u, 0u);
    ktgl_aoi_on_pane((uintptr_t)&p_obj, 0u, 0u);
    ktgl_aoi_on_submit((uintptr_t)&snap_vis, (uintptr_t)&live_sub, (uintptr_t)mgr);

    ktgl_aoi_mgr_enter((uintptr_t)mgr, 0u, 0u);
    const SynNode *seq[4] = {&snap_mm, &snap_fb, &snap_vis, &unsub};
    for (size_t i = 0; i < 4u; i++) {
        uintptr_t cur = c0 + i * 64u;
        memcpy(prod + 0x4048u, &cur, sizeof cur);
        assert(ktgl_aoi_pre_node((uintptr_t)seq[i], 0xdeadULL, 0u) == 0u);
    }
    uintptr_t end_c = c0 + 256u;
    memcpy(prod + 0x4048u, &end_c, sizeof end_c);
    assert(ktgl_aoi_mgr_exit(0xdeadULL, 0u, 0u) == 0u);

    assert(ktgl_aoi_cons_cmd(c0 + 8u, cp, 0u) == 0u && g_mock_wid == KTGL_WID_MINIMAP);
    assert(ktgl_aoi_cons_cmd(c0 + 72u, cp, 0u) == 0u && g_mock_wid == KTGL_WID_OBJECTIVE);
    assert(ktgl_aoi_cons_cmd(c0 + 136u, cp, 0u) == 0u && g_mock_wid == KTGL_WID_OBJECTIVE);
    assert(ktgl_aoi_cons_cmd(c0 + 200u, cp, 0u) == 0u && g_mock_wid == KTGL_WID_NONE);
}

typedef struct {
    const SynPane *pane;
    SynNode *nodes;
    size_t count;
} WorkerJobArgs;

static void *worker_submit_fn(void *arg) {
    WorkerJobArgs *a = (WorkerJobArgs *)arg;
    for (size_t i = 0; i < a->count; i++) {
        ktgl_aoi_on_pane((uintptr_t)a->pane, 0u, 0u);
        ktgl_aoi_on_submit((uintptr_t)&a->nodes[i], (uintptr_t)&a->nodes[i], 0u);
    }
    return NULL;
}

static void test_parallel_worker_submit(void) {
    ktgl_ui_tag_reset_for_test();
    ktgl_ui_tag_set_bounds_for_test(g_tb);
    g_mock_wid = 0u;
    _Alignas(16) uint8_t mgr[0x80] = {0}, prod[0x4100] = {0}, cons[0x4100] = {0}, chunk[512];
    uintptr_t pp = (uintptr_t)prod, sr = (uintptr_t)cons - 0x10u, cp = (uintptr_t)cons, c0 = (uintptr_t)chunk;
    memcpy(mgr + 0x40u, &pp, sizeof pp);
    memcpy(prod + 0x3fb0u, &sr, sizeof sr);
    memcpy(prod + 0x4050u, &c0, sizeof c0);
    memcpy(prod + 0x4048u, &c0, sizeof c0);

    SynPane panes[4];
    static const char *const names[4] = {
        "common_parts_heart_000", "battle_parts_main_gauge_zonaubattery_set",
        "battle_parts_main_misson_info", "PANE_NULL_MINIMAP",
    };
    static const uint8_t wids[4] = {
        KTGL_WID_HEARTS, KTGL_WID_BATTERIES, KTGL_WID_OBJECTIVE, KTGL_WID_MINIMAP,
    };
    SynNode nodes[4][64];
    WorkerJobArgs args[4];
    pthread_t th[4];
    memset(nodes, 0, sizeof nodes);
    for (size_t t = 0; t < 4u; t++) {
        syn_init(&panes[t], names[t], NULL, NULL);
        for (size_t j = 0; j < 64u; j++) nodes[t][j].vt = 0x20000ULL;
        args[t] = (WorkerJobArgs){&panes[t], nodes[t], 64u};
        assert(pthread_create(&th[t], NULL, worker_submit_fn, &args[t]) == 0);
    }
    for (size_t t = 0; t < 4u; t++) assert(pthread_join(th[t], NULL) == 0);

    ktgl_aoi_mgr_enter((uintptr_t)mgr, 0u, 0u);
    for (size_t t = 0; t < 4u; t++) {
        uintptr_t cur = c0 + t * 64u;
        memcpy(prod + 0x4048u, &cur, sizeof cur);
        for (size_t j = 0; j < 64u; j++) {
            assert(ktgl_aoi_pre_node((uintptr_t)&nodes[t][j], 0xdeadULL, 0u) == 0u);
        }
    }
    uintptr_t end_c = c0 + 256u;
    memcpy(prod + 0x4048u, &end_c, sizeof end_c);
    assert(ktgl_aoi_mgr_exit(0xdeadULL, 0u, 0u) == 0u);
    for (size_t t = 0; t < 4u; t++) {
        assert(ktgl_aoi_cons_cmd(c0 + t * 64u + 8u, cp, 0u) == 0u && g_mock_wid == wids[t]);
    }
}

static void test_real_screenlayout_and_corpus_fixes(void) {
    _Alignas(16) uint8_t real_sl[0x380] = {0};
    uintptr_t vt = 0x20000ULL;
    SynPane r_pane, d1, d2, mis_txt, boss_hp, bar_pic;
    const void *rp = &r_pane;
    memcpy(real_sl, &vt, sizeof vt);
    memcpy(real_sl + KTGL_SL_OFF_ROOT_PANE, &rp, sizeof rp);
    syn_init(&r_pane, "NULL_top", NULL, real_sl);
    syn_init(&d1, "NULL_box", &r_pane, real_sl);
    syn_init(&d2, "NULL_sub", &d1, real_sl);
    syn_init(&mis_txt, "TEXT22_mission", &d2, real_sl);
    syn_init(&boss_hp, "btl_gauge_hp_boss_unit", &r_pane, real_sl);
    syn_init(&bar_pic, "PIC_bar", &r_pane, real_sl);
    syn_add_child(&r_pane, &d1);
    syn_add_child(&d1, &d2);
    syn_add_child(&d2, &mis_txt);
    syn_add_child(&r_pane, &boss_hp);
    syn_add_child(&r_pane, &bar_pic);
    assert(ktgl_classify_pane(&bar_pic, &g_tb) == KTGL_WID_OBJECTIVE);

    SynPane gp1, gp2, gls, gac, bg_cmn;
    syn_init(&gp1, "btl_parts_guide_action", NULL, NULL);
    syn_init(&gp2, "btl_parts_guide_btn_action", NULL, NULL);
    syn_init(&gls, "PIC_icon_linkstrike", NULL, NULL);
    syn_init(&gac, "action_gacha_hint", NULL, NULL);
    syn_init(&bg_cmn, "battle_parts_main_guide_cmn_action", NULL, NULL);
    assert(ktgl_classify_pane(&gp1, &g_tb) == KTGL_WID_WORLD_MARKER);
    assert(ktgl_classify_pane(&gp2, &g_tb) == KTGL_WID_WORLD_MARKER);
    assert(ktgl_classify_pane(&gls, &g_tb) == KTGL_WID_WORLD_MARKER);
    assert(ktgl_classify_pane(&gac, &g_tb) == KTGL_WID_WORLD_MARKER);
    assert(ktgl_classify_pane(&bg_cmn, &g_tb) == KTGL_WID_BTN_GUIDE);

    SynPane ally_sl, ally_sync, item_sl, item_win;
    syn_init(&ally_sl, "btl_plt_player_other", NULL, NULL);
    syn_init(&ally_sync, "btl_gauge_sync_00", &ally_sl, &ally_sl);
    syn_init(&item_sl, "btl_win_base_carrying_item", NULL, NULL);
    syn_init(&item_win, "special_ability_select_parts_win_item_info", &item_sl, &item_sl);
    assert(ktgl_classify_pane(&ally_sync, &g_tb) == KTGL_WID_ALLIES);
    assert(ktgl_classify_pane(&item_win, &g_tb) == KTGL_WID_ITEMS);
}

typedef struct { uintptr_t cmd, ctx; } ConsWorkerArg;

static void *cons_worker_part1(void *arg) {
    ConsWorkerArg *a = (ConsWorkerArg *)arg;
    assert(ktgl_aoi_cons_cmd(a->cmd, a->ctx, 0u) == 0u);
    assert(g_mock_wid == KTGL_WID_OBJECTIVE);
    assert(ktgl_aoi_cons_exit(0u, 0u, 0u) == 0u && g_mock_wid == KTGL_WID_NONE);
    return NULL;
}

static void *cons_worker_part2(void *arg) {
    ConsWorkerArg *a = (ConsWorkerArg *)arg;
    assert(ktgl_aoi_cons_pkt_begin(a->ctx, a->cmd - 8u, a->cmd + 56u) == 0u);
    assert(g_mock_wid == KTGL_WID_NONE);
    assert(ktgl_aoi_cons_cmd(a->cmd, a->ctx, 0u) == a->ctx);
    assert(g_mock_wid == KTGL_WID_NONE);
    ktgl_aoi_post_flush(a->ctx, 0u, 0u);
    assert(g_mock_wid == KTGL_WID_MINIMAP);
    assert(ktgl_aoi_cons_pkt_end(a->ctx, 0u, 0u) == a->ctx);
    assert(g_mock_wid == KTGL_WID_MINIMAP);
    ktgl_aoi_post_flush(a->ctx, 0u, 0u);
    assert(g_mock_wid == KTGL_WID_NONE);
    assert(ktgl_aoi_cons_cmd(a->cmd, a->ctx, 0u) == 0u && g_mock_wid == KTGL_WID_NONE);
    assert(ktgl_aoi_cons_end(a->ctx, 0u, 0u) == 0u && g_mock_wid == KTGL_WID_NONE);
    return NULL;
}

static void test_consumer_cross_thread_and_nested_mgr(void) {
    ktgl_ui_tag_reset_for_test();
    ktgl_ui_tag_set_bounds_for_test(g_tb);
    g_mock_wid = 0u;
    _Alignas(16) uint8_t m1[0x80] = {0}, m2[0x80] = {0}, prod[0x4100] = {0}, cons[0x4100] = {0}, ch[256];
    uintptr_t pp = (uintptr_t)prod, sr = (uintptr_t)cons - 0x10u, cp = (uintptr_t)cons, c0 = (uintptr_t)ch;
    memcpy(m1 + 0x40u, &pp, sizeof pp);
    memcpy(m2 + 0x40u, &pp, sizeof pp);
    memcpy(prod + 0x3fb0u, &sr, sizeof sr);
    memcpy(prod + 0x4050u, &c0, sizeof c0);
    memcpy(prod + 0x4048u, &c0, sizeof c0);
    SynPane p_obj, p_mm;
    syn_init(&p_obj, "battle_parts_main_misson_info", NULL, NULL);
    syn_init(&p_mm, "PANE_NULL_MINIMAP", NULL, NULL);
    SynNode n_obj = {.vt = 0x20000ULL, .pane = &p_obj}, n_mm = {.vt = 0x20000ULL, .pane = &p_mm};
    ktgl_aoi_on_pane((uintptr_t)&p_obj, 0u, 0u); ktgl_aoi_on_submit((uintptr_t)&n_obj, (uintptr_t)&n_obj, 0u);
    ktgl_aoi_on_pane((uintptr_t)&p_mm, 0u, 0u); ktgl_aoi_on_submit((uintptr_t)&n_mm, (uintptr_t)&n_mm, 0u);

    ktgl_aoi_mgr_enter((uintptr_t)m1, 0u, 0u);
    uintptr_t c1 = c0 + 64u, c2 = c0 + 128u;
    memcpy(prod + 0x4048u, &c0, sizeof c0);
    assert(ktgl_aoi_pre_node((uintptr_t)&n_obj, (uintptr_t)m1, 0u) == 0u);
    ktgl_aoi_mgr_enter((uintptr_t)m2, 0u, 0u);
    memcpy(prod + 0x4048u, &c1, sizeof c1);
    assert(ktgl_aoi_pre_node((uintptr_t)&n_mm, (uintptr_t)m2, 0u) == 0u);
    assert(ktgl_aoi_mgr_exit((uintptr_t)m2, 0u, 0u) == 0u);
    memcpy(prod + 0x4048u, &c2, sizeof c2);
    assert(ktgl_aoi_mgr_exit((uintptr_t)m1, 0u, 0u) == 0u);

    pthread_t th1, th2;
    ConsWorkerArg a1 = {c0 + 8u, cp}, a2 = {c1 + 8u, cp};
    assert(pthread_create(&th1, NULL, cons_worker_part1, &a1) == 0 && pthread_join(th1, NULL) == 0);
    uint32_t batch_nz = 4u;
    memcpy(cons + 0x28e8u, &batch_nz, sizeof batch_nz);
    assert(pthread_create(&th2, NULL, cons_worker_part2, &a2) == 0 && pthread_join(th2, NULL) == 0);
}

static void test_classifier_depth5_btn_guide_and_snap_hash(void) {
    _Alignas(16) uint8_t sl[0x380] = {0};
    uintptr_t vt = 0x20000ULL;
    SynPane r_p, d1, d2, d3, d4, ally_p, leaf;
    const void *rp = &r_p;
    memcpy(sl, &vt, sizeof vt);
    memcpy(sl + KTGL_SL_OFF_ROOT_PANE, &rp, sizeof rp);
    syn_init(&r_p, "NULL_root", NULL, sl);
    syn_init(&d1, "NULL_l1", &r_p, sl);
    syn_init(&d2, "NULL_l2", &d1, sl);
    syn_init(&d3, "NULL_l3", &d2, sl);
    syn_init(&d4, "NULL_l4", &d3, sl);
    syn_init(&ally_p, "btl_plt_player_2p", &d4, sl);
    syn_init(&leaf, "PIC_00", &r_p, sl);
    syn_add_child(&r_p, &d1); syn_add_child(&d1, &d2); syn_add_child(&d2, &d3);
    syn_add_child(&d3, &d4); syn_add_child(&d4, &ally_p); syn_add_child(&r_p, &leaf);
    assert(ktgl_classify_pane(&leaf, &g_tb) == KTGL_WID_ALLIES);

    static const char *const bg_toks[] = {
        "TEXT24_keyguide_flare_0", "btl_parts_action_arrow_l", "btl_parts_action_x2",
        "icon_keyguide_longpress", "icon_keyguide_mash", "tb_text_operation_00",
    };
    for (size_t i = 0; i < sizeof bg_toks / sizeof bg_toks[0]; i++) {
        SynPane p; syn_init(&p, bg_toks[i], NULL, NULL);
        assert(ktgl_classify_pane(&p, &g_tb) == KTGL_WID_BTN_GUIDE);
    }
    SynPane sp_sc, mr, c1, c2, cm;
    syn_init(&sp_sc, "still_speech_scale", NULL, NULL);
    assert(ktgl_classify_pane(&sp_sc, &g_tb) == KTGL_WID_NONE);
    _Alignas(16) uint8_t msl[0x380] = {0}, sub_sl[0x380] = {0};
    const void *mrp = &mr;
    memcpy(msl, &vt, sizeof vt); memcpy(msl + KTGL_SL_OFF_ROOT_PANE, &mrp, sizeof mrp);
    memcpy(sub_sl, &vt, sizeof vt); memcpy(sub_sl + KTGL_SL_OFF_ROOT_PANE, &mrp, sizeof mrp);
    syn_init(&mr, "NULL_root", NULL, msl);
    syn_init(&c1, "player_info", &mr, msl);
    syn_init(&cm, "pause_menu", &mr, msl);
    syn_add_child(&mr, &c1); syn_add_child(&mr, &cm);
    assert(ktgl_is_master_hud(sub_sl, &g_tb) == 0 && ktgl_root_sl_has_menu(sub_sl, &g_tb) == 1);
    syn_init(&c2, "MINIMAP", &mr, msl);
    syn_add_child(&mr, &c2);
    assert(ktgl_is_master_hud(msl, &g_tb) == 1 && ktgl_root_sl_has_menu(msl, &g_tb) == 0);

    KtglSnapMap *sm = calloc(1u, sizeof *sm);
    assert(sm != NULL);
    uintptr_t stride = (uintptr_t)KTGL_SNAP_SETS * 16u;
    uint32_t sets[16];
    for (size_t i = 0; i < 16u; i++) {
        const void *k = (const void *)(0x100000ULL + i * stride);
        sets[i] = ktgl_snap_set_idx((uint64_t)(uintptr_t)k);
        ktgl_snap_put(sm, k, (uint8_t)((i % 14u) + 1u));
    }
    int diff = 0;
    for (size_t i = 1; i < 16u; i++) if (sets[i] != sets[0]) diff++;
    assert(diff >= 8);
    for (size_t i = 0; i < 16u; i++) {
        const void *k = (const void *)(0x100000ULL + i * stride);
        assert(ktgl_snap_get(sm, k) == (int)((i % 14u) + 1u));
    }
    free(sm);
}

static void test_snap_map_rr_eviction(void) {
    KtglSnapMap *sm = calloc(1u, sizeof *sm);
    assert(sm != NULL);
    ktgl_snap_reset(sm);
    static const uint64_t rr_keys[16] = {
        0x666aULL, 0x7178ULL, 0xccd4ULL, 0xd7bdULL,
        0xe5ccULL, 0x15b0cULL, 0x1af7aULL, 0x1db8fULL,
        0x20decULL, 0x2b18aULL, 0x2df24ULL, 0x33fe6ULL,
        0x59a0bULL, 0x629ceULL, 0x64567ULL, 0x70c52ULL,
    };
    for (size_t i = 0; i < 16u; i++) {
        const void *k = (const void *)(uintptr_t)(rr_keys[i] << 4);
        ktgl_snap_put(sm, k, (uint8_t)((i % 14u) + 1u));
    }
    for (size_t i = 8u; i < 16u; i++) {
        const void *k = (const void *)(uintptr_t)(rr_keys[i] << 4);
        assert(ktgl_snap_get(sm, k) == (int)((i % 14u) + 1u));
    }
    free(sm);
}

static void test_rollover_mid_node_and_va_reuse(void) {
    ktgl_ui_tag_reset_for_test();
    ktgl_ui_tag_set_bounds_for_test(g_tb);
    g_mock_wid = 0u;
    _Alignas(16) uint8_t mgr[0x80] = {0}, prod[0x4100] = {0}, cons[0x4100] = {0}, ch1[256], ch2[256];
    uintptr_t pp = (uintptr_t)prod, sr = (uintptr_t)cons - 0x10u, cp = (uintptr_t)cons;
    uintptr_t c1 = (uintptr_t)ch1, c2 = (uintptr_t)ch2;
    memcpy(mgr + 0x40u, &pp, sizeof pp);
    memcpy(prod + 0x3fb0u, &sr, sizeof sr);
    memcpy(prod + 0x4050u, &c1, sizeof c1);
    uintptr_t c1_32 = c1 + 32u, c1_128 = c1 + 128u, c2_48 = c2 + 48u;
    memcpy(prod + 0x4048u, &c1_32, sizeof c1_32);
    SynPane p_obj, p_mm;
    syn_init(&p_obj, "battle_parts_main_misson_info", NULL, NULL);
    syn_init(&p_mm, "PANE_NULL_MINIMAP", NULL, NULL);
    SynNode n_obj = {.vt = 0x20000ULL, .pane = &p_obj}, n_mm = {.vt = 0x20000ULL, .pane = &p_mm};
    ktgl_aoi_on_pane((uintptr_t)&p_obj, 0u, 0u); ktgl_aoi_on_submit((uintptr_t)&n_obj, (uintptr_t)&n_obj, 0u);
    ktgl_aoi_on_pane((uintptr_t)&p_mm, 0u, 0u); ktgl_aoi_on_submit((uintptr_t)&n_mm, (uintptr_t)&n_mm, 0u);

    ktgl_aoi_mgr_enter((uintptr_t)mgr, 0u, 0u);
    assert(ktgl_aoi_pre_node((uintptr_t)&n_obj, (uintptr_t)mgr, 0u) == 0u);
    assert(ktgl_aoi_rollover(c1, c1_128, pp) == 0u);
    memcpy(prod + 0x4050u, &c2, sizeof c2);
    memcpy(prod + 0x4048u, &c2_48, sizeof c2_48);
    assert(ktgl_aoi_pre_node((uintptr_t)&n_mm, (uintptr_t)mgr, 0u) == 0u);
    assert(ktgl_aoi_mgr_exit((uintptr_t)mgr, 0u, 0u) == 0u);

    assert(ktgl_aoi_cons_cmd(c1 + 40u, cp, 0u) == 0u && g_mock_wid == KTGL_WID_OBJECTIVE);
    assert(ktgl_aoi_rollover(c1, c1_128, pp) == 0u); /* !in_mgr end-of-frame submit keeps spans */
    assert(ktgl_aoi_cons_cmd(c1 + 40u, cp, 0u) == 0u && g_mock_wid == KTGL_WID_OBJECTIVE);
    assert(ktgl_aoi_cons_pkt_begin(cp, c1, c1_128) == 0u && g_mock_wid == KTGL_WID_NONE);
    assert(ktgl_aoi_cons_cmd(c1 + 40u, cp, 0u) == 0u && g_mock_wid == KTGL_WID_OBJECTIVE);
    assert(ktgl_aoi_cons_pkt_end(cp, 0u, 0u) == 0u && g_mock_wid == KTGL_WID_NONE);
    assert(ktgl_aoi_cons_cmd(c1 + 40u, cp, 0u) == 0u && g_mock_wid == KTGL_WID_NONE);
    assert(ktgl_aoi_cons_cmd(c2 + 8u, cp, 0u) == 0u && g_mock_wid == KTGL_WID_OBJECTIVE);

    uint8_t *syn = calloc(1u, 0x4000u);
    assert(syn != NULL);
    KtglEmitCtx ectx = {.pool_lo = 0x1000u, .pool_hi = 0x2000u, .cur = 0x1000u, .bcall_off = 0x1000u};
    ectx.cur = 0x1100u;
    const KtglSiteSpec sp = {0x100u, 0xeb09011fu, 0u, 21u, 8u, 9u, 8u};
    assert(ktgl_emit_call_stub(syn, &ectx, &sp, 0x12345678ULL) == 1);
    assert(ktgl_rd32(syn, 0x1118u) == 0xf94003ebu); /* ldr x11, [sp, #0] before x9 clobber */
    free(syn);
}

static void test_world_wheel_and_zero_non_evict(void) {
    _Alignas(16) uint8_t sl[0x380] = {0};
    uintptr_t vt = 0x20000ULL;
    SynPane r_p, inout_p, pos_p, text_r, bat_l, cool_rt, cool_tok, corner_a, main_bat;
    const void *rp = &r_p;
    memcpy(sl, &vt, sizeof vt);
    memcpy(sl + KTGL_SL_OFF_ROOT_PANE, &rp, sizeof rp);
    syn_init(&r_p, "NULL_root", NULL, sl);
    syn_init(&inout_p, "NULL_guide_item_inout", &r_p, sl);
    syn_init(&pos_p, "NULL_guide_item_pos", &inout_p, sl);
    syn_init(&text_r, "TEXT28_guide_r", &pos_p, sl);
    syn_init(&bat_l, "PIC_base_battery_L_00", &inout_p, sl);
    syn_add_child(&r_p, &inout_p);
    syn_add_child(&inout_p, &pos_p);
    syn_add_child(&pos_p, &text_r);
    syn_add_child(&inout_p, &bat_l);
    assert(ktgl_classify_pane(&text_r, &g_tb) == KTGL_WID_WORLD_MARKER);
    assert(ktgl_classify_pane(&bat_l, &g_tb) == KTGL_WID_WORLD_MARKER);
    syn_init(&cool_rt, "NULL_guide_item_cooltime", NULL, NULL);
    syn_init(&cool_tok, "PIC_icon_tokusyu", &cool_rt, NULL);
    assert(ktgl_classify_pane(&cool_tok, &g_tb) == KTGL_WID_WORLD_MARKER);
    syn_init(&corner_a, "Null_guide_item_a", NULL, NULL);
    assert(ktgl_classify_pane(&corner_a, &g_tb) == KTGL_WID_BTN_GUIDE);
    syn_init(&main_bat, "battle_parts_main_gauge_zonaubattery_set", NULL, NULL);
    assert(ktgl_classify_pane(&main_bat, &g_tb) == KTGL_WID_BATTERIES);

    KtglSnapMap *sm = calloc(1u, sizeof *sm);
    assert(sm != NULL);
    for (size_t i = 0; i < 64u; i++) {
        ktgl_snap_put(sm, (const void *)(0x200000ULL + i * 0x10000u), KTGL_WID_HEARTS);
    }
    for (uint32_t j = 0; j < 50000u; j++) {
        ktgl_snap_put(sm, (const void *)(0x9000000ULL + (uintptr_t)j * 16u), 0u);
    }
    for (size_t i = 0; i < 64u; i++) {
        assert(ktgl_snap_get(sm, (const void *)(0x200000ULL + i * 0x10000u)) == KTGL_WID_HEARTS);
    }
    ktgl_snap_del(sm, (const void *)0x200000ULL);
    assert(ktgl_snap_get(sm, (const void *)0x200000ULL) == -1);
    free(sm);
}


static void test_page_cache_and_mprotect_guard(void) {
    ktgl_pg_bump_epoch();
    SynPane root, mm;
    syn_init(&root, "NULL_root", NULL, NULL);
    syn_init(&mm, "PANE_NULL_MINIMAP", &root, &root);
    assert(ktgl_classify_pane(&mm, &g_tb) == KTGL_WID_MINIMAP);
    uint64_t p0 = ktgl_os_probe_count();
    for (int i = 0; i < 500; i++) {
        assert(ktgl_classify_pane(&mm, &g_tb) == KTGL_WID_MINIMAP);
    }
    uint64_t delta = ktgl_os_probe_count() - p0;
    assert(delta <= 8u);

    uint8_t *pg = mmap(NULL, 4096u, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(pg != MAP_FAILED);
    SynPane *pm = (SynPane *)pg;
    syn_init(pm, "PANE_NULL_MINIMAP", NULL, NULL);
    assert(ktgl_classify_pane(pm, &g_tb) == KTGL_WID_MINIMAP);
    ktgl_pg_bump_epoch();
    assert(mprotect(pg, 4096u, PROT_NONE) == 0);
    assert(ktgl_classify_pane(pm, &g_tb) == KTGL_WID_NONE);
    assert(munmap(pg, 4096u) == 0);
}

int main(void) {
    test_prot_none_guard();
    test_all_widget_classes();
    test_menu_override_and_fallbacks();
    test_node_guard_and_cache();
    test_producer_consumer_and_flush_order();
    test_aoi_binary_patch_install();
    test_relro_vtable_and_mod0_bss();
    test_deferred_sub_ctx_and_combo_bg();
    test_snapshot_map_and_cur_pane_priority();
    test_parallel_worker_submit();
    test_real_screenlayout_and_corpus_fixes();
    test_consumer_cross_thread_and_nested_mgr();
    test_classifier_depth5_btn_guide_and_snap_hash();
    test_snap_map_rr_eviction();
    test_rollover_mid_node_and_va_reuse();
    test_world_wheel_and_zero_non_evict();
    test_page_cache_and_mprotect_guard();
    printf("ktgl_wid_classify: PASS\n");
    return 0;
}
