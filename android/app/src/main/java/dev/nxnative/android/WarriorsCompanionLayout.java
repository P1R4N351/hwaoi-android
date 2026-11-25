// SPDX-License-Identifier: MIT
package dev.nxnative.android;

/**
 * The companion's geometry and motion in one place, shared verbatim by the Age of Calamity and Age of
 * Imprisonment ports (only the package line differs). Pure Java, no android.*: every rect is computed from
 * named constants in the 1240x1080 logical space, and every animated value is a closed-form function of the
 * frame time, so motion is frame-rate independent, retargetable mid-flight and allocation-free per frame.
 */
final class WarriorsCompanionLayout {
    private WarriorsCompanionLayout() { }

    static final float LOGIC_W = 1240f, LOGIC_H = 1080f;

    /*
     * Content-safe frame: at least 14 units inside both apps' Pad chrome. Header icons and their tab rules end
     * at y=77; the top edge bands end at 84 (AoI) / 92 (AoC); the bottom edge bands start at 996 (AoI) /
     * 1014 (AoC); the wall friezes occupy x 8..28 and 1212..1232; the AoC side rules x 30..42 and 1198..1210.
     * The AoI translucent side tint (x 20..116 and 1124..1220) is backdrop, not a keep-out.
     */
    static final float FRAME_L = 56f, FRAME_T = 108f, FRAME_R = 1184f, FRAME_B = 980f;
    static final float FRAME_W = FRAME_R - FRAME_L, CENTER_X = (FRAME_L + FRAME_R) * 0.5f;
    static final float GAP = 24f;

    /* Header: tab glyph top-left, settings glyph top-right. */
    static final float HEADER_B = 104f, TAB0_HIT_L = 1040f, TAB_SPLIT = 1128f;   // both tab glyphs sit in the right corner
    static final float TAB0_CX = 1090f, TAB1_CX = 1166f, NAV_CY = 62f, NAV_RULE_Y = 86f, NAV_RULE_HALF = 28f;

    /*
     * Objective: centred on OBJ_CY and as large as OBJ_FIT_W x OBJ_FIT_H allows. Its bitmap carries wide transparent
     * margins (the marker's glow), so the slot may reach past the reserved band; only those margins do.
     */
    static final float OBJ_BAND_H = 192f, OBJ_FIT_W = 980f, OBJ_FIT_H = 280f, OBJ_CY = 144f, OBJ_APPEAR_DY = -16f;

    /*
     * Left HUD: the bitmap's visible content clusters (row bands split on empty gaps), each cropped to its
     * content and scaled up into the space left of the minimap circle, stacked top-down. The minimap is centred
     * in what a HUD_COL_W column leaves; clusters may reach further right where the circle curves away.
     */
    // Every left-HUD cluster is drawn between HUD_MIN_SCALE and HUD_MAX_UPSCALE of its size in the game: a narrow one
    // (an ally pill, the button guide) is not blown up to the column's width, and none is squeezed below the game's size.
    static final float HUD_COL_W = 400f, HUD_MAX_UPSCALE = 1.6f, HUD_MIN_SCALE = 1f;
    static final int SCAN_STEP = 4, SCAN_ALPHA = 24, SCAN_MIN_GAP = 16, SCAN_PAD = 4, SCAN_MIN_AREA = 256;
    static final int MAX_CLUSTERS = 8, MAX_SCAN_ROWS = 512, MAX_SCAN_WIDTH = 2048, FIT_ITERS = 8;
    static final long RESCAN_MS = 250L;
    static final float MAP_TOP = FRAME_T + OBJ_BAND_H + GAP;
    static final float HUD_LEFT = 16f, HUD_TOP = 24f;   // the column runs from the top-left corner along the screen's left edge
    // A cluster that sits at the very bottom of the game's left strip (the KO counter) is pinned to the bottom-left corner.
    static final float HUD_BOTTOM = LOGIC_H - 24f, BOTTOM_ANCHOR = 0.12f;
    static final float MAP_RING = 2f, MAP_APPEAR_SHRINK = 0.04f;
    static final int MAP_RING_COLOR = 0x705CE6C4;

