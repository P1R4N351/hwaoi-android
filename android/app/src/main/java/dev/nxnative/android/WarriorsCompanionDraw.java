// SPDX-License-Identifier: MIT
package dev.nxnative.android;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.Typeface;
import java.util.Locale;

/**
 * Canvas drawing routines for {@link WarriorsCompanionView}.
 * Renders the harvested target-game backdrop/emblem, live mirrored game circular minimap,
 * tactical action decks, combo cards, and migrated Display 0 dev telemetry on a 1240x1080 grid.
 */
final class WarriorsCompanionDraw {
    static final float LOGIC_W = 1240f, LOGIC_H = 1080f;
    private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint imgPaint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final RectF rf = new RectF();
    private final Rect dstRect = new Rect();
    private final Path clipPath = new Path();

    void drawAll(
        Canvas c, WarriorsCompanionModel m, Bitmap backdrop, Bitmap emblem, Bitmap minimap,
        int w, int h, long nowMs) {
        c.drawColor(0xFF060709);
        if (w <= 0 || h <= 0) return;
        float sc = Math.min(w / LOGIC_W, h / LOGIC_H);
        float ox = (w - LOGIC_W * sc) * 0.5f + m.orbitDx();
        float oy = (h - LOGIC_H * sc) * 0.5f + m.orbitDy();
        c.save();
        c.translate(ox, oy);
        c.scale(sc, sc, 0f, 0f);
        drawFrame(c, m, backdrop, emblem, minimap, nowMs);
        if (m.dimmed()) drawDimVeil(c, m);
        c.restore();
    }

    private void drawFrame(
        Canvas c, WarriorsCompanionModel m, Bitmap backdrop, Bitmap emblem, Bitmap minimap, long nowMs) {
        p.setStyle(Paint.Style.FILL);
        p.setColor(m.bgColor());
        c.drawRect(0f, 0f, LOGIC_W, LOGIC_H, p);
        if (backdrop != null && !backdrop.isRecycled()) {
            dstRect.set(0, 0, (int) LOGIC_W, (int) LOGIC_H);
            imgPaint.setAlpha(155);
            c.drawBitmap(backdrop, null, dstRect, imgPaint);
            imgPaint.setAlpha(255);
        }
        drawHeader(c, m);
        int tab = m.activeTab();
        if (tab == WarriorsCompanionModel.TAB_TACTICAL) drawMapTab(c, m, minimap, nowMs);
        else if (tab == WarriorsCompanionModel.TAB_COMMAND) drawCommandTab(c, m);
        else if (tab == WarriorsCompanionModel.TAB_GRIMOIRE) drawGrimoireTab(c, m);
        else drawStatusTab(c, m, emblem);
    }

    private void drawHeader(Canvas c, WarriorsCompanionModel m) {
        p.setStyle(Paint.Style.FILL);
        p.setColor((m.cardColor() & 0x00FFFFFF) | 0xE6000000);
        c.drawRect(0f, 0f, LOGIC_W, 92f, p);
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(2f);
        p.setColor(m.borderColor());
        c.drawLine(0f, 92f, LOGIC_W, 92f, p);

        for (int i = 0; i < WarriorsCompanionModel.TAB_COUNT; i++) {
            float l = 24f + i * 302f;
            boolean sel = m.activeTab() == i;
            drawButton(c, l, 14f, l + 286f, 78f, m, sel, sel ? m.primaryColor() : m.borderColor(),
                    WarriorsCompanionModel.tabName(i), null, 22f);
        }
    }

    private void drawMapTab(Canvas c, WarriorsCompanionModel m, Bitmap minimap, long nowMs) {
        drawLiveMinimapCard(c, m, minimap);
        drawMapControlsCard(c, m);
        drawTimerAndKeepCard(c, m, nowMs);
    }

