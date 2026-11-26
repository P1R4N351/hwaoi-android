#pragma once
/* KTGL ScreenLayout / Pane classifier shared by AoI and AoC native UI hooks and host tests. */
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include <string.h>
#include "ktgl_wid_ids.h"
#if defined(__APPLE__)
#include <mach/mach.h>
#elif defined(__linux__)
#include <errno.h>
#include <sys/uio.h>
#include <unistd.h>
#if !defined(__ANDROID__) && !defined(_GNU_SOURCE)
extern ssize_t process_vm_readv(pid_t, const struct iovec *, unsigned long,
    const struct iovec *, unsigned long, unsigned long);
#endif
#endif

enum {
    KTGL_PANE_OFF_CHILDCNT = 0x38u,
    KTGL_PANE_OFF_CHILDREN = 0x48u,
    KTGL_PANE_OFF_OWNER_SL = 0xd8u,
    KTGL_PANE_OFF_NAME     = 0xb0u,
    KTGL_PANE_OFF_PARENT   = 0xd0u,
    KTGL_PANE_MIN_SIZE     = 0xe0u,
    KTGL_SL_OFF_ROOT_PANE  = 0x358u,
    KTGL_NODE_OFF_PANE     = 0x08u,
    KTGL_CACHE_SLOTS       = 512u,
    KTGL_MAX_HOPS          = 24u,
    KTGL_MAX_CHILDREN      = 64u,
    KTGL_MAX_SL_SCAN       = 128u,
    KTGL_NAME_MAX          = 95u
};

/* Thread-local to the calling UI worker or producer thread (never shared across
   threads); structural Pane fields (vtable, name, parent, owner, child count) guard against reuse. */
typedef struct {
    uintptr_t pane, vt, name_ptr, parent_ptr, owner_sl;
    uint32_t child_cnt;
    uint32_t epoch;
    uint8_t wid;
    uint8_t valid;
    uint8_t pad[2];
} KtglWidCacheEntry;

typedef struct {
    KtglWidCacheEntry slots[KTGL_CACHE_SLOTS];
    uint32_t epoch;
    uint32_t lookups;
} KtglWidCache;

typedef struct {
    uintptr_t ro_lo, ro_hi;
    uintptr_t mod_lo, mod_hi;
    uintptr_t mem_lo, mem_hi;
} KtglAddrBounds;

static inline int ktgl_ptr_aligned(const void *p) {
    uintptr_t v = (uintptr_t)p;
    return v > 0x10000ULL && !(v & 7ULL);
}

static inline int ktgl_in_mem(uintptr_t a, size_t len, const KtglAddrBounds *b) {
    if (!b || a <= 0x10000ULL || !len || a + len < a) return 0;
    uintptr_t end = a + len;
    if (b->mod_hi > b->mod_lo && a >= b->mod_lo && end <= b->mod_hi) return 1;
    if (b->mem_hi > b->mem_lo && a >= b->mem_lo && end <= b->mem_hi) return 1;
    return 0;
}

static inline int ktgl_os_read(uintptr_t addr, void *dst, size_t len) {
    if (!len || !dst || addr <= 0x10000ULL) return 0;
#if defined(__APPLE__)
    vm_size_t got = 0;
    return vm_read_overwrite(mach_task_self(), (vm_address_t)addr, (vm_size_t)len,
               (vm_address_t)(uintptr_t)dst, &got) == KERN_SUCCESS && got == len;
#elif defined(__linux__)
    struct iovec l = {dst, len}, r = {(void *)addr, len};
    ssize_t n = process_vm_readv(getpid(), &l, 1UL, &r, 1UL, 0UL);
    if (n == (ssize_t)len) return 1;
    if (n < 0 && (errno == ENOSYS || errno == EPERM || errno == EACCES) && len <= 4096u) {
        int p[2];
        if (pipe(p) != 0) return 0;
        int ok = (write(p[1], (const void *)addr, len) == (ssize_t)len) &&
                 (read(p[0], dst, len) == (ssize_t)len);
        close(p[0]);
        close(p[1]);
        return ok;
    }
    return 0;
#else
    memcpy(dst, (const void *)addr, len);
    return 1;
#endif
}