    /* Status tab: telemetry column on the left, seven hairline setting rows on the right. */
    static final float ST_X = FRAME_L + 16f, ST_FPS_Y = 192f, ST_SUB_Y = 228f;
    static final float ST_GRAPH_Y = 258f, ST_GRAPH_W = 600f, ST_GRAPH_H = 204f;
    static final float ST_DEV_Y = 516f, ST_LINE = 28f, ST_FOOTER_Y = 950f;
    static final int ST_DEV_LINES = 7;
    static final float ROW_X0 = ST_X + ST_GRAPH_W + 72f, ROW_X1 = FRAME_R - 8f;
    static final float ROW_Y0 = 140f, ROW_STEP = 112f, ROW_RULE = 96f;
    static final float ROW_DIAMOND_X = ROW_X0 + 12f, ROW_LABEL_X = ROW_X0 + 36f;
    static final int ROW_COUNT = 7;

    /* Motion. */
    static final float OMEGA = 28f;              // rad/s: from rest, within 1% of target at ~237 ms
    static final long SPRING_MAX_MS = 1000L;     // hard stop: an animation can never run longer than this
    static final long ABSENT_GRACE_MS = 150L;    // a live element must be missing this long before it fades out
    static final long SIZE_HOLD_MS = 1200L;      // a live element's displayed size shrinks only after this long
    static final float POS_EPS = 0.1f, ALPHA_EPS = 0.002f, VISIBLE_ALPHA = 0.02f, TAB_SHIFT = 32f;

    /** Minimap centre: middle of the frame, or middle of the space right of the HUD (hudRight <= 0: no HUD). */
    static float mapCenterX(float hudRight) {
        float left = hudRight > 0f ? hudRight : FRAME_L;
        return (left + FRAME_R) * 0.5f;
    }

    static float mapCenterY() {
        return (MAP_TOP + FRAME_B) * 0.5f;
    }

    /** Radius that keeps the ring inside the frame and at least GAP clear of the HUD on both sides of the space. */
    static float mapRadius(float hudRight) {
        float left = hudRight > 0f ? hudRight : FRAME_L, pad = hudRight > 0f ? GAP : 0f;
        return Math.min((FRAME_R - left) * 0.5f - pad, (FRAME_B - MAP_TOP) * 0.5f) - MAP_RING;
    }

    /** Bounds of the minimap circle (ring included inside the frame). */
    static void mapRect(float hudRight, float[] out) {
        float cx = mapCenterX(hudRight), cy = mapCenterY(), r = mapRadius(hudRight);
        out[0] = cx - r; out[1] = cy - r; out[2] = cx + r; out[3] = cy + r;
    }

    /** Right edge of the HUD column the minimap centres against when a HUD is shown. */
    static float hudColumnRight() {
        return FRAME_L + HUD_COL_W;
    }

    /**
     * Largest x a HUD cluster spanning [ya, yb] may reach: GAP left of the minimap circle (placed for a HUD),
     * measured over the span widened by GAP so a corner never comes closer than the gap either.
     */
    static float hudFreeRight(float ya, float yb) {
        float cx = mapCenterX(hudColumnRight()), cy = mapCenterY(), r = mapRadius(hudColumnRight()) + MAP_RING;
        float a = ya - GAP, b = yb + GAP;
        float dy = (a <= cy && b >= cy) ? 0f : Math.min(Math.abs(a - cy), Math.abs(b - cy));
        float edge = dy >= r ? FRAME_R : cx - (float) Math.sqrt(r * r - dy * dy);
        float lim = Math.min(FRAME_R, edge - GAP);
        return ya < MAP_TOP ? Math.min(lim, hudColumnRight()) : lim;   // beside the objective: stay in the column
    }