    private void drawLiveMinimapCard(Canvas c, WarriorsCompanionModel m, Bitmap minimap) {
        drawCard(c, 24f, 108f, 756f, 1048f, m);
        float cx = 390f, cy = 578f, rad = 334f;
        p.setStyle(Paint.Style.FILL);
        p.setColor(0xFF0A0D13);
        c.drawCircle(cx, cy, rad, p);

        if (minimap != null && !minimap.isRecycled()) {
            c.save();
            clipPath.reset();
            clipPath.addCircle(cx, cy, rad - 4f, Path.Direction.CW);
            c.clipPath(clipPath);
            dstRect.set((int) (cx - rad), (int) (cy - rad), (int) (cx + rad), (int) (cy + rad));
            c.drawBitmap(minimap, null, dstRect, imgPaint);
            c.restore();
        } else {
            drawText(c, "LIVE GAME MAP", cx, cy - 10f, 26f, m.primaryColor(), true, Paint.Align.CENTER);
            drawText(c, m.inGame ? "Waiting for minimap RT..." : "Enter gameplay to stream minimap RT",
                    cx, cy + 28f, 18f, 0xFF94A3B8, false, Paint.Align.CENTER);
        }

        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(3.5f);
        p.setColor(m.primaryColor());
        c.drawCircle(cx, cy, rad, p);
        drawText(c, m.minimapLabel(), 50f, 148f, 17f, 0xFF94A3B8, true, Paint.Align.LEFT);
        drawText(c, m.hasLiveMinimap ? "LIVE" : "STANDBY", 730f, 148f, 17f,
                m.hasLiveMinimap ? 0xFF34D399 : 0xFF94A3B8, true, Paint.Align.RIGHT);
    }

    private void drawMapControlsCard(Canvas c, WarriorsCompanionModel m) {
        drawCard(c, 774f, 108f, 1216f, 520f, m);
        drawText(c, "MAP CONTROLS", 796f, 146f, 19f, m.primaryColor(), true, Paint.Align.LEFT);
        drawButton(c, 796f, 170f, 1194f, 250f, m, true, m.primaryColor(),
                "CYCLE RT", m.rtCandLabel(), 20f);
        drawButton(c, 796f, 268f, 1194f, 348f, m, false, m.secondaryColor(),
                "FULL MAP", "MINUS (-)", 20f);
        drawButton(c, 796f, 366f, 988f, 496f, m, false, m.primaryColor(),
                "STASIS", "R + X", 19f);
        drawButton(c, 1002f, 366f, 1194f, 496f, m, false, m.secondaryColor(),
                "SPECIAL", "A", 19f);
    }

    private void drawTimerAndKeepCard(Canvas c, WarriorsCompanionModel m, long nowMs) {
        drawCard(c, 774f, 538f, 1216f, 1048f, m);
        drawText(c, "BATTLE TIMER", 796f, 576f, 18f, m.primaryColor(), true, Paint.Align.LEFT);
        drawText(c, m.formatTimer(nowMs), 995f, 646f, 42f, 0xFFF8FAFC, true, Paint.Align.CENTER);
        drawButton(c, 796f, 676f, 988f, 750f, m, m.timerRunning(), m.primaryColor(),
                m.timerRunning() ? "STOP" : "START", null, 20f);
        drawButton(c, 1002f, 676f, 1194f, 750f, m, false, m.borderColor(), "RESET", null, 20f);

        drawText(c, "KEEPS / KOs", 796f, 804f, 18f, m.secondaryColor(), true, Paint.Align.LEFT);
        drawText(c, String.valueOf(m.keepCount), 1194f, 808f, 32f, 0xFFF8FAFC, true, Paint.Align.RIGHT);
        drawButton(c, 796f, 838f, 888f, 924f, m, false, m.borderColor(), "-1", null, 22f);
        drawButton(c, 898f, 838f, 990f, 924f, m, false, m.primaryColor(), "+1", null, 22f);
        drawButton(c, 1000f, 838f, 1092f, 924f, m, false, m.secondaryColor(), "+10", null, 22f);
        drawButton(c, 1102f, 838f, 1194f, 924f, m, false, m.borderColor(), "RST", null, 19f);
        drawButton(c, 796f, 944f, 1194f, 1024f, m, false, m.borderColor(),
                "RESCAN GAME ART", "SRC: " + m.assetSource, 18f);
    }

    private void drawCommandTab(Canvas c, WarriorsCompanionModel m) {
        drawDeckCard(c, m, 24f, 610f, true);
        drawDeckCard(c, m, 630f, 1216f, false);
        drawPartyRow(c, m);
        drawCombatRow(c, m);
    }