static inline int ktgl_read_mem(
    uintptr_t addr, void *dst, size_t len, const KtglAddrBounds *b) {
    if (!ktgl_in_mem(addr, len, b)) return 0;
    if (b->mod_hi > b->mod_lo && addr >= b->mod_lo && addr + len <= b->mod_hi) {
        memcpy(dst, (const void *)addr, len);
        return 1;
    }
    return ktgl_os_read(addr, dst, len);
}

static inline uint64_t ktgl_nso_vtable_end(
    const uint8_t *base, uint64_t data_off, uint64_t mod_end) {
    uint64_t hi = mod_end > data_off ? mod_end : data_off;
    if (!base || hi < 0x18u) return hi;
    uint32_t m0 = 0u;
    memcpy(&m0, base + 4u, sizeof m0);
    if ((uint64_t)m0 + 16u <= hi && !memcmp(base + m0, "MOD0", 4u)) {
        int32_t bss_rel = 0;
        memcpy(&bss_rel, base + m0 + 8u, sizeof bss_rel);
        int64_t bss = (int64_t)m0 + (int64_t)bss_rel;
        if (bss > (int64_t)data_off && (uint64_t)bss <= hi) return (uint64_t)bss;
    }
    return hi;
}

static inline const void *ktgl_snap_ptr(const uint8_t *snap, size_t off) {
    const void *out = NULL;
    memcpy(&out, snap + off, sizeof out);
    return out;
}

static inline uint32_t ktgl_snap_u32(const uint8_t *snap, size_t off) {
    uint32_t out = 0;
    memcpy(&out, snap + off, sizeof out);
    return out;
}

static inline int ktgl_snap_pane(
    const void *pane, const KtglAddrBounds *b, uint8_t snap[KTGL_PANE_MIN_SIZE]) {
    if (!ktgl_ptr_aligned(pane)) return 0;
    if (!ktgl_read_mem((uintptr_t)pane, snap, KTGL_PANE_MIN_SIZE, b)) return 0;
    uintptr_t vt = (uintptr_t)ktgl_snap_ptr(snap, 0u);
    return b->ro_hi > b->ro_lo && !(vt & 7ULL) && vt >= b->ro_lo && vt < b->ro_hi;
}

static inline int ktgl_valid_pane(const void *pane, const KtglAddrBounds *b) {
    uint8_t snap[KTGL_PANE_MIN_SIZE];
    return ktgl_snap_pane(pane, b, snap);
}

static inline size_t ktgl_snap_name(
    const uint8_t snap[KTGL_PANE_MIN_SIZE], const KtglAddrBounds *b, char *buf, size_t cap) {
    if (!buf || cap < 2u) return 0u;
    uintptr_t sa = (uintptr_t)ktgl_snap_ptr(snap, KTGL_PANE_OFF_NAME);
    if (!ktgl_in_mem(sa, 2u, b)) { buf[0] = 0; return 0u; }
    size_t lim = cap - 1u < KTGL_NAME_MAX ? cap - 1u : KTGL_NAME_MAX;
    size_t need = lim + 1u;
    char raw[96];
    if (need > sizeof raw) need = sizeof raw;
    if (lim >= need) lim = need - 1u;
    size_t c1 = 4096u - (size_t)(sa & 4095ULL);
    if (c1 > need) c1 = need;
    if (!ktgl_read_mem(sa, raw, c1, b)) { buf[0] = 0; return 0u; }
    size_t got = c1;
    if (c1 < need && memchr(raw, 0, c1) == NULL) {
        size_t c2 = need - c1;
        if (ktgl_read_mem(sa + c1, raw + c1, c2, b)) got += c2;
    }
    size_t n = 0u;
    for (; n < got && n < lim && raw[n] != 0; n++) {
        uint8_t c = (uint8_t)raw[n];
        if (c < 0x20u || c >= 0x7fu) { buf[0] = 0; return 0u; }
        buf[n] = (char)c;
    }
    if (n == got) { buf[0] = 0; return 0u; }
    buf[n] = 0;
    return n;
}

static inline size_t ktgl_pane_name(
    const void *pane, const KtglAddrBounds *b, char *buf, size_t cap) {
    if (!buf || cap < 2u) return 0u;
    uint8_t snap[KTGL_PANE_MIN_SIZE];
    if (!ktgl_snap_pane(pane, b, snap)) { buf[0] = 0; return 0u; }
    return ktgl_snap_name(snap, b, buf, cap);
}

