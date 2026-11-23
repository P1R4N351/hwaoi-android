// SPDX-License-Identifier: MIT
package dev.nxnative.android;

import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.Typeface;
import java.util.Locale;

/**
 * Procedural Canvas drawing routines for {@link WarriorsCompanionView}.
 * Uses zero external image/font assets on a fixed 1240x1080 logical grid.
 */
final class WarriorsCompanionDraw {
    static final float LOGIC_W = 1240f, LOGIC_H = 1080f;
    private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF rf = new RectF();
    private final Path path = new Path();
    private final float[] proj = new float[3];

    void drawAll(Canvas c, WarriorsCompanionModel m, int w, int h, long nowMs) {
        c.drawColor(0xFF060709);
        if (w <= 0 || h <= 0) return;
        float sc = Math.min(w / LOGIC_W, h / LOGIC_H);
        float ox = (w - LOGIC_W * sc) * 0.5f + m.orbitDx();
        float oy = (h - LOGIC_H * sc) * 0.5f + m.orbitDy();
        c.save();
        c.translate(ox, oy);
        c.scale(sc, sc, 0f, 0f);
        drawFrame(c, m, nowMs);
        if (m.dimmed()) drawDimVeil(c, m);
        c.restore();
    }

    private void drawFrame(Canvas c, WarriorsCompanionModel m, long nowMs) {
        p.setStyle(Paint.Style.FILL);
        p.setColor(m.bgColor());
        c.drawRect(0f, 0f, LOGIC_W, LOGIC_H, p);
        drawHeader(c, m);
        int tab = m.activeTab();
        if (tab == WarriorsCompanionModel.TAB_TACTICAL) drawTacticalTab(c, m, nowMs);
        else if (tab == WarriorsCompanionModel.TAB_COMMAND) drawCommandTab(c, m);
        else if (tab == WarriorsCompanionModel.TAB_GRIMOIRE) drawGrimoireTab(c, m);
        else drawLedgerTab(c, m);
    }

    private void drawHeader(Canvas c, WarriorsCompanionModel m) {
        p.setStyle(Paint.Style.FILL);
        p.setColor(m.cardColor());
        c.drawRect(0f, 0f, LOGIC_W, 92f, p);
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(2f);
        p.setColor(m.borderColor());
        c.drawLine(0f, 92f, LOGIC_W, 92f, p);

        boolean aoc = m.gameKind == WarriorsCompanionModel.GAME_AOC;
        drawCrest(c, 50f, 50f, 21f, m.primaryColor(), aoc);
        drawText(c, aoc ? "SHEIKAH SLATE" : "ZONAI PAD", 86f, 44f, 21f, 0xFFF2F5F7, true, Paint.Align.LEFT);
        drawText(c, aoc ? "AGE OF CALAMITY" : "AGE OF IMPRISONMENT", 86f, 67f, 14f, m.primaryColor(), true, Paint.Align.LEFT);

        for (int i = 0; i < WarriorsCompanionModel.TAB_COUNT; i++) {
            float l = 476f + i * 154f;
            boolean sel = m.activeTab() == i;
            drawButton(c, l, 18f, l + 146f, 82f, m, sel, sel ? m.primaryColor() : m.borderColor(),
                    WarriorsCompanionModel.tabName(i), null, 20f);
        }
        int sc = m.writeSafe ? 0xFF34D399 : (m.moviePlaying ? 0xFFF59E0B : 0xFFEF4444);
        String st = m.moviePlaying ? "MOVIE" : m.hostPaused ? "PAUSED" : m.writeSafe ? "LIVE" : "WAIT";
        String fps = m.fps > 0.5f ? String.format(Locale.ROOT, "%.0f FPS", m.fps) : "-- FPS";
        drawPill(c, 1094f, 18f, 1216f, 82f, 0xFF0A0D12, sc, st, fps);
    }