    private void drawDeckCard(Canvas c, WarriorsCompanionModel m, float l, float r, boolean rDeck) {
        drawCard(c, l, 108f, r, 620f, m);
        int accent = rDeck ? m.primaryColor() : m.secondaryColor();
        String sh = rDeck ? "R" : "L";
        drawText(c, rDeck ? "R DECK" : "L DECK", l + 22f, 146f, 20f, accent, true, Paint.Align.LEFT);
        float bw = (r - l - 56f) * 0.5f, bh = 208f;
        for (int i = 0; i < 4; i++) {
            float bl = l + 20f + (i % 2) * (bw + 16f);
            float bt = 172f + (i / 2) * (bh + 18f);
            int act = (rDeck ? WarriorsCompanionModel.ACT_R_BASE : WarriorsCompanionModel.ACT_L_BASE) + i;
            boolean firing = m.seqActive && m.seqTag == act;
            String title = rDeck ? WarriorsCompanionModel.rDeckTitle(m.gameKind, i)
                    : WarriorsCompanionModel.lDeckTitle(m.gameKind, i);
            drawButton(c, bl, bt, bl + bw, bt + bh, m, firing, accent,
                    title, WarriorsCompanionModel.deckChord(sh, i), 24f);
        }
    }

    private void drawPartyRow(Canvas c, WarriorsCompanionModel m) {
        drawCard(c, 24f, 640f, 1216f, 830f, m);
        drawText(c, "PARTY SWAP", 46f, 678f, 19f, m.primaryColor(), true, Paint.Align.LEFT);
        float bw = 274f;
        for (int i = 0; i < 4; i++) {
            float bl = 46f + i * (bw + 16f);
            boolean active = m.activeParty == i;
            drawButton(c, bl, 700f, bl + bw, 808f, m, active, m.primaryColor(),
                    WarriorsCompanionModel.partyDirLabel(i), "SLOT " + (i + 1), 22f);
        }
    }

    private void drawCombatRow(Canvas c, WarriorsCompanionModel m) {
        drawCard(c, 24f, 848f, 1216f, 1048f, m);
        drawButton(c, 46f, 880f, 324f, 1018f, m, m.seqActive && m.seqTag == WarriorsCompanionModel.ACT_LOCKON,
                m.primaryColor(), "LOCK-ON", "R3", 22f);
        drawButton(c, 340f, 880f, 618f, 1018f, m, m.seqActive && m.seqTag == WarriorsCompanionModel.ACT_SPECIAL,
                m.secondaryColor(), "SPECIAL", "A", 22f);
        drawButton(c, 634f, 880f, 912f, 1018f, m, m.seqActive && m.seqTag == WarriorsCompanionModel.ACT_DODGE,
                m.primaryColor(), "DODGE", "B", 22f);
        drawButton(c, 928f, 880f, 1194f, 1018f, m, m.seqActive, 0xFFEF4444,
                "ABORT", m.seqActive ? "ACTIVE" : "IDLE", 22f);
    }

    private void drawGrimoireTab(Canvas c, WarriorsCompanionModel m) {
        drawCard(c, 24f, 108f, 1216f, 190f, m);
        drawText(c, "COMBOS", 46f, 158f, 22f, m.primaryColor(), true, Paint.Align.LEFT);
        drawButton(c, 680f, 120f, 990f, 178f, m, m.macroMode, m.primaryColor(),
                m.macroMode ? "TAP TO FIRE: ON" : "TAP TO FIRE: OFF", null, 18f);
        drawButton(c, 1006f, 120f, 1198f, 178f, m, m.seqActive, 0xFFEF4444, "ABORT", null, 18f);

        for (int i = 0; i < 6; i++) {
            float bl = 24f + (i % 3) * 402f, bt = 208f + (i / 3) * 422f;
            int act = WarriorsCompanionModel.ACT_COMBO_BASE + i;
            boolean firing = m.seqActive && m.seqTag == act;
            drawCard(c, bl, bt, bl + 388f, bt + 400f, m);
            drawText(c, WarriorsCompanionModel.comboTitle(i), bl + 24f, bt + 56f, 34f, m.primaryColor(), true, Paint.Align.LEFT);
            drawText(c, WarriorsCompanionModel.comboRole(i), bl + 364f, bt + 50f, 20f, m.secondaryColor(), true, Paint.Align.RIGHT);
            drawComboSequencePills(c, bl + 24f, bt + 140f, i, m);
            drawButton(c, bl + 24f, bt + 298f, bl + 364f, bt + 374f, m, firing,
                    m.macroMode ? m.primaryColor() : m.borderColor(),
                    firing ? "FIRING..." : (m.macroMode ? "FIRE " + WarriorsCompanionModel.comboTitle(i) : "REF ONLY"), null, 19f);
        }
    }