    /**
     * Fit held clusters (source rects, top-down order in idx[0..n)) into the left column: each scaled as large as
     * the circle allows (capped at HUD_MAX_UPSCALE, and never above the first, topmost cluster's scale), stacked
     * with GAP between; if the stack overflows the column every scale shrinks by one factor and the fit reruns.
     * Writes destination rects (4 floats per cluster, indexed like the source) and returns the stack bottom.
     */
    static float fitClusters(ClusterHold h, int[] idx, int n, float[] scale, float[] out) {
        for (int k = 0; k < n; k++) scale[k] = HUD_MAX_UPSCALE;
        int m = (n > 0 && h.anchoredBottom(idx[n - 1])) ? n - 1 : n;   // the pinned cluster is not part of the stack
        float bottom = fitPass(h, idx, m, scale, out), limit = FRAME_B;
        if (m < n) {
            int i = idx[n - 1];
            float w = h.width(i), ht = h.height(i);
            float s = m > 0 ? Math.min(HUD_MAX_UPSCALE, scale[0]) : HUD_MAX_UPSCALE;
            if (w > 0f) s = Math.min(s, (hudColumnRight() - HUD_LEFT) / w);
            scale[n - 1] = s;
            float top = HUD_BOTTOM - ht * s;
            out[i * 4] = HUD_LEFT; out[i * 4 + 1] = top; out[i * 4 + 2] = HUD_LEFT + w * s; out[i * 4 + 3] = HUD_BOTTOM;
            limit = top - GAP;
        }
        // Overflow is taken from the bottom of the stack up: the lowest cluster gives way first (to HUD_MIN_SCALE at most),
        // then the one above it, so a cluster appearing low in the column (combo messages) never shrinks what is above.
        for (int k = m - 1; k >= 1 && bottom > limit + 1e-3f; k--) {
            float hk = h.height(idx[k]) * scale[k];
            if (!(hk > 0f)) continue;
            float floor = h.height(idx[k]) * Math.min(scale[k], HUD_MIN_SCALE);
            float want = Math.max(floor, hk - (bottom - limit));
            scale[k] *= want / hk;
            bottom = fitPass(h, idx, m, scale, out);
        }
        if (bottom > limit + 1e-3f) {   // still too tall: the stack shrinks by one factor
            float sum = 0f;
            for (int k = 0; k < m; k++) sum += h.height(idx[k]) * scale[k];
            float room = limit - HUD_TOP - GAP * Math.max(0, m - 1);
            if (sum > 0f && room > 0f) {
                for (int k = 0; k < m; k++) scale[k] *= room / sum;
                bottom = fitPass(h, idx, m, scale, out);
            }
        }
        return m < n ? HUD_BOTTOM : bottom;
    }

    private static float fitPass(ClusterHold h, int[] idx, int n, float[] scale, float[] out) {
        float y = HUD_TOP;
        for (int k = 0; k < n; k++) {
            int i = idx[k];
            float w = h.width(i), ht = h.height(i);
            float s = k == 0 ? scale[0] : Math.min(scale[k], scale[0]);
            for (int it = 0; it < FIT_ITERS && w > 0f; it++) {
                s = Math.min(s, (hudFreeRight(y, y + ht * s) - HUD_LEFT) / w);
            }
            s = Math.max(0f, s);
            scale[k] = s;
            out[i * 4] = HUD_LEFT; out[i * 4 + 1] = y; out[i * 4 + 2] = HUD_LEFT + w * s; out[i * 4 + 3] = y + ht * s;
            y += ht * s + GAP;
        }
        return n > 0 ? y - GAP : HUD_TOP;
    }

    static float objectiveScale(float bw, float bh) {
        if (!(bw > 0f && bh > 0f)) return 0f;
        return Math.min(OBJ_FIT_W / bw, OBJ_FIT_H / bh);
    }