static inline const void *ktgl_sl_root_pane(
    const void *sl, const KtglAddrBounds *b) {
    uint8_t snap[KTGL_PANE_MIN_SIZE];
    if (!ktgl_snap_pane(sl, b, snap)) return NULL;
    char nm[8];
    if (ktgl_snap_name(snap, b, nm, sizeof nm) > 0u) return sl;
    const void *rp = NULL;
    if (!ktgl_read_mem((uintptr_t)sl + KTGL_SL_OFF_ROOT_PANE, &rp, sizeof rp, b)) {
        return NULL;
    }
    return ktgl_valid_pane(rp, b) ? rp : NULL;
}

static inline uint32_t ktgl_snap_children(
    const uint8_t snap[KTGL_PANE_MIN_SIZE], const KtglAddrBounds *b,
    const void *out[KTGL_MAX_CHILDREN]) {
    uint32_t cnt = ktgl_snap_u32(snap, KTGL_PANE_OFF_CHILDCNT);
    const void *arr = ktgl_snap_ptr(snap, KTGL_PANE_OFF_CHILDREN);
    uint32_t lim = cnt < KTGL_MAX_CHILDREN ? cnt : KTGL_MAX_CHILDREN;
    if (!lim || !ktgl_ptr_aligned(arr)) return 0u;
    if (!ktgl_read_mem((uintptr_t)arr, out, (size_t)lim * sizeof(void *), b)) return 0u;
    return lim;
}

static inline int ktgl_has_any(const char *s, const char *const *keys, size_t n) {
    for (size_t i = 0; i < n; i++) if (strstr(s, keys[i])) return 1;
    return 0;
}

/* A key that is a prefix of other words ("tb_text_ko" in "tb_text_komoku") matches only where it ends the word:
   at the end of the name or before anything that is not a lower-case letter. */
static inline int ktgl_has_any_word(const char *s, const char *const *keys, size_t n) {
    for (size_t i = 0; i < n; i++) {
        size_t kl = strlen(keys[i]);
        const char *p = s;
        for (uint32_t hop = 0u; hop < KTGL_NAME_MAX && (p = strstr(p, keys[i])) != NULL; hop++, p++) {
            char c = p[kl];
            if (c < 'a' || c > 'z') return 1;
        }
    }
    return 0;
}

static inline uint8_t ktgl_match_menu(const char *n) {
    static const char *const k[] = {
        "pause", "win_status", "WIN_base_status", "BLUR_status", "NULL_win_status",
        "_menu", "option", "shop", "weaponshop", "win_camp", "btn_camp",
        "topmenu_camp", "contents_camp", "list_material_camp",
        "slot_material_select_camp", "gauge_camp", "worldmap", "wmap",
        "preparation", "result", "gallery", "chara_list", "chara_select",
        "armor_select", "special_ability_select_parts_win",
        "special_ability_select_parts_znaigear", "trainingcenter", "narration",
        "event_parts", "title_", "ttl_parts", "divine", "kokuin", "dyeing",
        "korok", "locationname", "keyassign", "menu_parts_multiplay",
        "worldmap_parts_multiplay", "allies", "tutorial", "icon_weaponskill"
    };
    if (!strncmp(n, "icon_", 5u) || !strncmp(n, "PIC_icon_", 9u)) return 0u;
    if (strstr(n, "btl_mask_carrying_item") || strstr(n, "menu_action_")
            || strstr(n, "menu_sync_") || strstr(n, "win_item_info")) return 0u;
    if (ktgl_has_any(n, k, sizeof k / sizeof k[0])) return KTGL_WID_MENU;
    const char *mp = strstr(n, "menu_");
    if (mp && !strstr(n, "icon_menu_")) return KTGL_WID_MENU;
    return 0u;
}

