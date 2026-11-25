// SPDX-License-Identifier: MIT
package dev.nxnative.android;

/**
 * Host JVM checks for {@link WarriorsCompanionLayout}: element rects in every state combination stay inside the
 * frame and apart, and the motion maths (spring, size hold, presence grace, scene sequencing) behaves as stated.
 * Shared verbatim by the Age of Calamity and Age of Imprisonment ports (only the package line differs).
 */
public final class WarriorsCompanionLayoutTest {
    private static final float E = 1e-3f;
    private static int checks;

    private static void check(boolean cond, String msg) {
        checks++;
        if (!cond) throw new AssertionError(msg);
    }

    private static boolean inFrame(float[] r) {
        return r[0] >= WarriorsCompanionLayout.FRAME_L - E && r[1] >= WarriorsCompanionLayout.FRAME_T - E
                && r[2] <= WarriorsCompanionLayout.FRAME_R + E && r[3] <= WarriorsCompanionLayout.FRAME_B + E;
    }

    /** Horizontal or vertical separation of two rects, at least gap. */
    private static boolean apart(float[] a, float[] b, float gap) {
        return a[2] + gap <= b[0] + E || b[2] + gap <= a[0] + E || a[3] + gap <= b[1] + E || b[3] + gap <= a[1] + E;
    }

    private static final int[][] OBJ_SIZES = {{1152, 224}, {160, 224}, {400, 224}, {1152, 40}, {4000, 224}, {64, 16}};
    private static final int HUD_W = 496, HUD_H = 1080;

    /** The live AoI battle strip as measured (496x1080): blocks of opaque content where the game draws. */
    private static int[] hudStrip(boolean list, int heartsRight) {
        int[] px = new int[HUD_W * HUD_H];
        fill(px, 50, 25, heartsRight, 69);   // hearts row
        fill(px, 46, 76, 190, 100);          // gauge
        fill(px, 47, 107, 227, 155);         // batteries
        fill(px, 43, 154, 158, 226);         // d-pad / item icons
        fill(px, 80, 256, 316, 320);         // ally pill
        if (list) fill(px, 56, 621, 458, 847);
        return px;
    }

    private static void fill(int[] px, int l, int t, int r, int b) {
        for (int y = t; y < b; y++) for (int x = l; x < r; x++) px[y * HUD_W + x] = 0xFFE0E0E0;
    }

    private static boolean near(int got, int want) {
        return Math.abs(got - want) <= WarriorsCompanionLayout.SCAN_STEP + WarriorsCompanionLayout.SCAN_PAD;
    }

    private static void testClusterDetection() {
        WarriorsCompanionLayout.ClusterScan s = new WarriorsCompanionLayout.ClusterScan();
        s.scan(hudStrip(true, 274), HUD_W, HUD_H);
        check(s.count() == 3, "three clusters (status block, ally pill, list): " + s.count());
        check(near(s.x0[0], 43) && near(s.y0[0], 25) && near(s.x1[0], 274) && near(s.y1[0], 226), "status block bounds");
        check(near(s.x0[1], 80) && near(s.y0[1], 256) && near(s.x1[1], 316) && near(s.y1[1], 320), "ally pill bounds");
        check(near(s.x0[2], 56) && near(s.y0[2], 621) && near(s.x1[2], 458) && near(s.y1[2], 847), "list bounds");
        s.scan(hudStrip(false, 274), HUD_W, HUD_H);
        check(s.count() == 2, "a transparent list takes no cluster");
        s.scan(new int[HUD_W * HUD_H], HUD_W, HUD_H);
        check(s.count() == 0, "a transparent strip has no clusters");
        int[] px = new int[HUD_W * HUD_H];
        fill(px, 10, 10, 100, 50); fill(px, 10, 58, 100, 90);     // 8 px apart: one cluster
        fill(px, 10, 200, 100, 240); fill(px, 10, 264, 100, 300); // 24 px apart: two clusters
        fill(px, 300, 600, 302, 602);                             // one stray pixel: noise
        s.scan(px, HUD_W, HUD_H);
        check(s.count() == 3, "gaps under SCAN_MIN_GAP merge, wider gaps split, specks drop: " + s.count());
    }