    /** Slot for an objective whose (held) source size is bw x bh: out of the screen's right edge, as on the top screen, on the OBJ_CY line. */
    static boolean objectiveSlot(float bw, float bh, float[] out) {
        float s = objectiveScale(bw, bh);
        if (s <= 0f) return clear(out);
        float sw = bw * s, sh = bh * s, left = LOGIC_W - sw;
        float top = Math.max(4f, OBJ_CY - sh * 0.5f);
        out[0] = left; out[1] = top; out[2] = left + sw; out[3] = top + sh;
        return true;
    }

    /** Header tab under a logical touch point, or -1. */
    static int headerTabAt(float lx, float ly) {
        if (ly < 0f || ly > HEADER_B) return -1;
        if (lx >= TAB0_HIT_L && lx < TAB_SPLIT) return 0;
        if (lx >= TAB_SPLIT && lx <= LOGIC_W) return 1;
        return -1;
    }

    /** Status setting row under a logical touch point, or -1. */
    static int statusRowAt(float lx, float ly) {
        if (lx < ROW_X0 - 16f || lx > ROW_X1 + 12f || ly < ROW_Y0) return -1;
        int row = (int) ((ly - ROW_Y0) / ROW_STEP);
        return row >= 0 && row < ROW_COUNT ? row : -1;
    }

    static float rowTop(int row) {
        return ROW_Y0 + row * ROW_STEP;
    }

    private static boolean clear(float[] out) {
        out[0] = 0f; out[1] = 0f; out[2] = 0f; out[3] = 0f;
        return false;
    }

    /**
     * Critically damped spring in closed form: x(t) = T + (c1 + c2 t) e^(-wt), c1 = x0 - T, c2 = v0 + w c1.
     * The value is a pure function of the state and the frame time, so it does not depend on how often it is
     * sampled; a retarget carries position and velocity over, so an interrupted move bends instead of jumping.
     */
    static final class Spring {
        private final float eps;
        private float target, x0, v0;
        private long t0;
        private boolean init;

        Spring(float eps) {
            this.eps = eps;
        }

        void snap(float v, long now) {
            target = v; x0 = v; v0 = 0f; t0 = now; init = true;
        }

        void retarget(float to, long now) {
            if (!init) { snap(to, now); return; }
            if (Float.compare(to, target) == 0) return;   // same target: keep the flight in progress
            float x = value(now), v = velocity(now);
            x0 = x; v0 = v; t0 = now; target = to;
        }

        float target() {
            return target;
        }

        float value(long now) {
            float t = elapsed(now);
            if (settledAt(t)) return target;
            float c1 = x0 - target, c2 = v0 + OMEGA * c1;
            return target + (c1 + c2 * t) * (float) Math.exp(-OMEGA * t);
        }

        float velocity(long now) {
            float t = elapsed(now);
            if (settledAt(t)) return 0f;
            float c1 = x0 - target, c2 = v0 + OMEGA * c1;
            return (c2 - OMEGA * (c1 + c2 * t)) * (float) Math.exp(-OMEGA * t);
        }

        boolean settled(long now) {
            return settledAt(elapsed(now));
        }

        private boolean settledAt(float t) {
            if (!init || t * 1000f >= SPRING_MAX_MS) return true;
            float c1 = x0 - target, c2 = v0 + OMEGA * c1, e = (float) Math.exp(-OMEGA * t);
            float dx = (c1 + c2 * t) * e, dv = (c2 - OMEGA * (c1 + c2 * t)) * e;
            return Math.abs(dx) < eps && Math.abs(dv) < eps * OMEGA;
        }

        private float elapsed(long now) {
            long d = now - t0;
            return d <= 0L ? 0f : d / 1000f;
        }
    }

    /** A live element counts as present until it has been missing for ABSENT_GRACE_MS (one dropped frame is not a fade). */
    static final class Presence {
        private long lastSeen;
        private boolean seen;

        boolean update(boolean present, long now) {
            if (present) { lastSeen = now; seen = true; return true; }
            if (!seen) return false;
            long d = now - lastSeen;
            if (d < 0L) { lastSeen = now; return true; }
            if (d < ABSENT_GRACE_MS) return true;
            seen = false;
            return false;
        }
    }