static inline uint8_t ktgl_match_top_nonmenu(const char *n) {
    static const char *const ks[] = {
        "vmes", "serifu", "subtitle", "smes", "message_balloon",
        "btl_win_base_msg", "btl_plt_msg", "win_message",
        "TEXT26_message", "TEXT28_message",
        "fukidasi", "battle_parts_main_msg"
    };
    static const char *const kw[] = {
        "lockon", "fort_gauge", "btl_gauge_fort", "boss_info", "boss_name",
        "hp_boss_unit", "hp_normal_unit", "btl_parts_weak", "danger",
        "pointer_air", "pointer_lock", "btl_parts_bow", "reticle", "db_center",
        "battle_parts_center", "center_system_message", "center_ko",
        "center_success", "center_start", "chapter_name", "gameover",
        "btl_plt_marker", "btl_parts_route", "unit_info", "goremsensor",
        "seeksensor", "btl_plt_sensor", "guide_btn_action", "guide_finalblow",
        "keyguide_finalblow", "action_gacha", "icon_linkstrike"
    };
    if (ktgl_has_any(n, ks, sizeof ks / sizeof ks[0])) return KTGL_WID_SPEECH;
    if (strstr(n, "still_speech") && !strstr(n, "still_speech_scale")) return KTGL_WID_SPEECH;
    if (ktgl_has_any(n, kw, sizeof kw / sizeof kw[0])) return KTGL_WID_WORLD_MARKER;
    if (strstr(n, "guide_action") && !strstr(n, "joycon") && !strstr(n, "cmn_action")) {
        return KTGL_WID_WORLD_MARKER;
    }
    return 0u;
}

static inline uint8_t ktgl_match_bot_left(const char *n) {
    static const char *const kh[] = {
        "common_parts_heart", "Parts_99_10_heart", "btl_parts_heart",
        "btl_eff_heart", "HeartBreak", "heart_recov", "Parts_heart_effect"
    };
    static const char *const kb[] = {
        "zonaubattery", "battery_L_", "gauge_battery", "base_battery", "flare_battery"
    };
    static const char *const kg[] = {
        "gauge_musou", "flare_musou", "gauge_sync", "eff_sync",
        "gauge_meiyaku", "meiyaku_gauge", "gauge_strike",
        "battle_parts_main_gauge_0", "btl_gauge_kohga", "btl_gauge_urbosa",
        "btl_gauge_impa", "gauge_ganon", "gauge_linkstrike",
        "linkstrike_playing", "linkstrike_stanby"
    };
    if (ktgl_has_any(n, kh, sizeof kh / sizeof kh[0])) return KTGL_WID_HEARTS;
    if (ktgl_has_any(n, kb, sizeof kb / sizeof kb[0])) return KTGL_WID_BATTERIES;
    if (ktgl_has_any(n, kg, sizeof kg / sizeof kg[0])) return KTGL_WID_GAUGE;
    if (strstr(n, "btl_parts_mark_") && !strstr(n, "btl_parts_mark_eff")) return KTGL_WID_GAUGE;
    return 0u;
}

static inline uint8_t ktgl_match_bot_rest(const char *n) {
    static const char *const km[] = {
        "minimap", "MINIMAP", "btl_icon_map", "btl_icon_unit_player",
        "btl_icon_fort", "btl_icon_base", "btl_icon_treasure",
        "btl_icon_barricade", "btl_icon_door", "btl_icon_oneway", "btl_icon_connection"
    };
    static const char *const ko[] = {
        "misson_info", "mission_info", "TEXT22_mission", "TEXT28_newmission",
        "tb_text_mission", "tb_text_newmission", "mission_count", "mission_timer"
    };
    static const char *const kc[] = {
        "comboflow", "NUL_atk_normal", "NUL_atk_heavy",
        "actionsheet_next_arrow", "actionsheet_key"
    };
    static const char *const kbu[] = {
        "guide_cmn_action", "guide_action_joycon", "guide_item",
        "guide_btn_item", "guide_icon_item", "item_cooltime", "db_cooltime",
        "db_keyguide", "zonaugear_sky", "znai_bullet", "timelimit",
        "action_cooltime", "keyguide_sky", "zonaugear_info", "zonaugear_exchange",
        "cmn_win_base_guide", "keyguide_flare_", "btl_parts_action_arrow",
        "btl_parts_action_x", "keyguide_longpress", "keyguide_mash",
        "tb_text_operation_"
    };
    static const char *const kal[] = {"player_info", "btl_plt_player", "PIC_ext_player"};
    static const char *const kko[] = {"ko_count", "kocount"};
    static const char *const kkw[] = {"num_ko", "tb_text_ko", "tb_num_ko"};
    static const char *const kit[] = {
        "carrying_item", "Parts_stl_carrying", "btl_icon_apple",
        "battle_parts_main_apple", "btl_flare_rod", "battle_parts_main_get"
    };
    static const char *const klo[] = {
        "btl_buff", "btl_parts_base_zl", "btl_parts_base_zr", "btl_parts_base_sl",
        "NULL_inout_zl", "NULL_inout_zr", "zonaugear_stock",
        "NULL_item_carry", "PIC_base_icon_item", "PIC_frame_icon_item"
    };
    if (ktgl_has_any(n, km, sizeof km / sizeof km[0])) return KTGL_WID_MINIMAP;
    if (ktgl_has_any(n, ko, sizeof ko / sizeof ko[0])) return KTGL_WID_OBJECTIVE;
    if (ktgl_has_any(n, kc, sizeof kc / sizeof kc[0])) return KTGL_WID_COMBO;
    if (ktgl_has_any(n, kbu, sizeof kbu / sizeof kbu[0])) return KTGL_WID_BTN_GUIDE;
    if (ktgl_has_any(n, kal, sizeof kal / sizeof kal[0])) return KTGL_WID_ALLIES;
    if (ktgl_has_any(n, kko, sizeof kko / sizeof kko[0])) return KTGL_WID_KO_COUNT;
    if (ktgl_has_any_word(n, kkw, sizeof kkw / sizeof kkw[0])) return KTGL_WID_KO_COUNT;
    if (ktgl_has_any(n, kit, sizeof kit / sizeof kit[0])) return KTGL_WID_ITEMS;
    if (ktgl_has_any(n, klo, sizeof klo / sizeof klo[0])) return KTGL_WID_LEFT_OTHER;
    return 0u;
}