    private static int fitStrip(WarriorsCompanionLayout.ClusterHold h, boolean list, int[] idx, float[] sc, float[] out) {
        WarriorsCompanionLayout.ClusterScan s = new WarriorsCompanionLayout.ClusterScan();
        s.scan(hudStrip(list, 274), HUD_W, HUD_H);
        h.reset();
        h.ingest(s, 0L);
        int n = h.order(idx);
        WarriorsCompanionLayout.fitClusters(h, idx, n, sc, out);
        return n;
    }

    private static void checkClusterFit(WarriorsCompanionLayout.ClusterHold h, int[] idx, int n, float[] sc, float[] out) {
        float[] map = new float[4], r = new float[4];
        WarriorsCompanionLayout.mapRect(WarriorsCompanionLayout.hudColumnRight(), map);
        float cx = (map[0] + map[2]) * 0.5f, cy = (map[1] + map[3]) * 0.5f, rad = (map[2] - map[0]) * 0.5f;
        float prevBottom = -1f;
        for (int k = 0; k < n; k++) {
            int i = idx[k];
            r[0] = out[i * 4]; r[1] = out[i * 4 + 1]; r[2] = out[i * 4 + 2]; r[3] = out[i * 4 + 3];
            check(r[0] >= WarriorsCompanionLayout.HUD_LEFT - E && r[1] >= WarriorsCompanionLayout.HUD_TOP - E
                    && r[2] <= WarriorsCompanionLayout.FRAME_R + E && r[3] <= WarriorsCompanionLayout.FRAME_B + E, "cluster " + k + " inside the left column");
            if (k == 0) check(Math.abs(r[0] - WarriorsCompanionLayout.HUD_LEFT) < E && Math.abs(r[1] - WarriorsCompanionLayout.HUD_TOP) < E, "top cluster in the top-left corner");
            float qx = Math.max(r[0], Math.min(cx, r[2])), qy = Math.max(r[1], Math.min(cy, r[3]));
            float d = (float) Math.hypot(qx - cx, qy - cy);
            check(d >= rad + 8f, "cluster " + k + " clear of the minimap circle: " + (d - rad));
            float aspect = (r[2] - r[0]) / (r[3] - r[1]), src = h.width(i) / h.height(i);
            check(Math.abs(aspect - src) < 1e-3f * Math.max(1f, src), "cluster " + k + " keeps its aspect");
            if (prevBottom >= 0f) check(r[1] - prevBottom >= WarriorsCompanionLayout.GAP - E, "clusters stacked with GAP");
            if (k > 0) check(sc[k] <= sc[0] + E, "no cluster scaled above the top (hearts/batteries) cluster");
            prevBottom = r[3];
        }
    }

    private static void testClusterFit() {
        WarriorsCompanionLayout.ClusterHold h = new WarriorsCompanionLayout.ClusterHold();
        int[] idx = new int[WarriorsCompanionLayout.MAX_CLUSTERS];
        float[] sc = new float[WarriorsCompanionLayout.MAX_CLUSTERS], out = new float[4 * WarriorsCompanionLayout.MAX_CLUSTERS];
        int n = fitStrip(h, false, idx, sc, out);
        check(n == 2, "two clusters without the list");
        checkClusterFit(h, idx, n, sc, out);
        float noList = sc[0];
        check(noList >= 1.5f, "hearts/batteries scaled up (was 0.79x as one strip): " + noList);
        System.out.println("hud clusters, no list: scale " + sc[0] + " / " + sc[1] + ", status block "
                + out[idx[0] * 4] + "," + out[idx[0] * 4 + 1] + "," + out[idx[0] * 4 + 2] + "," + out[idx[0] * 4 + 3]);
        n = fitStrip(h, true, idx, sc, out);
        check(n == 3, "three clusters with the list");
        checkClusterFit(h, idx, n, sc, out);
        check(out[idx[2] * 4 + 3] <= WarriorsCompanionLayout.FRAME_B + E, "a full stack still fits the column");
        check(sc[0] >= 1.2f && sc[0] <= noList + E, "the list costs the top cluster some scale, not all: " + sc[0]);
        System.out.println("hud clusters, with list: scale " + sc[0] + " / " + sc[1] + " / " + sc[2]);
        int[] px = new int[HUD_W * HUD_H];
        fill(px, 20, 20, 470, 120);    // wide top block: the circle limits it near 1x
        fill(px, 20, 200, 60, 240);    // small icon below: width alone would allow 3x
        WarriorsCompanionLayout.ClusterScan s = new WarriorsCompanionLayout.ClusterScan();
        s.scan(px, HUD_W, HUD_H);
        h.reset();
        h.ingest(s, 0L);
        n = h.order(idx);
        WarriorsCompanionLayout.fitClusters(h, idx, n, sc, out);
        check(n == 2 && sc[0] < 1.5f, "wide top block is circle-limited: " + sc[0]);
        checkClusterFit(h, idx, n, sc, out);
    }