    /**
     * Displayed source size: grows at once, shrinks only to the largest size seen during the last full
     * SIZE_HOLD_MS window, so a bitmap whose size jitters frame to frame does not make its element pulse.
     */
    static final class Hold {
        private float w, h, winW, winH;
        private long until;
        private boolean has;

        void sample(float sw, float sh, long now) {
            if (!has) { w = sw; h = sh; winW = sw; winH = sh; until = now + SIZE_HOLD_MS; has = true; return; }
            winW = Math.max(winW, sw); winH = Math.max(winH, sh);
            w = Math.max(w, sw); h = Math.max(h, sh);
            if (now >= until || now < until - SIZE_HOLD_MS) {
                w = winW; h = winH; winW = sw; winH = sh; until = now + SIZE_HOLD_MS;
            }
        }

        void reset() {
            has = false; w = 0f; h = 0f; winW = 0f; winH = 0f;
        }

        float w() { return w; }
        float h() { return h; }
    }

    /**
     * Visible-content scan of a HUD bitmap on a SCAN_STEP grid, fed one sampled row at a time (the caller reads
     * the row; no allocation here). Occupied rows form bands; a band ends after SCAN_MIN_GAP source pixels of
     * empty rows. Each band's content bounds, padded by SCAN_PAD, is one cluster (at most MAX_CLUSTERS).
     */
    static final class ClusterScan {
        private final int[] rowMin = new int[MAX_SCAN_ROWS], rowMax = new int[MAX_SCAN_ROWS];
        final int[] x0 = new int[MAX_CLUSTERS], y0 = new int[MAX_CLUSTERS];
        final int[] x1 = new int[MAX_CLUSTERS], y1 = new int[MAX_CLUSTERS];
        private int rows, srcW, srcH, count;

        void begin(int w, int h) {
            srcW = Math.max(0, Math.min(w, MAX_SCAN_WIDTH)); srcH = Math.max(0, h); count = 0;
            rows = Math.min(MAX_SCAN_ROWS, (srcH + SCAN_STEP - 1) / SCAN_STEP);
        }

        int rowCount() { return rows; }
        int srcHeight() { return srcH; }
        int rowY(int i) { return i * SCAN_STEP; }
        int count() { return count; }

        /** Sampled row i from px[off .. off + srcW). */
        void row(int i, int[] px, int off) {
            if (i < 0 || i >= rows) return;
            int mn = Integer.MAX_VALUE, mx = -1;
            for (int x = 0; x < srcW; x += SCAN_STEP) {
                if ((px[off + x] >>> 24) > SCAN_ALPHA) { if (x < mn) mn = x; mx = x; }
            }
            rowMin[i] = mn; rowMax[i] = mx;
        }

        void finish() {
            int start = -1, last = -1, empty = 0, mn = Integer.MAX_VALUE, mx = -1;
            for (int i = 0; i < rows; i++) {
                if (rowMax[i] >= 0) {
                    if (start < 0) { start = i; mn = Integer.MAX_VALUE; mx = -1; }
                    last = i; empty = 0;
                    mn = Math.min(mn, rowMin[i]); mx = Math.max(mx, rowMax[i]);
                } else if (start >= 0 && (++empty) * SCAN_STEP >= SCAN_MIN_GAP) {
                    emit(start, last, mn, mx);
                    start = -1;
                }
            }
            if (start >= 0) emit(start, last, mn, mx);
        }

        private void emit(int first, int last, int mn, int mx) {
            int l = Math.max(0, mn - SCAN_PAD), t = Math.max(0, first * SCAN_STEP - SCAN_PAD);
            int r = Math.min(srcW, mx + SCAN_STEP + SCAN_PAD), b = Math.min(srcH, (last + 1) * SCAN_STEP + SCAN_PAD);
            if (count >= MAX_CLUSTERS || (r - l) * (b - t) < SCAN_MIN_AREA) return;
            x0[count] = l; y0[count] = t; x1[count] = r; y1[count] = b;
            count++;
        }