    private void drawCrest(Canvas c, float cx, float cy, float r, int col, boolean starGear) {
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(2.6f);
        p.setColor(col);
        c.drawCircle(cx, cy, r, p);
        if (starGear) {
            for (int i = 0; i < 8; i++) {
                double a = i * Math.PI / 4.0;
                c.drawLine(cx + (float) Math.cos(a) * r * 0.65f, cy + (float) Math.sin(a) * r * 0.65f,
                        cx + (float) Math.cos(a) * r * 1.15f, cy + (float) Math.sin(a) * r * 1.15f, p);
            }
        } else {
            p.setColor(0xFFE8B84B);
            c.drawCircle(cx, cy, r * 0.52f, p);
            c.drawLine(cx - r * 0.8f, cy, cx + r * 0.8f, cy, p);
        }
    }

    private void drawTacticalTab(Canvas c, WarriorsCompanionModel m, long nowMs) {
        drawCard(c, 24f, 108f, 736f, 936f, m);
        drawRadar(c, m, 380f, 530f, 326f);
        drawRadarFooter(c, m);
        drawTagPalette(c, m);
        drawPinList(c, m);
        drawStopwatchCard(c, m, nowMs);
        drawKeepCard(c, m);
    }

    private void drawRadar(Canvas c, WarriorsCompanionModel m, float cx, float cy, float R) {
        drawText(c, "BATTLEFIELD RADAR \u00b7 " + (int) m.radiusMeters() + "m", 44f, 146f, 20f, m.primaryColor(), true, Paint.Align.LEFT);
        drawText(c, String.format(Locale.ROOT, "INERTIAL STICK VECTOR  HDG %03d\u00b0", Math.round(m.yawDeg) % 360),
                716f, 146f, 18f, 0xFF94A3B8, false, Paint.Align.RIGHT);

        p.setStyle(Paint.Style.FILL);
        p.setColor(0xFF090D14);
        c.drawCircle(cx, cy, R, p);
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(1.6f);
        p.setColor(m.borderColor());
        c.drawCircle(cx, cy, R, p);
        c.drawCircle(cx, cy, R * 0.66f, p);
        c.drawCircle(cx, cy, R * 0.33f, p);
        c.drawLine(cx - R, cy, cx + R, cy, p);
        c.drawLine(cx, cy - R, cx, cy + R, p);

        drawCompassTicks(c, m, cx, cy, R);
        drawTrailPath(c, m, cx, cy, R);
        drawPinMarkers(c, m, cx, cy, R);
        drawPlayerChevron(c, m, cx, cy);
    }

    private void drawCompassTicks(Canvas c, WarriorsCompanionModel m, float cx, float cy, float R) {
        float rot = m.headingUp ? -m.yawDeg : 0f;
        for (int i = 0; i < 4; i++) {
            String lbl = i == 0 ? "N" : i == 1 ? "E" : i == 2 ? "S" : "W";
            double rad = Math.toRadians(-90.0 + i * 90.0 + rot);
            float tx = cx + (float) Math.cos(rad) * (R - 18f);
            float ty = cy + (float) Math.sin(rad) * (R - 18f) + 6f;
            drawText(c, lbl, tx, ty, 18f, i == 0 ? m.secondaryColor() : 0xFF94A3B8, i == 0, Paint.Align.CENTER);
        }
    }

    private void drawTrailPath(Canvas c, WarriorsCompanionModel m, float cx, float cy, float R) {
        int n = m.trailCount;
        if (n < 2) return;
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeCap(Paint.Cap.ROUND);
        float px = 0f, py = 0f;
        boolean hasPrev = false;
        for (int i = 0; i < n; i++) {
            if (!m.projectXZ(m.trailX[i], m.trailZ[i], proj)) continue;
            float sx = cx + proj[0] * R, sy = cy + proj[1] * R;
            if (hasPrev) {
                int alpha = 55 + (i * 190) / Math.max(1, n - 1);
                p.setStrokeWidth(3.2f);
                p.setColor((m.primaryColor() & 0x00FFFFFF) | (alpha << 24));
                c.drawLine(px, py, sx, sy, p);
            }
            px = sx; py = sy; hasPrev = true;
        }
    }