static inline uint8_t ktgl_match_name(const char *n) {
    uint8_t w = ktgl_match_menu(n);
    if (w) return w;
    w = ktgl_match_top_nonmenu(n);
    if (w) return w;
    w = ktgl_match_bot_rest(n);
    if (w) return w;
    w = ktgl_match_bot_left(n);
    if (w) return w;
    return strstr(n, "marker_destination") ? KTGL_WID_OBJECTIVE : 0u;
}

static inline uint8_t ktgl_pick_top(uint8_t a, uint8_t b) {
    if (a == KTGL_WID_MENU || b == KTGL_WID_MENU) return KTGL_WID_MENU;
    if (a == KTGL_WID_SPEECH || b == KTGL_WID_SPEECH) return KTGL_WID_SPEECH;
    return a ? a : b;
}

static inline int ktgl_is_master_hud(const void *sl, const KtglAddrBounds *b) {
    const void *root = ktgl_sl_root_pane(sl, b);
    uint8_t snap[KTGL_PANE_MIN_SIZE];
    if (!root || !ktgl_snap_pane(root, b, snap)) return 0;
    const void *kids[KTGL_MAX_CHILDREN];
    uint32_t lim = ktgl_snap_children(snap, b, kids);
    if (!lim) return 0;
    int sp = 0, sm = 0, sg = 0, so = 0, sk = 0;
    for (uint32_t i = 0; i < lim; i++) {
        char nm[96];
        if (!ktgl_pane_name(kids[i], b, nm, sizeof nm)) continue;
        if (strstr(nm, "player_info")) sp = 1;
        if (strstr(nm, "MINIMAP") || strstr(nm, "minimap")) sm = 1;
        if (strstr(nm, "main_gauge")) sg = 1;
        if (strstr(nm, "misson_info") || strstr(nm, "mission_info")) so = 1;
        if (strstr(nm, "ko_count") || strstr(nm, "comboflow")) sk = 1;
    }
    return (sp + sm + sg + so + sk) >= 2;
}

static inline int ktgl_root_sl_has_menu(const void *root_sl, const KtglAddrBounds *b) {
    const void *root = ktgl_sl_root_pane(root_sl, b);
    uint8_t snap[KTGL_PANE_MIN_SIZE];
    if (!root || !ktgl_snap_pane(root, b, snap) || ktgl_is_master_hud(root_sl, b)) return 0;
    const void *kids[KTGL_MAX_CHILDREN];
    uint32_t lim = ktgl_snap_children(snap, b, kids);
    for (uint32_t i = 0; i < lim; i++) {
        char nm[96];
        if (ktgl_pane_name(kids[i], b, nm, sizeof nm) && !strstr(nm, "minimap") && ktgl_match_menu(nm)) return 1;
    }
    return 0;
}