        /** Whole-image convenience (tests, offline tools): argb row-major, stride w. */
        void scan(int[] argb, int w, int h) {
            begin(w, h);
            for (int i = 0; i < rows; i++) row(i, argb, rowY(i) * w);
            finish();
        }
    }

    /**
     * Clusters kept stable over time: a detection is matched to the held cluster it overlaps vertically; held
     * bounds grow at once and shrink only to the union seen over the last SIZE_HOLD_MS window, so animating
     * sprites do not move the layout; a held cluster not seen for SIZE_HOLD_MS is dropped.
     */
    static final class ClusterHold {
        private final float[] x0 = new float[MAX_CLUSTERS], y0 = new float[MAX_CLUSTERS];
        private final float[] x1 = new float[MAX_CLUSTERS], y1 = new float[MAX_CLUSTERS];
        private final float[] wx0 = new float[MAX_CLUSTERS], wy0 = new float[MAX_CLUSTERS];
        private final float[] wx1 = new float[MAX_CLUSTERS], wy1 = new float[MAX_CLUSTERS];
        private final long[] until = new long[MAX_CLUSTERS], seen = new long[MAX_CLUSTERS];
        private final boolean[] live = new boolean[MAX_CLUSTERS], hit = new boolean[MAX_CLUSTERS];
        private float srcH;

        void ingest(ClusterScan s, long now) {
            srcH = s.srcHeight();
            for (int i = 0; i < MAX_CLUSTERS; i++) hit[i] = false;
            for (int j = 0; j < s.count(); j++) {
                int i = match(s.y0[j], s.y1[j]);
                if (i < 0) i = free();
                if (i < 0) continue;
                update(i, s.x0[j], s.y0[j], s.x1[j], s.y1[j], now);
                hit[i] = true;
            }
            for (int i = 0; i < MAX_CLUSTERS; i++) {
                long d = now - seen[i];
                if (live[i] && !hit[i] && (d >= SIZE_HOLD_MS || d < 0L || overlapsHit(i))) live[i] = false;
            }
        }

        private boolean overlapsHit(int i) {
            for (int k = 0; k < MAX_CLUSTERS; k++) {
                if (live[k] && hit[k] && y0[i] < y1[k] && y1[i] > y0[k]) return true;
            }
            return false;
        }

        private int match(float t, float b) {
            for (int i = 0; i < MAX_CLUSTERS; i++) if (live[i] && !hit[i] && t < y1[i] && b > y0[i]) return i;
            return -1;
        }

        private int free() {
            for (int i = 0; i < MAX_CLUSTERS; i++) if (!live[i]) return i;
            return -1;
        }

        private void update(int i, float l, float t, float r, float b, long now) {
            if (!live[i]) {
                x0[i] = l; y0[i] = t; x1[i] = r; y1[i] = b;
                wx0[i] = l; wy0[i] = t; wx1[i] = r; wy1[i] = b;
                until[i] = now + SIZE_HOLD_MS; live[i] = true; seen[i] = now;
                return;
            }
            seen[i] = now;
            wx0[i] = Math.min(wx0[i], l); wy0[i] = Math.min(wy0[i], t); wx1[i] = Math.max(wx1[i], r); wy1[i] = Math.max(wy1[i], b);
            x0[i] = Math.min(x0[i], l); y0[i] = Math.min(y0[i], t); x1[i] = Math.max(x1[i], r); y1[i] = Math.max(y1[i], b);
            if (now >= until[i] || now < until[i] - SIZE_HOLD_MS) {
                x0[i] = wx0[i]; y0[i] = wy0[i]; x1[i] = wx1[i]; y1[i] = wy1[i];
                wx0[i] = l; wy0[i] = t; wx1[i] = r; wy1[i] = b;
                until[i] = now + SIZE_HOLD_MS;
            }
        }