    private static void testPinnedBottomCluster() {
        WarriorsCompanionLayout.ClusterScan s = new WarriorsCompanionLayout.ClusterScan();
        WarriorsCompanionLayout.ClusterHold h = new WarriorsCompanionLayout.ClusterHold();
        int[] idx = new int[WarriorsCompanionLayout.MAX_CLUSTERS];
        float[] sc = new float[WarriorsCompanionLayout.MAX_CLUSTERS];
        float[] before = new float[4 * WarriorsCompanionLayout.MAX_CLUSTERS], after = new float[4 * WarriorsCompanionLayout.MAX_CLUSTERS];
        int[] px = hudStrip(true, 274);
        s.scan(px, HUD_W, HUD_H); h.ingest(s, 0L);
        int n0 = h.order(idx), top = idx[0];
        WarriorsCompanionLayout.fitClusters(h, idx, n0, sc, before);
        check(!h.anchoredBottom(idx[n0 - 1]), "a mid-strip list is not pinned");
        for (int y = 1001; y < 1053; y++) for (int x = 52; x < 256; x++) px[y * HUD_W + x] = 0xFFE0E0E0;   // KO counter, measured
        s.scan(px, HUD_W, HUD_H); h.ingest(s, 250L);
        int n1 = h.order(idx), ko = idx[n1 - 1];
        check(n1 == n0 + 1 && h.anchoredBottom(ko), "a block at the strip's bottom is the pinned cluster");
        float bottom = WarriorsCompanionLayout.fitClusters(h, idx, n1, sc, after);
        check(Math.abs(after[ko * 4] - WarriorsCompanionLayout.HUD_LEFT) < E && Math.abs(after[ko * 4 + 3] - WarriorsCompanionLayout.HUD_BOTTOM) < E
                && Math.abs(bottom - WarriorsCompanionLayout.HUD_BOTTOM) < E, "pinned to the bottom-left corner");
        boolean same = true;
        for (int k = 0; k < 4; k++) same &= Math.abs(before[top * 4 + k] - after[top * 4 + k]) < E;
        check(same, "the pinned cluster does not move or shrink the top cluster");
        for (int k = 0; k < n1 - 1; k++) check(after[idx[k] * 4 + 3] <= after[ko * 4 + 1] - WarriorsCompanionLayout.GAP + E, "stack stays above the pinned cluster");
    }

    private static void testClusterSizeLimits() {
        WarriorsCompanionLayout.ClusterScan s = new WarriorsCompanionLayout.ClusterScan();
        WarriorsCompanionLayout.ClusterHold h = new WarriorsCompanionLayout.ClusterHold();
        int[] idx = new int[WarriorsCompanionLayout.MAX_CLUSTERS];
        float[] sc = new float[WarriorsCompanionLayout.MAX_CLUSTERS], out = new float[4 * WarriorsCompanionLayout.MAX_CLUSTERS];
        int[] px = new int[HUD_W * HUD_H];
        for (int y = 40; y < 80; y++) for (int x = 80; x < 140; x++) px[y * HUD_W + x] = 0xFFE0E0E0;      // a narrow pill-like cluster
        for (int y = 200; y < 330; y++) for (int x = 40; x < 190; x++) px[y * HUD_W + x] = 0xFFE0E0E0;    // a button-guide-like cluster
        s.scan(px, HUD_W, HUD_H); h.ingest(s, 0L);
        int n = h.order(idx);
        WarriorsCompanionLayout.fitClusters(h, idx, n, sc, out);
        check(n == 2, "two clusters");
        for (int k = 0; k < n; k++) {
            int i = idx[k];
            float drawn = (out[i * 4 + 2] - out[i * 4]) / h.width(i);
            check(drawn <= WarriorsCompanionLayout.HUD_MAX_UPSCALE + 1e-3f, "cluster " + k + " not above the maximum scale");
            check(drawn >= WarriorsCompanionLayout.HUD_MIN_SCALE - 1e-3f, "cluster " + k + " not below the minimum scale");
        }
    }