    private void drawComboSequencePills(Canvas c, float x, float y, int idx, WarriorsCompanionModel m) {
        int ys = idx;
        float cx = x;
        for (int k = 0; k < ys; k++) {
            drawMiniPill(c, cx, y, 48f, 44f, m.primaryColor(), "Y");
            cx += 56f;
        }
        drawMiniPill(c, cx, y, 52f, 44f, m.secondaryColor(), "X");
    }

    private void drawMiniPill(Canvas c, float x, float y, float w, float h, int col, String txt) {
        rf.set(x, y, x + w, y + h);
        p.setStyle(Paint.Style.FILL);
        p.setColor((col & 0x00FFFFFF) | 0x33000000);
        c.drawRoundRect(rf, 10f, 10f, p);
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(2f);
        p.setColor(col);
        c.drawRoundRect(rf, 10f, 10f, p);
        drawText(c, txt, x + w * 0.5f, y + h * 0.68f, 20f, 0xFFFFFFFF, true, Paint.Align.CENTER);
    }

    private void drawStatusTab(Canvas c, WarriorsCompanionModel m, Bitmap emblem) {
        drawPerfAndDevCard(c, m, emblem);
        drawControlsAndHardwareCard(c, m);
    }

    private void drawPerfAndDevCard(Canvas c, WarriorsCompanionModel m, Bitmap emblem) {
        drawCard(c, 24f, 108f, 756f, 1048f, m);
        drawText(c, "DEV TELEMETRY", 46f, 146f, 20f, m.primaryColor(), true, Paint.Align.LEFT);
        if (emblem != null && !emblem.isRecycled()) {
            dstRect.set(652, 122, 736, 206);
            c.drawBitmap(emblem, null, dstRect, imgPaint);
        }
        String fpsStr = m.fps > 0.5f ? String.format(Locale.ROOT, "%.1f", m.fps) : "\u2014";
        float low1 = m.fpsOnePercentLow();
        String lowStr = low1 > 0.5f ? String.format(Locale.ROOT, "%.1f", low1) : "\u2014";
        String msStr = m.frameMs > 0.1f ? String.format(Locale.ROOT, "%.1f ms", m.frameMs) : "\u2014";

        drawText(c, fpsStr + " FPS", 46f, 214f, 44f, 0xFFF8FAFC, true, Paint.Align.LEFT);
        drawText(c, "1% LOW " + lowStr + "   \u00b7   " + msStr, 46f, 248f, 18f, m.secondaryColor(), true, Paint.Align.LEFT);

        drawFpsGraph(c, m, 46f, 272f, 688f, 224f);

        // Migrated Display 0 corner dev info
        drawText(c, "CORNER HUD READOUT", 46f, 542f, 18f, m.primaryColor(), true, Paint.Align.LEFT);
        float y = 582f;
        if (m.devBuildTag != null && !m.devBuildTag.isEmpty()) {
            drawText(c, m.devBuildTag, 46f, y, 18f, m.secondaryColor(), true, Paint.Align.LEFT);
            y += 30f;
        }
        if (m.devOverlayText != null && !m.devOverlayText.isEmpty()) {
            String[] lines = m.devOverlayText.split("\n");
            int lim = Math.min(lines.length, 7);
            for (int i = 0; i < lim; i++) {
                drawText(c, lines[i].trim(), 46f, y, 18f, 0xFFE2E8F0, false, Paint.Align.LEFT);
                y += 28f;
            }
        } else {
            drawText(c, "Enable Dev HUD -> Status to stream corner stats here", 46f, y, 17f, 0xFF94A3B8, false, Paint.Align.LEFT);
            y += 30f;
        }

        // Engine + Save footer rows
        String st = m.moviePlaying ? "VIDEO" : m.hostPaused ? "PAUSED" : m.writeSafe ? "READY" : "WAIT";
        drawKvRow(c, 46f, 734f, 870f, "STATE / FRAMES", st + "  \u00b7  " + m.presents, 0xFFF1F5F9);
        String last = m.saveBusy ? "Saving" : (m.lastSaveAgeS < 0 ? "\u2014" : (m.lastSaveAgeS + "s ago"));
        drawKvRow(c, 46f, 734f, 922f, "SAVES (" + m.saveFileName() + ")", m.saveCommits + "  \u00b7  " + last, m.secondaryColor());
        String tid = m.gameKind == WarriorsCompanionModel.GAME_AOC ? "01002B00111A2000" : "04003F40232DE000";
        drawKvRow(c, 46f, 734f, 974f, "TITLE / INPUT", tid + "  \u00b7  " + (m.inputActive ? "ACTIVE" : "IDLE"), 0xFF94A3B8);
    }