    private void drawPinMarkers(Canvas c, WarriorsCompanionModel m, float cx, float cy, float R) {
        for (int i = 0; i < WarriorsCompanionModel.PIN_CAP; i++) {
            if (!m.pinActive[i] || !m.projectXZ(m.pinX[i], m.pinZ[i], proj)) continue;
            float sx = cx + proj[0] * R, sy = cy + proj[1] * R;
            p.setStyle(Paint.Style.FILL);
            p.setColor(proj[2] > 0.5f ? 0xFFF59E0B : m.secondaryColor());
            c.drawCircle(sx, sy, 9f, p);
            drawText(c, m.pinLabels[i], sx, sy - 14f, 16f, 0xFFF8FAFC, true, Paint.Align.CENTER);
        }
    }

    private void drawPlayerChevron(Canvas c, WarriorsCompanionModel m, float cx, float cy) {
        float ang = m.headingUp ? 0f : m.yawDeg;
        c.save();
        c.rotate(ang, cx, cy);
        path.reset();
        path.moveTo(cx, cy - 18f);
        path.lineTo(cx + 13f, cy + 14f);
        path.lineTo(cx, cy + 6f);
        path.lineTo(cx - 13f, cy + 14f);
        path.close();
        p.setStyle(Paint.Style.FILL);
        p.setColor(0xFFFFFFFF);
        c.drawPath(path, p);
        c.restore();
    }

    private void drawRadarFooter(Canvas c, WarriorsCompanionModel m) {
        drawButton(c, 24f, 952f, 194f, 1048f, m, false, m.borderColor(),
                "ZOOM " + (int) m.radiusMeters() + "m", "CYCLE RANGE", 19f);
        drawButton(c, 204f, 952f, 374f, 1048f, m, m.headingUp, m.primaryColor(),
                m.headingUp ? "HEADING-UP" : "NORTH-UP", "ORIENTATION", 19f);
        drawButton(c, 384f, 952f, 554f, 1048f, m, true, m.secondaryColor(),
                "DROP " + WarriorsCompanionModel.pinPreset(m.pinPresetIdx), "PINS " + m.pinCount() + "/8", 19f);
        drawButton(c, 564f, 952f, 736f, 1048f, m, false, 0xFF7F1D1D,
                "CLEAR PINS", "REMOVE ALL", 19f);
    }

    private void drawTagPalette(Canvas c, WarriorsCompanionModel m) {
        drawCard(c, 756f, 108f, 1216f, 290f, m);
        drawText(c, "QUICK TACTICAL PINS", 774f, 142f, 19f, m.primaryColor(), true, Paint.Align.LEFT);
        for (int i = 0; i < 5; i++) {
            int col = i % 3, row = i / 3;
            float l = 774f + col * 144f, t = 160f + row * 60f;
            boolean sel = m.pinPresetIdx == i;
            drawButton(c, l, t, l + 136f, t + 50f, m, sel, sel ? m.secondaryColor() : m.borderColor(),
                    WarriorsCompanionModel.pinPreset(i), null, 18f);
        }
    }

    private void drawPinList(Canvas c, WarriorsCompanionModel m) {
        drawCard(c, 756f, 306f, 1216f, 626f, m);
        drawText(c, "ACTIVE WAYPOINTS (" + m.pinCount() + "/8)", 774f, 340f, 19f, m.primaryColor(), true, Paint.Align.LEFT);
        int row = 0;
        for (int i = 0; i < WarriorsCompanionModel.PIN_CAP && row < 6; i++) {
            if (!m.pinActive[i]) continue;
            float top = 358f + row * 42f;
            drawRectOutline(c, 774f, top, 1198f, top + 36f, 0xFF0D1117, m.borderColor());
            drawText(c, (i + 1) + ". " + m.pinLabels[i], 788f, top + 25f, 18f, 0xFFF1F5F9, true, Paint.Align.LEFT);
            drawText(c, m.formatPinDelta(i), 1138f, top + 25f, 18f, m.secondaryColor(), false, Paint.Align.RIGHT);
            drawText(c, "\u00d7", 1176f, top + 26f, 22f, 0xFFEF4444, true, Paint.Align.CENTER);
            row++;
        }
        if (row == 0) {
            drawText(c, "Tap a tag above to mark current position", 986f, 488f, 19f, 0xFF64748B, false, Paint.Align.CENTER);
        }
    }