static inline void ktgl_scan_note_snap(
    const uint8_t snap[KTGL_PANE_MIN_SIZE], const KtglAddrBounds *b, uint32_t *mask) {
    char nm[96];
    if (!ktgl_snap_name(snap, b, nm, sizeof nm)) return;
    uint8_t k = ktgl_match_menu(nm);
    if (!k) {
        uint8_t top = ktgl_match_top_nonmenu(nm);
        if (top == KTGL_WID_SPEECH) k = KTGL_WID_SPEECH;
    }
    if (!k) k = ktgl_match_bot_rest(nm);
    if (!k) k = ktgl_match_bot_left(nm);
    if (k && k < KTGL_WID_COUNT) *mask |= (1u << k);
}

static inline void ktgl_scan_node_children(
    const uint8_t snap[KTGL_PANE_MIN_SIZE], const KtglAddrBounds *b, uint32_t *mask) {
    const void *stk[KTGL_MAX_SL_SCAN];
    uint32_t sp = 0u, seen = 1u;
    ktgl_scan_note_snap(snap, b, mask);
    const void *k0[KTGL_MAX_CHILDREN];
    uint32_t l0 = ktgl_snap_children(snap, b, k0);
    for (uint32_t i = 0u; i < l0 && sp < KTGL_MAX_SL_SCAN; i++) stk[sp++] = k0[l0 - 1u - i];
    for (uint32_t step = 0u; step < KTGL_MAX_SL_SCAN && sp > 0u && seen < KTGL_MAX_SL_SCAN; step++) {
        const void *node = stk[--sp];
        uint8_t child_snap[KTGL_PANE_MIN_SIZE];
        if (!ktgl_snap_pane(node, b, child_snap)) continue;
        seen++;
        ktgl_scan_note_snap(child_snap, b, mask);
        const void *kids[KTGL_MAX_CHILDREN];
        uint32_t lk = ktgl_snap_children(child_snap, b, kids);
        for (uint32_t j = 0u; j < lk && sp < KTGL_MAX_SL_SCAN; j++) stk[sp++] = kids[lk - 1u - j];
    }
}

static inline uint8_t ktgl_resolve_sl_mask(uint32_t mask) {
    static const uint8_t k_pri[] = {
        KTGL_WID_OBJECTIVE, KTGL_WID_MINIMAP, KTGL_WID_ALLIES, KTGL_WID_ITEMS,
        KTGL_WID_KO_COUNT, KTGL_WID_COMBO, KTGL_WID_BTN_GUIDE, KTGL_WID_HEARTS,
        KTGL_WID_BATTERIES, KTGL_WID_GAUGE, KTGL_WID_LEFT_OTHER, KTGL_WID_SPEECH,
        KTGL_WID_MENU
    };
    if (!mask) return 0u;
    for (size_t i = 0; i < sizeof k_pri / sizeof k_pri[0]; i++) {
        if (mask & (1u << k_pri[i])) return k_pri[i];
    }
    return 0u;
}

static inline uint8_t ktgl_scan_owner_sl(const void *sl, const KtglAddrBounds *b) {
    const void *root = ktgl_sl_root_pane(sl, b);
    uint8_t snap[KTGL_PANE_MIN_SIZE];
    if (!root || !ktgl_snap_pane(root, b, snap) || ktgl_is_master_hud(sl, b)) return 0u;
    uint32_t mask = 0u;
    ktgl_scan_node_children(snap, b, &mask);
    return ktgl_resolve_sl_mask(mask);
}

