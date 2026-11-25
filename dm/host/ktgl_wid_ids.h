#pragma once
/* Single source of truth for KTGL widget IDs and second-screen routing destinations. */

enum {
    KTGL_WID_NONE         = 0,
    KTGL_WID_HEARTS       = 1,
    KTGL_WID_GAUGE        = 2,
    KTGL_WID_BATTERIES    = 3,
    KTGL_WID_BTN_GUIDE    = 4,
    KTGL_WID_OBJECTIVE    = 5,
    KTGL_WID_MINIMAP      = 6,
    KTGL_WID_LEFT_OTHER   = 7,
    KTGL_WID_SPEECH       = 8,
    KTGL_WID_COMBO        = 9,
    KTGL_WID_WORLD_MARKER = 10,
    KTGL_WID_MENU         = 11,
    KTGL_WID_ALLIES       = 12,
    KTGL_WID_KO_COUNT     = 13,
    KTGL_WID_ITEMS        = 14,
    KTGL_WID_COUNT        = 15,
    KTGL_WID_FLAG_BG      = 0x80
};

enum {
    KTGL_DEST_FALLBACK = 0,
    KTGL_DEST_TOP      = 1,
    KTGL_DEST_MM       = 2,
    KTGL_DEST_OBJ      = 3,
    KTGL_DEST_LHUD     = 4,
    KTGL_DEST_HIDE     = 5
};