    private void drawStopwatchCard(Canvas c, WarriorsCompanionModel m, long nowMs) {
        drawCard(c, 756f, 642f, 1216f, 836f, m);
        drawText(c, "BATTLE STOPWATCH", 774f, 676f, 19f, m.primaryColor(), true, Paint.Align.LEFT);
        drawText(c, m.formatTimer(nowMs), 986f, 744f, 44f, 0xFFFFFFFF, true, Paint.Align.CENTER);
        drawButton(c, 774f, 766f, 980f, 822f, m, m.timerRunning(), m.primaryColor(),
                m.timerRunning() ? "PAUSE" : "START", null, 20f);
        drawButton(c, 992f, 766f, 1198f, 822f, m, false, m.borderColor(), "RESET", null, 20f);
    }

    private void drawKeepCard(Canvas c, WarriorsCompanionModel m) {
        drawCard(c, 756f, 852f, 1216f, 1048f, m);
        drawText(c, "KEEPS / OBJECTIVES", 774f, 886f, 19f, m.primaryColor(), true, Paint.Align.LEFT);
        drawText(c, String.valueOf(m.keepCount), 986f, 954f, 44f, m.secondaryColor(), true, Paint.Align.CENTER);
        drawButton(c, 774f, 978f, 868f, 1034f, m, false, m.borderColor(), "-1", null, 22f);
        drawButton(c, 878f, 978f, 972f, 1034f, m, false, m.primaryColor(), "+1", null, 22f);
        drawButton(c, 982f, 978f, 1076f, 1034f, m, false, m.secondaryColor(), "+10", null, 22f);
        drawButton(c, 1086f, 978f, 1198f, 1034f, m, false, 0xFF7F1D1D, "ZERO", null, 19f);
    }

    private void drawCommandTab(Canvas c, WarriorsCompanionModel m) {
        drawText(c, "PARTY SWITCHER  (ZERO-PAUSE D-PAD)", 24f, 138f, 20f, m.primaryColor(), true, Paint.Align.LEFT);
        for (int i = 0; i < 4; i++) {
            float l = 24f + i * 300f;
            boolean sel = m.activeParty == i;
            drawButton(c, l, 152f, l + 292f, 290f, m, sel, sel ? m.primaryColor() : m.borderColor(),
                    "WARRIOR " + (i + 1), WarriorsCompanionModel.partyDirLabel(i), 26f);
        }
        drawDeckCard(c, m, 24f, 310f, 610f, 842f, true);
        drawDeckCard(c, m, 630f, 310f, 1216f, 842f, false);
        drawUtilityStrip(c, m);
    }

    private void drawDeckCard(
        Canvas c, WarriorsCompanionModel m, float l, float t, float r, float b, boolean rDeck) {
        drawCard(c, l, t, r, b, m);
        String hdr = rDeck ? (m.gameKind == WarriorsCompanionModel.GAME_AOC ? "SHEIKAH RUNES  [R + FACE]" : "SYNC & ZONAI ARTS  [R + FACE]")
                : (m.gameKind == WarriorsCompanionModel.GAME_AOC ? "ELEMENTAL RODS & HEAL  [L + FACE]" : "ZONAI DEVICES & RATION  [L + FACE]");
        drawText(c, hdr, l + 20f, t + 42f, 21f, rDeck ? m.primaryColor() : m.secondaryColor(), true, Paint.Align.LEFT);
        for (int i = 0; i < 4; i++) {
            int col = i & 1, row = i >> 1;
            float bl = l + 18f + col * 280f, bt = t + 66f + row * 224f;
            String title = rDeck ? WarriorsCompanionModel.rDeckTitle(m.gameKind, i)
                    : WarriorsCompanionModel.lDeckTitle(m.gameKind, i);
            String chord = WarriorsCompanionModel.deckChord(rDeck ? "R" : "L", i);
            int tag = (rDeck ? WarriorsCompanionModel.ACT_R_BASE : WarriorsCompanionModel.ACT_L_BASE) + i;
            boolean firing = m.seqActive && m.seqTag == tag;
            drawButton(c, bl, bt, bl + 266f, bt + 204f, m, firing,
                    firing ? 0xFFFFFFFF : (rDeck ? m.primaryColor() : m.secondaryColor()), title, chord, 25f);
        }
    }