static inline uint8_t ktgl_classify_pane_snap(
    const void *pane, const uint8_t snap0[KTGL_PANE_MIN_SIZE], const KtglAddrBounds *b) {
    const void *pane_sl = ktgl_snap_ptr(snap0, KTGL_PANE_OFF_OWNER_SL);
    const void *root_sl = NULL, *p = pane;
    uint8_t top_pri = 0u, bot_wid = 0u, md_hit = 0u, has_allies = 0u;
    for (uint32_t hop = 0u; hop < KTGL_MAX_HOPS && p; hop++) {
        uint8_t hbuf[KTGL_PANE_MIN_SIZE];
        const uint8_t *hsnap = hop == 0u ? snap0 : hbuf;
        if (hop != 0u && !ktgl_snap_pane(p, b, hbuf)) break;
        root_sl = ktgl_snap_ptr(hsnap, KTGL_PANE_OFF_OWNER_SL);
        char nm[96];
        if (ktgl_snap_name(hsnap, b, nm, sizeof nm)) {
            uint8_t m = ktgl_match_menu(nm);
            if (!m) m = ktgl_match_top_nonmenu(nm);
            if (m) top_pri = ktgl_pick_top(top_pri, m);
            uint8_t bw = ktgl_match_bot_rest(nm);
            if (!bw) bw = ktgl_match_bot_left(nm);
            if (bw == KTGL_WID_ALLIES) has_allies = 1u;
            if (!bot_wid && bw) bot_wid = bw;
            if (strstr(nm, "marker_destination")) md_hit = 1u;
        }
        p = ktgl_snap_ptr(hsnap, KTGL_PANE_OFF_PARENT);
    }
    if (top_pri == KTGL_WID_MENU || ktgl_root_sl_has_menu(root_sl, b)) return KTGL_WID_MENU;
    if (top_pri) return top_pri;
    uint8_t fb = ktgl_scan_owner_sl(pane_sl, b);
    if (!fb && root_sl != pane_sl) fb = ktgl_scan_owner_sl(root_sl, b);
    if (bot_wid == KTGL_WID_GAUGE && (has_allies || fb == KTGL_WID_ALLIES)) return KTGL_WID_ALLIES;
    if (bot_wid) return bot_wid;
    if (fb) return fb;
    return md_hit ? KTGL_WID_OBJECTIVE : 0u;
}

static inline uint8_t ktgl_classify_pane(const void *pane, const KtglAddrBounds *b) {
    uint8_t snap[KTGL_PANE_MIN_SIZE];
    if (!ktgl_snap_pane(pane, b, snap)) return 0u;
    return ktgl_classify_pane_snap(pane, snap, b);
}

static inline uint8_t ktgl_tag_combo_bg(
    uint8_t wid, const uint8_t snap[KTGL_PANE_MIN_SIZE], const KtglAddrBounds *b) {
    if (wid != KTGL_WID_COMBO) return wid;
    char nm[96];
    if (ktgl_snap_name(snap, b, nm, sizeof nm) && !strncmp(nm, "WIN_comboflow_square", 20u)) {
        return (uint8_t)(wid | (uint8_t)KTGL_WID_FLAG_BG);
    }
    return wid;
}

enum {
    KTGL_SNAP_SETS = 4096u,
    KTGL_SNAP_WAYS = 8u,
    KTGL_SNAP_VALID = 0x100ULL
};

typedef struct {
    _Atomic uint64_t slots[KTGL_SNAP_SETS][KTGL_SNAP_WAYS];
} KtglSnapMap;