    private static void testClusterHold() {
        WarriorsCompanionLayout.ClusterScan s = new WarriorsCompanionLayout.ClusterScan();
        WarriorsCompanionLayout.ClusterHold h = new WarriorsCompanionLayout.ClusterHold();
        int[] idx = new int[WarriorsCompanionLayout.MAX_CLUSTERS];
        float right0 = -1f;
        boolean steady = true, listKept = true;
        for (long t = 0L; t < 4000L; t += WarriorsCompanionLayout.RESCAN_MS) {
            boolean odd = (t / WarriorsCompanionLayout.RESCAN_MS) % 2L == 1L;
            s.scan(hudStrip(!(t >= 1000L && t < 1500L), odd ? 200 : 274), HUD_W, HUD_H);   // hearts animate; list blinks
            h.ingest(s, t);
            int n = h.order(idx);
            if (right0 < 0f) right0 = h.right(idx[0]);
            steady &= Math.abs(h.right(idx[0]) - right0) < E;
            listKept &= n == 3;
        }
        check(steady, "an animating sprite does not move its cluster");
        check(listKept, "a list absent for under SIZE_HOLD_MS keeps its place");
        for (long t = 4000L; t < 6000L; t += WarriorsCompanionLayout.RESCAN_MS) { s.scan(hudStrip(false, 274), HUD_W, HUD_H); h.ingest(s, t); }
        check(h.order(idx) == 2, "a list gone for longer is dropped and takes no space");
    }

    private static void testStaticLayoutAllStates() {
        float[] map = new float[4], obj = new float[4], hud = new float[4];
        for (int hasHud = 0; hasHud <= 1; hasHud++) {
            WarriorsCompanionLayout.mapRect(hasHud == 1 ? WarriorsCompanionLayout.hudColumnRight() : 0f, map);
            check(inFrame(map), "minimap inside frame, hud=" + hasHud);
            check(Math.abs((map[2] - map[0]) - (map[3] - map[1])) < E, "minimap bounds are square");
            check(map[1] >= WarriorsCompanionLayout.HEADER_B, "minimap below header");
            hud[0] = WarriorsCompanionLayout.HUD_LEFT; hud[1] = WarriorsCompanionLayout.HUD_TOP;
            hud[2] = WarriorsCompanionLayout.hudColumnRight(); hud[3] = WarriorsCompanionLayout.FRAME_B;
            if (hasHud == 1) {
                check(map[0] - hud[2] >= WarriorsCompanionLayout.GAP - E, "minimap clear of the hud column");
                check(Math.abs((map[0] - hud[2]) - (WarriorsCompanionLayout.FRAME_R - map[2])) < E,
                        "minimap centred in the space the hud column leaves");
            }
            checkObjectives(obj, map, hasHud == 1 ? hud : null);
        }
        WarriorsCompanionLayout.mapRect(0f, map);
        check(Math.abs((map[0] + map[2]) * 0.5f - 620f) < E, "minimap centred without hud");
        check(!WarriorsCompanionLayout.objectiveSlot(10, 0, obj), "empty sources give no rect");
    }