    private void drawUtilityStrip(Canvas c, WarriorsCompanionModel m) {
        drawButton(c, 24f, 868f, 316f, 1040f, m, m.seqActive && m.seqTag == WarriorsCompanionModel.ACT_LOCKON,
                m.primaryColor(), "LOCK-ON", "RIGHT STICK PRESS", 24f);
        drawButton(c, 324f, 868f, 616f, 1040f, m, m.seqActive && m.seqTag == WarriorsCompanionModel.ACT_SPECIAL,
                m.secondaryColor(), "SPECIAL MUSOU", "FACE [A]", 24f);
        drawButton(c, 624f, 868f, 916f, 1040f, m, m.seqActive && m.seqTag == WarriorsCompanionModel.ACT_DODGE,
                m.primaryColor(), "DODGE / CANCEL", "FACE [B]", 24f);
        drawButton(c, 924f, 868f, 1216f, 1040f, m, m.seqActive,
                m.seqActive ? 0xFFEF4444 : 0xFF7F1D1D, "ABORT CHORD", m.seqActive ? "CANCEL ACTIVE" : "IDLE", 24f);
    }

    private void drawGrimoireTab(Canvas c, WarriorsCompanionModel m) {
        drawCard(c, 24f, 108f, 1216f, 188f, m);
        drawText(c, "MUSOU COMBO GRIMOIRE  (Y \u2192 X STRINGS)", 44f, 156f, 22f, m.primaryColor(), true, Paint.Align.LEFT);
        drawButton(c, 680f, 118f, 990f, 178f, m, m.macroMode, m.secondaryColor(),
                m.macroMode ? "MODE: 1-TAP MACRO" : "MODE: REFERENCE", null, 19f);
        drawButton(c, 1006f, 118f, 1200f, 178f, m, m.seqActive, 0xFFEF4444, "ABORT", null, 20f);

        for (int i = 0; i < 6; i++) {
            int col = i % 3, row = i / 3;
            float l = 24f + col * 402f, t = 208f + row * 372f;
            boolean active = m.seqActive && m.seqTag == (WarriorsCompanionModel.ACT_COMBO_BASE + i);
            drawComboCard(c, m, i, l, t, l + 388f, t + 352f, active);
        }
        drawCard(c, 24f, 966f, 1216f, 1048f, m);
        String msg = m.seqActive ? String.format(Locale.ROOT, "EXECUTING TAG %d  (STEP %d / %d)  \u2014  MANUAL PRESS ABORTS",
                m.seqTag, m.seqStep + 1, Math.max(1, m.seqCount))
                : "Manual A / B / X / Y / L / R presses immediately abort any running macro chord.";
        drawText(c, msg, 620f, 1016f, 20f, m.seqActive ? m.secondaryColor() : 0xFF94A3B8, m.seqActive, Paint.Align.CENTER);
    }