        void reset() {
            for (int i = 0; i < MAX_CLUSTERS; i++) live[i] = false;
        }

        /** Live clusters in top-down order into idx; returns how many. */
        int order(int[] idx) {
            int n = 0;
            for (int i = 0; i < MAX_CLUSTERS; i++) {
                if (!live[i]) continue;
                int k = n++;
                while (k > 0 && y0[idx[k - 1]] > y0[i]) { idx[k] = idx[k - 1]; k--; }
                idx[k] = i;
            }
            return n;
        }

        boolean live(int i) { return live[i]; }
        boolean overlapsLive(int i) {
            for (int k = 0; k < MAX_CLUSTERS; k++) {
                if (k != i && live[k] && y0[i] < y1[k] && y1[i] > y0[k]) return true;
            }
            return false;
        }
        /** True for a cluster lying wholly in the lower half of the strip and reaching its bottom BOTTOM_ANCHOR. */
        boolean anchoredBottom(int i) {
            return live[i] && srcH > 0f && y0[i] >= srcH * 0.5f && y1[i] >= srcH * (1f - BOTTOM_ANCHOR);
        }
        float left(int i) { return x0[i]; }
        float top(int i) { return y0[i]; }
        float right(int i) { return x1[i]; }
        float bottom(int i) { return y1[i]; }
        float width(int i) { return x1[i] - x0[i]; }
        float height(int i) { return y1[i] - y0[i]; }
    }

    /** An animated rect with an animated alpha. */
    static final class Slot {
        private final Spring l = new Spring(POS_EPS), t = new Spring(POS_EPS);
        private final Spring r = new Spring(POS_EPS), b = new Spring(POS_EPS);
        private final Spring a = new Spring(ALPHA_EPS);

        /** Move to the target rect; an element that is fully hidden first jumps to its appear pose (dy, shrink). */
        void show(float[] rect, long now, float dy, float shrink) {
            if (hidden(now)) {
                float ix = (rect[2] - rect[0]) * shrink * 0.5f, iy = (rect[3] - rect[1]) * shrink * 0.5f;
                l.snap(rect[0] + ix, now); t.snap(rect[1] + iy + dy, now);
                r.snap(rect[2] - ix, now); b.snap(rect[3] - iy + dy, now);
            }
            l.retarget(rect[0], now); t.retarget(rect[1], now);
            r.retarget(rect[2], now); b.retarget(rect[3], now);
            a.retarget(1f, now);
        }

        void hide(long now) {
            a.retarget(0f, now);
        }

        /** True once faded out completely (or never shown). */
        boolean hidden(long now) {
            return a.target() == 0f && a.value(now) <= ALPHA_EPS;
        }

        float alpha(long now) {
            return Math.max(0f, Math.min(1f, a.value(now)));
        }

        float left(long now) { return l.value(now); }
        float top(long now) { return t.value(now); }
        float right(long now) { return r.value(now); }
        float bottom(long now) { return b.value(now); }

        boolean settled(long now) {
            return a.settled(now) && l.settled(now) && t.settled(now) && r.settled(now) && b.settled(now);
        }
    }

    /**
     * Per-view tactical scene: targets from what is live this frame, springs toward them. The left HUD clusters
     * and the minimap are sequenced so they never overlap while visible: the map moves right and settles before
     * the clusters fade in, and the clusters fade out before the map moves back.
     */
    static final class Scene {
        final Slot map = new Slot(), obj = new Slot();
        final Slot[] hudParts = new Slot[MAX_CLUSTERS];
        final ClusterHold clusters = new ClusterHold();
        private final ClusterScan scan = new ClusterScan();
        private final Spring tab = new Spring(ALPHA_EPS), dim = new Spring(ALPHA_EPS);
        private final Presence mapP = new Presence(), hudP = new Presence(), objP = new Presence();
        private final Hold objHold = new Hold();
        private final float[] tmp = new float[4], mapTarget = new float[4], partRects = new float[4 * MAX_CLUSTERS];
        private final float[] partScale = new float[MAX_CLUSTERS], one = new float[4];
        private final int[] order = new int[MAX_CLUSTERS];