    private static void checkObjectives(float[] obj, float[] map, float[] hud) {
        for (int[] os : OBJ_SIZES) {
            check(WarriorsCompanionLayout.objectiveSlot(os[0], os[1], obj), "objective slot " + os[0] + "x" + os[1]);
            float sw = obj[2] - obj[0], sh = obj[3] - obj[1];
            check(sw <= WarriorsCompanionLayout.OBJ_FIT_W + E && sh <= WarriorsCompanionLayout.OBJ_FIT_H + E, "objective within its fit box");
            check(Math.abs(sw - WarriorsCompanionLayout.OBJ_FIT_W) < E || Math.abs(sh - WarriorsCompanionLayout.OBJ_FIT_H) < E,
                    "objective as large as the fit box allows");
            check(Math.abs(sw / sh - (float) os[0] / os[1]) < 0.01f, "objective keeps its aspect");
            check(Math.abs(obj[2] - WarriorsCompanionLayout.LOGIC_W) < E, "objective comes out of the right edge");
            check(obj[1] >= 4f - E && Math.abs((obj[1] + obj[3]) * 0.5f - Math.max(WarriorsCompanionLayout.OBJ_CY, 4f + sh * 0.5f)) < E,
                    "objective centred on its line");
            check(obj[0] >= 0f && obj[2] <= WarriorsCompanionLayout.LOGIC_W, "objective on screen");
        }
        check(map[1] >= WarriorsCompanionLayout.FRAME_T + WarriorsCompanionLayout.OBJ_BAND_H, "minimap below the objective band");
    }

    private static void testStatusAndHitTests() {
        float lastRule = WarriorsCompanionLayout.rowTop(WarriorsCompanionLayout.ROW_COUNT - 1) + WarriorsCompanionLayout.ROW_RULE;
        check(WarriorsCompanionLayout.rowTop(0) >= WarriorsCompanionLayout.FRAME_T && lastRule <= WarriorsCompanionLayout.FRAME_B,
                "setting rows inside frame");
        check(WarriorsCompanionLayout.ROW_X0 >= WarriorsCompanionLayout.ST_X + WarriorsCompanionLayout.ST_GRAPH_W + WarriorsCompanionLayout.GAP,
                "telemetry column clear of setting rows");
        check(WarriorsCompanionLayout.ROW_X1 <= WarriorsCompanionLayout.FRAME_R && WarriorsCompanionLayout.ST_X >= WarriorsCompanionLayout.FRAME_L,
                "status columns inside frame");
        float devEnd = WarriorsCompanionLayout.ST_DEV_Y + 30f + WarriorsCompanionLayout.ST_DEV_LINES * WarriorsCompanionLayout.ST_LINE;
        check(WarriorsCompanionLayout.ST_GRAPH_Y + WarriorsCompanionLayout.ST_GRAPH_H < WarriorsCompanionLayout.ST_DEV_Y - 18f
                && devEnd < WarriorsCompanionLayout.ST_FOOTER_Y - 17f && WarriorsCompanionLayout.ST_FOOTER_Y <= WarriorsCompanionLayout.FRAME_B,
                "telemetry blocks stack without collision");
        for (int r = 0; r < WarriorsCompanionLayout.ROW_COUNT; r++) {
            float y = WarriorsCompanionLayout.rowTop(r) + 48f;
            check(WarriorsCompanionLayout.statusRowAt(WarriorsCompanionLayout.ROW_LABEL_X, y) == r, "row " + r + " hit");
        }
        check(WarriorsCompanionLayout.statusRowAt(WarriorsCompanionLayout.ROW_LABEL_X, 100f) == -1, "above rows misses");
        check(WarriorsCompanionLayout.statusRowAt(WarriorsCompanionLayout.ROW_LABEL_X,
                WarriorsCompanionLayout.rowTop(WarriorsCompanionLayout.ROW_COUNT)) == -1, "below rows misses");
        check(WarriorsCompanionLayout.statusRowAt(400f, 200f) == -1, "telemetry column is not a row");
        check(WarriorsCompanionLayout.headerTabAt(WarriorsCompanionLayout.TAB0_CX, 50f) == 0 && WarriorsCompanionLayout.headerTabAt(WarriorsCompanionLayout.TAB1_CX, 50f) == 1,
                "header glyphs hit their tabs");
        check(WarriorsCompanionLayout.headerTabAt(620f, 50f) == -1 && WarriorsCompanionLayout.headerTabAt(74f, 50f) == -1
                && WarriorsCompanionLayout.headerTabAt(WarriorsCompanionLayout.TAB0_CX, 120f) == -1,
                "header misses");
    }