    private void drawComboCard(
        Canvas c, WarriorsCompanionModel m, int idx, float l, float t, float r, float b, boolean active) {
        drawCardHighlight(c, l, t, r, b, m, active ? m.secondaryColor() : m.borderColor(), active);
        drawText(c, WarriorsCompanionModel.comboTitle(idx), l + 24f, t + 54f, 34f, m.primaryColor(), true, Paint.Align.LEFT);
        drawText(c, WarriorsCompanionModel.comboRole(idx), r - 20f, t + 50f, 20f, 0xFFF8FAFC, true, Paint.Align.RIGHT);
        drawComboNodes(c, m, idx, l + 36f, t + 184f, active);
        drawText(c, WarriorsCompanionModel.comboChain(idx), (l + r) * 0.5f, t + 286f, 24f, 0xFFCBD5E1, false, Paint.Align.CENTER);
        String hint = m.macroMode ? (active ? "RUNNING..." : "TAP TO EXECUTE") : "INSPECT ONLY";
        drawText(c, hint, (l + r) * 0.5f, t + 328f, 18f, active ? m.secondaryColor() : 0xFF64748B, active, Paint.Align.CENTER);
    }

    private void drawComboNodes(
        Canvas c, WarriorsCompanionModel m, int idx, float startX, float cy, boolean active) {
        int steps = idx + 1;
        float stepW = Math.min(54f, 310f / Math.max(1, steps));
        for (int s = 0; s < steps; s++) {
            float nx = startX + s * stepW + 18f;
            boolean isX = (s == idx);
            boolean hit = active && m.seqStep == s;
            int col = hit ? 0xFFFFFFFF : isX ? m.secondaryColor() : m.primaryColor();
            p.setStyle(hit ? Paint.Style.FILL : Paint.Style.STROKE);
            p.setStrokeWidth(2.6f);
            p.setColor(col);
            c.drawCircle(nx, cy, isX ? 21f : 18f, p);
            drawText(c, isX ? "X" : "Y", nx, cy + 7f, 20f, hit ? 0xFF000000 : col, true, Paint.Align.CENTER);
        }
    }

    private void drawLedgerTab(Canvas c, WarriorsCompanionModel m) {
        drawSparklineCard(c, m);
        drawEngineCard(c, m);
        drawSaveCard(c, m);
        drawBifrostCard(c, m);
    }

    private void drawSparklineCard(Canvas c, WarriorsCompanionModel m) {
        drawCard(c, 24f, 108f, 740f, 560f, m);
        drawText(c, "FRAME PACING & SPARKLINE", 46f, 146f, 20f, m.primaryColor(), true, Paint.Align.LEFT);
        String headline = m.fps > 0.5f
                ? String.format(Locale.ROOT, "%.1f FPS   \u00b7   %.1f ms", m.fps, m.frameMs)
                : "--.- FPS   \u00b7   --.- ms";
        drawText(c, headline, 46f, 210f, 38f, 0xFFFFFFFF, true, Paint.Align.LEFT);

        float gx = 46f, gy = 250f, gw = 670f, gh = 270f;
        drawRectOutline(c, gx, gy, gx + gw, gy + gh, 0xFF0A0E14, m.borderColor());
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(1.5f);
        p.setColor(0xFF334155);
        c.drawLine(gx, gy + gh * 0.5f, gx + gw, gy + gh * 0.5f, p);
        drawText(c, "60", gx + 8f, gy + 22f, 15f, 0xFF64748B, false, Paint.Align.LEFT);
        drawText(c, "30", gx + 8f, gy + gh * 0.5f - 6f, 15f, 0xFF64748B, false, Paint.Align.LEFT);

        if (m.fpsHistCount < 2) return;
        p.setColor(m.primaryColor());
        p.setStrokeWidth(3f);
        int start = (m.fpsHistHead + WarriorsCompanionModel.FPS_CAP - m.fpsHistCount) % WarriorsCompanionModel.FPS_CAP;
        float px = 0f, py = 0f;
        for (int i = 0; i < m.fpsHistCount; i++) {
            float v = Math.max(0f, Math.min(60f, m.fpsHist[(start + i) % WarriorsCompanionModel.FPS_CAP]));
            float sx = gx + (i * gw) / Math.max(1, WarriorsCompanionModel.FPS_CAP - 1);
            float sy = gy + gh - (v / 60f) * gh;
            if (i > 0) c.drawLine(px, py, sx, sy, p);
            px = sx; py = sy;
        }
    }