    private void drawFpsGraph(Canvas c, WarriorsCompanionModel m, float gx, float gy, float gw, float gh) {
        p.setStyle(Paint.Style.FILL);
        p.setColor(0xCC0A0D13);
        c.drawRect(gx, gy, gx + gw, gy + gh, p);
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(1.5f);
        p.setColor(0xFF1E293B);
        c.drawRect(gx, gy, gx + gw, gy + gh, p);
        c.drawLine(gx, gy + gh * 0.5f, gx + gw, gy + gh * 0.5f, p);
        drawText(c, "60", gx + 8f, gy + 20f, 15f, 0xFF64748B, false, Paint.Align.LEFT);
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

    private void drawControlsAndHardwareCard(Canvas c, WarriorsCompanionModel m) {
        drawCard(c, 774f, 108f, 1216f, 1048f, m);
        drawText(c, "SECOND SCREEN & LEDs", 796f, 146f, 19f, m.primaryColor(), true, Paint.Align.LEFT);

        drawButton(c, 796f, 180f, 1194f, 290f, m, m.devOnStatus, m.primaryColor(),
                "DEV HUD -> STATUS", m.devOnStatus ? "CORNERS HIDDEN ON TOP" : "SHOWN IN TOP CORNERS", 20f);

        drawButton(c, 796f, 314f, 1194f, 424f, m, m.stickSync, m.secondaryColor(),
                "STICK LED = SCREEN", m.stickSyncLabel() + "  \u00b7  " + m.bifrostLookDesc(), 20f);

        drawButton(c, 796f, 448f, 1194f, 558f, m, m.autoDim, m.primaryColor(),
                "CUTSCENE AUTO-DIM", m.autoDim ? "ON" : "OFF", 20f);

        drawText(c, "TARGET GAME ART", 796f, 616f, 18f, m.secondaryColor(), true, Paint.Align.LEFT);
        drawKvRow(c, 796f, 1194f, 654f, "BACKDROP", m.hasBackdrop ? m.assetSource : "NONE",
                m.hasBackdrop ? 0xFF34D399 : 0xFF94A3B8);
        drawKvRow(c, 796f, 1194f, 692f, "EMBLEM", m.hasEmblem ? "HARVESTED" : "NONE",
                m.hasEmblem ? 0xFF34D399 : 0xFF94A3B8);

        drawButton(c, 796f, 728f, 1194f, 838f, m, false, m.primaryColor(),
                "RESCAN GAME ART", "PACK & MOVIES", 20f);
        drawButton(c, 796f, 862f, 1194f, 960f, m, false, m.borderColor(),
                "CLEAR HARVESTED ART", "RETURN TO PROCEDURAL", 19f);
    }

    private void drawDimVeil(Canvas c, WarriorsCompanionModel m) {
        p.setStyle(Paint.Style.FILL);
        p.setColor(0xC805070B);
        c.drawRect(0f, 0f, LOGIC_W, LOGIC_H, p);
        drawText(c, "CUTSCENE", LOGIC_W * 0.5f, LOGIC_H * 0.5f, 32f, m.secondaryColor(), true, Paint.Align.CENTER);
    }

    private void drawCard(Canvas c, float l, float t, float r, float b, WarriorsCompanionModel m) {
        rf.set(l, t, r, b);
        p.setStyle(Paint.Style.FILL);
        p.setColor((m.cardColor() & 0x00FFFFFF) | 0xDD000000);
        c.drawRoundRect(rf, 16f, 16f, p);
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(2f);
        p.setColor(m.borderColor());
        c.drawRoundRect(rf, 16f, 16f, p);
    }

    private void drawButton(
        Canvas c, float l, float t, float r, float b, WarriorsCompanionModel m,
        boolean lit, int accent, String title, String sub, float titleSp) {
        rf.set(l, t, r, b);
        p.setStyle(Paint.Style.FILL);
        p.setColor(lit ? ((accent & 0x00FFFFFF) | 0x3D000000) : 0xD80E1117);
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
            drawText(c, sub, cx, cy + 24f, Math.max(15f, titleSp - 6f), accent, false, Paint.Align.CENTER);
        }
    }

    private void drawKvRow(Canvas c, float x, float rx, float y, String k, String v, int vc) {
        drawText(c, k, x, y, 17f, 0xFF94A3B8, true, Paint.Align.LEFT);
        drawText(c, v, rx, y, 18f, vc, true, Paint.Align.RIGHT);
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