    private static void testSpringEase() {
        WarriorsCompanionLayout.Spring s = new WarriorsCompanionLayout.Spring(WarriorsCompanionLayout.POS_EPS);
        s.snap(0f, 1000L);
        s.retarget(100f, 1000L);
        check(Math.abs(s.value(1000L)) < E && Math.abs(s.value(900L)) < E, "starts where it was (and ignores a clock step back)");
        float prev = 0f;
        long settledAt = -1L;
        for (long t = 1000L; t <= 2000L; t++) {
            float v = s.value(t);
            check(v >= prev - E && v <= 100f + E, "monotone, no overshoot at " + t);
            prev = v;
            if (settledAt < 0L && s.settled(t)) settledAt = t - 1000L;
        }
        check(Math.abs(s.value(1150L) - 100f) > 5f, "still visibly moving at 150 ms");
        check(Math.abs(s.value(1300L) - 100f) < 1f, "within 1 unit of target by 300 ms");
        check(settledAt > 150L && settledAt <= 400L, "settles between 150 and 400 ms: " + settledAt);
        check(s.value(5000L) == 100f && s.velocity(5000L) == 0f, "rests exactly on target");
        System.out.println("spring: 0->100, value@100ms=" + s.value(1100L) + " @150ms=" + s.value(1150L)
                + " @300ms=" + s.value(1300L) + " settled@" + settledAt + "ms");
    }

    private static void testSpringRetargetAndRateIndependence() {
        WarriorsCompanionLayout.Spring a = new WarriorsCompanionLayout.Spring(WarriorsCompanionLayout.POS_EPS);
        a.snap(0f, 0L);
        a.retarget(200f, 0L);
        float x = a.value(120L), v = a.velocity(120L);
        a.retarget(-50f, 120L);
        check(Math.abs(a.value(120L) - x) < E, "retarget keeps position");
        check(Math.abs(a.velocity(120L) - v) < 1e-2f * Math.max(1f, Math.abs(v)), "retarget keeps velocity");
        check(Math.abs(a.value(800L) + 50f) < 0.5f, "retargeted flight lands on the new target");
        WarriorsCompanionLayout.Spring fast = new WarriorsCompanionLayout.Spring(WarriorsCompanionLayout.POS_EPS);
        WarriorsCompanionLayout.Spring slow = new WarriorsCompanionLayout.Spring(WarriorsCompanionLayout.POS_EPS);
        fast.snap(10f, 0L); slow.snap(10f, 0L);
        fast.retarget(90f, 0L); slow.retarget(90f, 0L);
        for (long t = 0L; t <= 528L; t += 16L) fast.retarget(90f, t);
        for (long t = 0L; t <= 528L; t += 33L) slow.retarget(90f, t);
        check(fast.value(264L) == slow.value(264L) && fast.value(528L) == slow.value(528L),
                "value depends on time, not on frame rate (16 ms vs 33 ms frames)");
        WarriorsCompanionLayout.Spring stuck = new WarriorsCompanionLayout.Spring(0f);
        stuck.snap(0f, 0L);
        stuck.retarget(1e6f, 0L);
        check(!stuck.settled(999L) && stuck.settled(WarriorsCompanionLayout.SPRING_MAX_MS), "hard stop bounds every animation");
    }

    private static void testHoldAndPresence() {
        WarriorsCompanionLayout.Hold h = new WarriorsCompanionLayout.Hold();
        float minW = Float.MAX_VALUE, maxW = 0f;
        for (long t = 0L; t < 5000L; t += 50L) {
            h.sample((t / 50L) % 2L == 0L ? 1000f : 600f, 224f, t);
            minW = Math.min(minW, h.w()); maxW = Math.max(maxW, h.w());
        }
        check(minW == 1000f && maxW == 1000f, "frame-to-frame size jitter is held at the largest size");
        long shrunkAt = -1L;
        for (long t = 5000L; t < 9000L; t += 50L) {
            h.sample(600f, 224f, t);
            check(h.w() >= 600f, "never below the live size");
            if (shrunkAt < 0L && h.w() == 600f) shrunkAt = t - 5000L;
        }
        check(shrunkAt > 0L && shrunkAt <= 2L * WarriorsCompanionLayout.SIZE_HOLD_MS + 50L, "a real shrink lands: " + shrunkAt);
        h.sample(900f, 224f, 9000L);
        check(h.w() == 900f, "growth is immediate");
        WarriorsCompanionLayout.Presence p = new WarriorsCompanionLayout.Presence();
        check(!p.update(false, 0L) && p.update(true, 10L), "presence starts absent, shows at once");
        check(p.update(false, 110L), "a 100 ms drop-out is not an absence");
        check(p.update(true, 120L) && p.update(false, 200L) && !p.update(false, 300L), "a 180 ms drop-out is");
    }