    private void drawEngineCard(Canvas c, WarriorsCompanionModel m) {
        drawCard(c, 756f, 108f, 1216f, 560f, m);
        drawText(c, "ENGINE & PRESENTATION STATE", 778f, 146f, 20f, m.primaryColor(), true, Paint.Align.LEFT);
        String tid = m.gameKind == WarriorsCompanionModel.GAME_AOC ? "01002B00111A2000 (1.3.0)" : "04003F40232DE000 (v196608)";
        drawKvRow(c, 778f, 1196f, 210f, "TITLE ID", tid, 0xFFF1F5F9);
        drawKvRow(c, 778f, 1196f, 270f, "STATUS", m.writeSafe ? "READY / ACTIVE" : m.moviePlaying ? "CUTSCENE MOVIE" : "WAITING", m.primaryColor());
        drawKvRow(c, 778f, 1196f, 330f, "PRESENTED", String.valueOf(m.presents), 0xFFF1F5F9);
        drawKvRow(c, 778f, 1196f, 390f, "VIDEO DECODER", m.moviePlaying ? "ACTIVE (AUTO-DIM)" : "IDLE", m.moviePlaying ? m.secondaryColor() : 0xFF94A3B8);
        drawKvRow(c, 778f, 1196f, 450f, "PHYSICAL INPUT", m.inputActive ? "LIVE SIGNAL" : "IDLE", m.inputActive ? 0xFF34D399 : 0xFF94A3B8);
        drawKvRow(c, 778f, 1196f, 510f, "RENDER PATH", "Procedural Canvas (Zero Assets)", 0xFF94A3B8);
    }

    private void drawSaveCard(Canvas c, WarriorsCompanionModel m) {
        drawCard(c, 24f, 580f, 610f, 1048f, m);
        drawText(c, "SAVE DATA LEDGER", 46f, 618f, 20f, m.primaryColor(), true, Paint.Align.LEFT);
        drawKvRow(c, 46f, 590f, 690f, "STORE BLOB", m.saveFileName(), 0xFFF8FAFC);
        drawKvRow(c, 46f, 590f, 760f, "SESSION COMMITS", String.valueOf(m.saveCommits), m.secondaryColor());
        String age = m.lastSaveAgeS < 0 ? "No commit this session" : (m.lastSaveAgeS + "s ago");
        drawKvRow(c, 46f, 590f, 830f, "LAST AUTOSAVE", age, 0xFFF1F5F9);
        drawKvRow(c, 46f, 590f, 900f, "STORAGE STATE", m.saveBusy ? "WRITING..." : "IDLE / DURABLE", m.saveBusy ? 0xFFF59E0B : 0xFF34D399);
    }

    private void drawBifrostCard(Canvas c, WarriorsCompanionModel m) {
        drawCard(c, 630f, 580f, 1216f, 1048f, m);
        drawText(c, "COMPANION & BIFROST SYNC", 652f, 618f, 20f, m.primaryColor(), true, Paint.Align.LEFT);
        drawButton(c, 652f, 650f, 1194f, 742f, m, m.autoDim, m.primaryColor(),
                "CUTSCENE AUTO-DIM: " + (m.autoDim ? "ON" : "OFF"), "TAP TO TOGGLE MOVIE DIMMING", 21f);
        drawKvRow(c, 652f, 1196f, 800f, "STICK LED THEME", m.bifrostLookDesc(), m.secondaryColor());
        drawKvRow(c, 652f, 1196f, 870f, "AMOLED ORBIT", String.format(Locale.ROOT, "(%d, %d) px / 60s", m.orbitDx(), m.orbitDy()), 0xFF94A3B8);
        drawKvRow(c, 652f, 1196f, 940f, "COMPLIANCE", "MIT Clean-Room / Host-Safe", 0xFF34D399);
    }