        Scene() {
            for (int i = 0; i < MAX_CLUSTERS; i++) hudParts[i] = new Slot();
        }

        /** The scan the view fills from the HUD bitmap (begin, row..., finish), then hands over with ingestHud. */
        ClusterScan hudScan() {
            return scan;
        }

        void ingestHud(long now) {
            clusters.ingest(scan, now);
        }

        /** Advance targets for this frame; returns true while anything is still moving. */
        boolean step(long now, int activeTab, boolean dimmed, boolean mapIn,
                     boolean hudIn, boolean objIn, int objW, int objH) {
            tab.retarget(activeTab == 0 ? 0f : 1f, now);
            dim.retarget(dimmed ? 1f : 0f, now);
            int n = clusters.order(order);
            boolean hudOn = hudP.update(hudIn && n > 0, now);
            boolean hudSide = hudOn || hudAlpha(now) > VISIBLE_ALPHA;
            stepMap(now, mapP.update(mapIn, now), hudSide ? hudColumnRight() : 0f);
            stepHud(now, hudOn, n);
            stepObjective(now, objP.update(objIn && objW > 0 && objH > 0, now), objW, objH);
            return !settled(now);
        }

        /** Most visible HUD cluster's alpha. */
        float hudAlpha(long now) {
            float a = 0f;
            for (int i = 0; i < MAX_CLUSTERS; i++) a = Math.max(a, hudParts[i].alpha(now));
            return a;
        }

        private void stepMap(long now, boolean on, float hudRight) {
            mapRect(hudRight, mapTarget);
            if (!on) { map.hide(now); return; }
            map.show(mapTarget, now, 0f, MAP_APPEAR_SHRINK);
        }

        private void stepHud(long now, boolean on, int n) {
            if (!on) {
                boolean allHidden = true;
                for (int i = 0; i < MAX_CLUSTERS; i++) { hudParts[i].hide(now); allHidden &= hudParts[i].hidden(now); }
                if (allHidden) clusters.reset();
                return;
            }
            fitClusters(clusters, order, n, partScale, partRects);
            boolean mapReady = map.alpha(now) <= VISIBLE_ALPHA || Math.abs(map.left(now) - mapTarget[0]) < 1f;
            for (int i = 0; i < MAX_CLUSTERS; i++) {
                Slot p = hudParts[i];
                if (!clusters.live(i)) { p.hide(now); continue; }
                if (!mapReady && p.hidden(now)) continue;
                one[0] = partRects[i * 4]; one[1] = partRects[i * 4 + 1]; one[2] = partRects[i * 4 + 2]; one[3] = partRects[i * 4 + 3];
                p.show(one, now, 0f, 0f);
            }
        }

        private void stepObjective(long now, boolean on, int w, int h) {
            if (!on) {
                obj.hide(now);
                if (obj.hidden(now)) objHold.reset();
                return;
            }
            objHold.sample(w, h, now);
            objectiveSlot(objHold.w(), objHold.h(), tmp);
            obj.show(tmp, now, OBJ_APPEAR_DY, 0f);
        }

        /** 0 on the tactical tab, 1 on the status tab, in between while switching. */
        float tabT(long now) {
            return Math.max(0f, Math.min(1f, tab.value(now)));
        }

        float dimT(long now) {
            return Math.max(0f, Math.min(1f, dim.value(now)));
        }

        float objectiveDrawScale(long now) {
            return objHold.h() > 0f ? (obj.bottom(now) - obj.top(now)) / objHold.h() : 0f;
        }

        boolean settled(long now) {
            if (!(tab.settled(now) && dim.settled(now) && map.settled(now) && obj.settled(now))) return false;
            for (int i = 0; i < MAX_CLUSTERS; i++) if (!hudParts[i].settled(now)) return false;
            return true;
        }
    }
}