static inline uint64_t ktgl_snap_hash(uint64_t key) {
    uint64_t x = key ^ (key >> 30);
    x *= 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

static inline uint32_t ktgl_snap_set_idx(uint64_t key) {
    return (uint32_t)(ktgl_snap_hash(key) & (KTGL_SNAP_SETS - 1u));
}

static inline void ktgl_snap_reset(KtglSnapMap *m) {
    if (!m) return;
    for (uint32_t s = 0u; s < KTGL_SNAP_SETS; s++) {
        for (uint32_t w = 0u; w < KTGL_SNAP_WAYS; w++) {
            atomic_store_explicit(&m->slots[s][w], 0ULL, memory_order_relaxed);
        }
    }
}

static inline void ktgl_snap_put(KtglSnapMap *m, const void *snap_node, uint8_t wid) {
    if (!m || !ktgl_ptr_aligned(snap_node)) return;
    uint64_t key = ((uint64_t)(uintptr_t)snap_node >> 4) & 0x0007ffffffffffffULL;
    uint64_t tag = (key << 9) | KTGL_SNAP_VALID | (uint64_t)wid;
    uint32_t set = ktgl_snap_set_idx(key);
    int empty_w = -1;
    for (uint32_t w = 0u; w < KTGL_SNAP_WAYS; w++) {
        uint64_t cur = atomic_load_explicit(&m->slots[set][w], memory_order_relaxed);
        if ((cur & KTGL_SNAP_VALID) != 0ULL && (cur >> 9) == key) {
            atomic_store_explicit(&m->slots[set][w], tag, memory_order_release);
            return;
        }
        if (empty_w < 0 && (cur & KTGL_SNAP_VALID) == 0ULL) empty_w = (int)w;
    }
    uint32_t dst = empty_w >= 0 ? (uint32_t)empty_w : (uint32_t)((ktgl_snap_hash(key) >> 12) & (KTGL_SNAP_WAYS - 1u));
    atomic_store_explicit(&m->slots[set][dst], tag, memory_order_release);
}

static inline int ktgl_snap_get(const KtglSnapMap *m, const void *snap_node) {
    if (!m || !ktgl_ptr_aligned(snap_node)) return -1;
    uint64_t key = ((uint64_t)(uintptr_t)snap_node >> 4) & 0x0007ffffffffffffULL;
    uint32_t set = ktgl_snap_set_idx(key);
    for (uint32_t w = 0u; w < KTGL_SNAP_WAYS; w++) {
        uint64_t cur = atomic_load_explicit(&m->slots[set][w], memory_order_acquire);
        if ((cur & KTGL_SNAP_VALID) != 0ULL && (cur >> 9) == key) {
            return (int)(cur & 0xffu);
        }
    }
    return -1;
}

static inline const void *ktgl_resolve_live_pane(
    const void *live_node, const KtglAddrBounds *b) {
    if (!ktgl_ptr_aligned(live_node)) return NULL;
    uintptr_t p1 = 0u;
    if (!ktgl_read_mem((uintptr_t)live_node + KTGL_NODE_OFF_PANE, &p1, sizeof p1, b)) return NULL;
    if (ktgl_valid_pane((const void *)p1, b)) return (const void *)p1;
    if (!ktgl_ptr_aligned((const void *)p1)) return NULL;
    static const size_t offs[3] = {0x08u, 0x78u, 0x68u};
    for (size_t i = 0u; i < 3u; i++) {
        uintptr_t cand = 0u;
        if (ktgl_read_mem(p1 + offs[i], &cand, sizeof cand, b)
                && ktgl_valid_pane((const void *)cand, b)) {
            return (const void *)cand;
        }
    }
    return NULL;
}

static inline int ktgl_classify_pane_cached(
    const void *pane, const KtglAddrBounds *b, KtglWidCache *cache) {
    uint8_t snap[KTGL_PANE_MIN_SIZE];
    if (!ktgl_snap_pane(pane, b, snap)) return -1;
    uintptr_t vt = (uintptr_t)ktgl_snap_ptr(snap, 0u);
    uintptr_t name_ptr = (uintptr_t)ktgl_snap_ptr(snap, KTGL_PANE_OFF_NAME);
    uintptr_t parent_ptr = (uintptr_t)ktgl_snap_ptr(snap, KTGL_PANE_OFF_PARENT);
    uintptr_t owner_sl = (uintptr_t)ktgl_snap_ptr(snap, KTGL_PANE_OFF_OWNER_SL);
    uint32_t child_cnt = ktgl_snap_u32(snap, KTGL_PANE_OFF_CHILDCNT);
    if (cache) {
        if (cache->epoch == 0u) cache->epoch = 1u;
        uint32_t idx = (uint32_t)((((uintptr_t)pane >> 4) * 2654435761u) & (KTGL_CACHE_SLOTS - 1u));
        KtglWidCacheEntry *e = &cache->slots[idx];
        if (e->valid && e->epoch == cache->epoch && e->pane == (uintptr_t)pane
                && e->vt == vt && e->name_ptr == name_ptr && e->parent_ptr == parent_ptr
                && e->owner_sl == owner_sl && e->child_cnt == child_cnt) {
            return (int)e->wid;
        }
        uint8_t wid = ktgl_tag_combo_bg(ktgl_classify_pane_snap(pane, snap, b), snap, b);
        e->pane = (uintptr_t)pane; e->vt = vt; e->name_ptr = name_ptr;
        e->parent_ptr = parent_ptr; e->owner_sl = owner_sl; e->child_cnt = child_cnt;
        e->epoch = cache->epoch; e->wid = wid; e->valid = 1u;
        return (int)wid;
    }
    return (int)ktgl_tag_combo_bg(ktgl_classify_pane_snap(pane, snap, b), snap, b);
}

static inline int ktgl_classify_node(
    const void *node_obj, const KtglAddrBounds *b, KtglWidCache *cache) {
    if (!ktgl_ptr_aligned(node_obj)) return -1;
    uintptr_t nhdr[2] = {0u, 0u};
    if (!ktgl_read_mem((uintptr_t)node_obj, nhdr, sizeof nhdr, b)) return -1;
    return ktgl_classify_pane_cached((const void *)nhdr[1], b, cache);
}