    /** Scene frame: rescan the strip every RESCAN_MS like the view does, then step. */
    private static boolean frame(WarriorsCompanionLayout.Scene sc, long t, long[] lastScan, boolean hudOn, int[] strip,
                                 boolean mapIn, int tab, boolean dim, boolean objIn, int objW) {
        if (hudOn && (lastScan[0] < 0L || t - lastScan[0] >= WarriorsCompanionLayout.RESCAN_MS)) {
            sc.hudScan().scan(strip, HUD_W, HUD_H);
            sc.ingestHud(t);
            lastScan[0] = t;
        }
        return sc.step(t, tab, dim, mapIn, hudOn, objIn, objW, 224);
    }

    /** Worst intrusion (px) of a visible HUD cluster into the visible minimap circle (negative: clear). */
    private static float intrusion(WarriorsCompanionLayout.Scene sc, long t) {
        float cx = (sc.map.left(t) + sc.map.right(t)) * 0.5f, cy = (sc.map.top(t) + sc.map.bottom(t)) * 0.5f;
        float rad = (sc.map.right(t) - sc.map.left(t)) * 0.5f, worst = -Float.MAX_VALUE;
        for (int i = 0; i < WarriorsCompanionLayout.MAX_CLUSTERS; i++) {
            WarriorsCompanionLayout.Slot p = sc.hudParts[i];
            if (p.alpha(t) <= WarriorsCompanionLayout.VISIBLE_ALPHA) continue;
            float qx = Math.max(p.left(t), Math.min(cx, p.right(t))), qy = Math.max(p.top(t), Math.min(cy, p.bottom(t)));
            worst = Math.max(worst, rad - (float) Math.hypot(qx - cx, qy - cy));
        }
        return worst;
    }

    private static float runHudToggle(long frameMs, float[] mapCxAt1056) {
        WarriorsCompanionLayout.Scene sc = new WarriorsCompanionLayout.Scene();
        int[] strip = hudStrip(false, 274);
        long[] lastScan = {-1L};
        float worst = -Float.MAX_VALUE, objMin = Float.MAX_VALUE, objMax = -Float.MAX_VALUE;
        boolean stillAt3200 = true;
        int bothVisible = 0;
        for (long t = 0L; t <= 4000L; t += frameMs) {
            boolean hudOn = t >= 528L && t < 2112L;
            if (!hudOn) lastScan[0] = -1L;
            int objW = (t / frameMs) % 2L == 0L ? 1152 : 700;
            boolean moving = frame(sc, t, lastScan, hudOn, strip, true, 0, false, true, objW);
            if (t == 1056L) mapCxAt1056[0] = (sc.map.left(t) + sc.map.right(t)) * 0.5f;
            if (sc.hudAlpha(t) > WarriorsCompanionLayout.VISIBLE_ALPHA && sc.map.alpha(t) > WarriorsCompanionLayout.VISIBLE_ALPHA) {
                worst = Math.max(worst, intrusion(sc, t));
                bothVisible++;
            }
            if (t >= 400L) { objMin = Math.min(objMin, sc.obj.left(t)); objMax = Math.max(objMax, sc.obj.left(t)); }
            if (t >= 3200L && moving) stillAt3200 = false;
        }
        check(bothVisible > 1000 / (int) frameMs, "hud and minimap were shown together for over a second");
        check(objMax - objMin < E, "objective slot does not pulse with per-frame width jitter");
        check(stillAt3200, "scene comes to rest after the HUD leaves (" + frameMs + " ms frames)");
        float cx = (sc.map.left(4000L) + sc.map.right(4000L)) * 0.5f;
        check(Math.abs(cx - WarriorsCompanionLayout.mapCenterX(0f)) < E, "minimap back in the centre without hud");
        return worst;
    }