    private void drawDimVeil(Canvas c, WarriorsCompanionModel m) {
        p.setStyle(Paint.Style.FILL);
        p.setColor(0xC2050608);
        c.drawRect(0f, 92f, LOGIC_W, LOGIC_H, p);
        drawPill(c, 430f, 480f, 810f, 600f, m.cardColor(), m.secondaryColor(),
                "CUTSCENE \u00b7 AUTO-DIM", "INPUT CHORDS LOCKED");
    }

    private void drawCard(Canvas c, float l, float t, float r, float b, WarriorsCompanionModel m) {
        drawCardHighlight(c, l, t, r, b, m, m.borderColor(), false);
    }

    private void drawCardHighlight(
        Canvas c, float l, float t, float r, float b, WarriorsCompanionModel m, int border, boolean thick) {
        rf.set(l, t, r, b);
        p.setStyle(Paint.Style.FILL);
        p.setColor(m.cardColor());
        c.drawRoundRect(rf, 14f, 14f, p);
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(thick ? 3.5f : 1.8f);
        p.setColor(border);
        c.drawRoundRect(rf, 14f, 14f, p);
    }

    private void drawRectOutline(Canvas c, float l, float t, float r, float b, int fill, int border) {
        rf.set(l, t, r, b);
        p.setStyle(Paint.Style.FILL);
        p.setColor(fill);
        c.drawRoundRect(rf, 8f, 8f, p);
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(1.5f);
        p.setColor(border);
        c.drawRoundRect(rf, 8f, 8f, p);
    }

    private void drawButton(
        Canvas c, float l, float t, float r, float b, WarriorsCompanionModel m,
        boolean lit, int accent, String title, String sub, float titleSp) {
        rf.set(l, t, r, b);
        p.setStyle(Paint.Style.FILL);
        p.setColor(lit ? ((accent & 0x00FFFFFF) | 0x33000000) : 0xFF0E1117);
        c.drawRoundRect(rf, 12f, 12f, p);
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(lit ? 3f : 1.8f);
        p.setColor(lit ? accent : m.borderColor());
        c.drawRoundRect(rf, 12f, 12f, p);

        float cx = (l + r) * 0.5f, cy = (t + b) * 0.5f;
        if (sub == null) {
            drawText(c, title, cx, cy + titleSp * 0.34f, titleSp, lit ? 0xFFFFFFFF : 0xFFE2E8F0, true, Paint.Align.CENTER);
        } else {
            drawText(c, title, cx, cy - 4f, titleSp, lit ? 0xFFFFFFFF : 0xFFE2E8F0, true, Paint.Align.CENTER);
            drawText(c, sub, cx, cy + 24f, Math.max(15f, titleSp - 7f), accent, false, Paint.Align.CENTER);
        }
    }

    private void drawPill(
        Canvas c, float l, float t, float r, float b, int fill, int accent, String line1, String line2) {
        rf.set(l, t, r, b);
        p.setStyle(Paint.Style.FILL);
        p.setColor(fill);
        c.drawRoundRect(rf, 12f, 12f, p);
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(2.4f);
        p.setColor(accent);
        c.drawRoundRect(rf, 12f, 12f, p);
        float cx = (l + r) * 0.5f, cy = (t + b) * 0.5f;
        drawText(c, line1, cx, cy - 4f, 19f, accent, true, Paint.Align.CENTER);
        drawText(c, line2, cx, cy + 20f, 16f, 0xFFCBD5E1, false, Paint.Align.CENTER);
    }

    private void drawKvRow(Canvas c, float x, float rx, float y, String k, String v, int vc) {
        drawText(c, k, x, y, 17f, 0xFF94A3B8, true, Paint.Align.LEFT);
        drawText(c, v, rx, y, 19f, vc, true, Paint.Align.RIGHT);
    }

    private void drawText(
        Canvas c, String s, float x, float y, float sz, int col, boolean bold, Paint.Align align) {
        p.setStyle(Paint.Style.FILL);
        p.setColor(col);
        p.setTextSize(sz);
        p.setTextAlign(align);
        p.setTypeface(bold ? Typeface.DEFAULT_BOLD : Typeface.DEFAULT);
        c.drawText(s != null ? s : "", x, y, p);
    }
}