    private static void testSceneSequencing() {
        float[] cxFast = new float[1], cxSlow = new float[1];
        float worstFast = runHudToggle(16L, cxFast), worstSlow = runHudToggle(33L, cxSlow);
        check(worstFast < 0f && worstSlow < 0f,
                "hud clusters never enter the minimap circle while both visible: " + worstFast + " / " + worstSlow);
        check(Math.abs(cxFast[0] - cxSlow[0]) < 0.5f, "minimap position at 1056 ms does not depend on frame rate");
        WarriorsCompanionLayout.Scene sc = new WarriorsCompanionLayout.Scene();
        int[] strip = hudStrip(false, 274);
        long[] ls = {-1L};
        frame(sc, 0L, ls, false, strip, true, 0, false, false, 0);
        check(!frame(sc, 1000L, ls, false, strip, true, 0, false, false, 0), "nothing moves once settled");
        frame(sc, 1000L, ls, true, strip, true, 0, false, false, 0);
        float startCx = (sc.map.left(1000L) + sc.map.right(1000L)) * 0.5f;
        float midCx = (sc.map.left(1100L) + sc.map.right(1100L)) * 0.5f;
        float endCx = WarriorsCompanionLayout.mapCenterX(WarriorsCompanionLayout.hudColumnRight());
        check(Math.abs(startCx - WarriorsCompanionLayout.mapCenterX(0f)) < E
                && midCx > startCx + 20f && midCx < endCx - 5f,
                "minimap recentres smoothly, not by snapping: " + startCx + " -> " + midCx);
        check(sc.hudAlpha(1000L) == 0f, "hud waits for the minimap to clear its column");
        for (long t = 1016L; t <= 1600L; t += 16L) frame(sc, t, ls, true, strip, true, 0, false, false, 0);
        check(Math.abs((sc.map.left(1600L) + sc.map.right(1600L)) * 0.5f - endCx) < E && sc.hudAlpha(1600L) > 0.5f,
                "minimap settles beside the hud, hud fades in");
        frame(sc, 1600L, ls, true, strip, true, 1, true, false, 0);
        float t1 = sc.tabT(1700L), d1 = sc.dimT(1700L);
        check(t1 > 0.2f && t1 < 1f && d1 > 0.2f && d1 < 1f, "tab switch and dim veil ease in");
        check(sc.tabT(2100L) == 1f && sc.dimT(2100L) == 1f, "tab switch and dim veil finish");
        frame(sc, 2084L, ls, false, strip, true, 0, false, false, 0);
        frame(sc, 2100L, ls, false, strip, false, 0, false, false, 0);
        check(sc.map.alpha(2120L) > 0.9f, "a one-frame stream drop-out does not fade the minimap");
        frame(sc, 2400L, ls, false, strip, false, 0, false, false, 0);
        check(sc.map.alpha(2700L) < WarriorsCompanionLayout.VISIBLE_ALPHA, "a real absence fades the minimap out");
    }

    public static void main(String[] args) {
        testStaticLayoutAllStates();
        testStatusAndHitTests();
        testSpringEase();
        testSpringRetargetAndRateIndependence();
        testHoldAndPresence();
        testClusterDetection();
        testClusterFit();
        testClusterHold();
        testPinnedBottomCluster();
        testClusterSizeLimits();
        testSceneSequencing();
        float[] r = new float[4];
        WarriorsCompanionLayout.mapRect(0f, r);
        System.out.println("map(no hud)=" + r[0] + "," + r[1] + "," + r[2] + "," + r[3]);
        WarriorsCompanionLayout.mapRect(WarriorsCompanionLayout.hudColumnRight(), r);
        System.out.println("map(hud)=" + r[0] + "," + r[1] + "," + r[2] + "," + r[3]);
        WarriorsCompanionLayout.objectiveSlot(1152, 224, r);
        System.out.println("objective(1152x224)=" + r[0] + "," + r[1] + "," + r[2] + "," + r[3]);
        System.out.println("WarriorsCompanionLayoutTest: PASS (" + checks + " checks)");
    }
}
