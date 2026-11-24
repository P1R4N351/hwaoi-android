// SPDX-License-Identifier: MIT
package dev.nxnative.android;

import android.graphics.Bitmap;
import android.graphics.BitmapShader;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.Typeface;
import java.util.Locale;

/**
 * Purah Pad themed borderless Canvas renderer for {@link WarriorsCompanionView}.
 * Pure #000000 OLED background unless filled by harvested game assets, live circular minimap
 * and right-middle objective banner on Display 4, tactical decks, combo cards, and dev telemetry.
 */
final class WarriorsCompanionDraw {
    static final float LOGIC_W = 1240f, LOGIC_H = 1080f;
    private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint imgPaint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final Paint texPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF rf = new RectF();
    private final Rect dstRect = new Rect();
    private final Path clipPath = new Path();
    private Bitmap lastPurahTex;
    private BitmapShader purahShader;

    void drawAll(
        Canvas c, WarriorsCompanionModel m, Bitmap backdrop, Bitmap emblem,
        Bitmap minimap, Bitmap objective, Bitmap purahTex, int w, int h, long nowMs) {
        c.drawColor(0xFF000000);
        if (w <= 0 || h <= 0) return;
        syncPurahShader(purahTex);
        float sc = Math.min(w / LOGIC_W, h / LOGIC_H);
        float ox = (w - LOGIC_W * sc) * 0.5f + m.orbitDx();
        float oy = (h - LOGIC_H * sc) * 0.5f + m.orbitDy();
        c.save();
        c.translate(ox, oy);
        c.scale(sc, sc, 0f, 0f);
        drawFrame(c, m, backdrop, emblem, minimap, objective);
        if (m.dimmed()) drawDimVeil(c, m);
        c.restore();
    }

    private void syncPurahShader(Bitmap tex) {
        if (tex == lastPurahTex) return;
        lastPurahTex = tex;
        purahShader = (tex != null && !tex.isRecycled())
                ? new BitmapShader(tex, Shader.TileMode.REPEAT, Shader.TileMode.REPEAT) : null;
    }

    private void drawFrame(
        Canvas c, WarriorsCompanionModel m, Bitmap backdrop, Bitmap emblem,
        Bitmap minimap, Bitmap objective) {
        p.setStyle(Paint.Style.FILL);
        p.setColor(0xFF000000);
        c.drawRect(0f, 0f, LOGIC_W, LOGIC_H, p);
        if (backdrop != null && !backdrop.isRecycled()) {
            dstRect.set(0, 0, (int) LOGIC_W, (int) LOGIC_H);
            imgPaint.setAlpha(185);
            c.drawBitmap(backdrop, null, dstRect, imgPaint);
            imgPaint.setAlpha(255);
        }
        drawHeader(c, m);
        int tab = m.activeTab();
        if (tab == WarriorsCompanionModel.TAB_TACTICAL) drawMapTab(c, m, emblem, minimap, objective);
        else if (tab == WarriorsCompanionModel.TAB_COMMAND) drawCommandTab(c, m);
        else if (tab == WarriorsCompanionModel.TAB_GRIMOIRE) drawGrimoireTab(c, m);
        else drawStatusTab(c, m, emblem);
    }

    private void drawHeader(Canvas c, WarriorsCompanionModel m) {
        p.setStyle(Paint.Style.FILL);
        p.setColor(m.hasBackdrop ? 0xCC06090D : 0xFF000000);
        c.drawRect(0f, 0f, LOGIC_W, 92f, p);
        drawPurahOverlay(c, 0f, 0f, LOGIC_W, 92f, 22);
        for (int i = 0; i < WarriorsCompanionModel.TAB_COUNT; i++) {
            float l = 24f + i * 302f;
            boolean sel = m.activeTab() == i;
            drawButton(c, l, 14f, l + 286f, 78f, m, sel, sel ? m.primaryColor() : m.secondaryColor(),
                    WarriorsCompanionModel.tabName(i), null, 22f);
            if (sel) {
                p.setStyle(Paint.Style.FILL);
                p.setColor(m.secondaryColor());
                c.drawRect(l + 24f, 74f, l + 262f, 78f, p);
            }
        }
    }

    private void drawMapTab(
        Canvas c, WarriorsCompanionModel m, Bitmap emblem, Bitmap minimap, Bitmap objective) {
        drawLiveMinimapZone(c, m, emblem, minimap);
        drawObjectiveZone(c, m, objective);
        drawMapControlsZone(c, m);
    }

    private void drawLiveMinimapZone(Canvas c, WarriorsCompanionModel m, Bitmap emblem, Bitmap minimap) {
        drawZoneSurface(c, 24f, 108f, 748f, 1048f, m, true);
        float cx = 386f, cy = 584f, rad = 330f;
        p.setStyle(Paint.Style.FILL);
        p.setColor(0xFF000000);
        c.drawCircle(cx, cy, rad, p);

        if (minimap != null && !minimap.isRecycled()) {
            c.save();
            clipPath.reset();
            clipPath.addCircle(cx, cy, rad - 2f, Path.Direction.CW);
            c.clipPath(clipPath);
            dstRect.set((int) (cx - rad), (int) (cy - rad), (int) (cx + rad), (int) (cy + rad));
            c.drawBitmap(minimap, null, dstRect, imgPaint);
            c.restore();
        } else {
            drawText(c, "MINIMAP FEED", cx, cy - 10f, 26f, m.primaryColor(), true, Paint.Align.CENTER);
            drawText(c, m.inGame ? "Waiting for minimap RT..." : "Enter gameplay to stream minimap",
                    cx, cy + 28f, 18f, 0xFF7FA39E, false, Paint.Align.CENTER);
        }

        float tx = 44f;
        if (emblem != null && !emblem.isRecycled()) {
            dstRect.set(40, 118, 82, 160);
            c.drawBitmap(emblem, null, dstRect, imgPaint);
            tx = 94f;
        }
        drawText(c, m.minimapLabel(), tx, 148f, 17f, m.secondaryColor(), true, Paint.Align.LEFT);
        drawText(c, m.hasLiveMinimap ? "LIVE" : "STANDBY", 726f, 148f, 17f,
                m.hasLiveMinimap ? m.primaryColor() : 0xFF6B7F7D, true, Paint.Align.RIGHT);
    }

    private void drawObjectiveZone(Canvas c, WarriorsCompanionModel m, Bitmap objective) {
        drawZoneSurface(c, 768f, 116f, 1216f, 336f, m, true);
        drawCornerTicks(c, 768f, 116f, 1216f, 336f, m.secondaryColor(), 10f);
        drawText(c, "OBJECTIVE", 784f, 146f, 18f, m.secondaryColor(), true, Paint.Align.LEFT);
        drawText(c, m.objectiveLabel(), 1200f, 146f, 16f, 0xFF6E8E88, true, Paint.Align.RIGHT);

        p.setStyle(Paint.Style.FILL);
        p.setColor(0xFF000000);
        c.drawRect(776f, 162f, 1208f, 324f, p);

        if (objective != null && !objective.isRecycled() && m.hasLiveObjective) {
            float bx = 776f, by = 162f, bw = 432f, bh = 162f;
            float sc = Math.min(bw / Math.max(1, objective.getWidth()), bh / Math.max(1, objective.getHeight()));
            int dw = Math.round(objective.getWidth() * sc), dh = Math.round(objective.getHeight() * sc);
            int dl = Math.round(bx + (bw - dw) * 0.5f), dt = Math.round(by + (bh - dh) * 0.5f);
            dstRect.set(dl, dt, dl + dw, dt + dh);
            c.drawBitmap(objective, null, dstRect, imgPaint);
        } else {
            drawText(c, m.inGame ? "No active objective" : "Enter gameplay for objective HUD",
                    992f, 248f, 18f, 0xFF6B7F7D, false, Paint.Align.CENTER);
        }
    }

    private void drawMapControlsZone(Canvas c, WarriorsCompanionModel m) {
        drawZoneSurface(c, 768f, 356f, 1216f, 848f, m, false);
        drawText(c, "PURAH PAD LINK", 784f, 386f, 18f, m.secondaryColor(), true, Paint.Align.LEFT);
        drawText(c, "SRC: " + m.assetSource, 1200f, 386f, 16f,
                m.hasBackdrop || m.hasPurahTex ? m.primaryColor() : 0xFF6B7F7D, true, Paint.Align.RIGHT);

        drawButton(c, 784f, 404f, 1200f, 496f, m, false, m.secondaryColor(),
                "FULL MAP", "MINUS (-)", 20f);
        drawButton(c, 784f, 512f, 1200f, 604f, m, true, m.primaryColor(),
                "CYCLE RT", m.rtCandLabel(), 20f);
        drawButton(c, 784f, 620f, 984f, 716f, m, false, m.primaryColor(),
                "STASIS", "R + X", 19f);
        drawButton(c, 1000f, 620f, 1200f, 716f, m, false, m.secondaryColor(),
                "SPECIAL", "A", 19f);
        drawButton(c, 784f, 740f, 1200f, 828f, m, false, m.secondaryColor(),
                "RESCAN GAME ART", "NUP / PACK / GPU TEXTURES", 18f);
    }

    private void drawCommandTab(Canvas c, WarriorsCompanionModel m) {
        drawDeckCard(c, m, 24f, 610f, true);
        drawDeckCard(c, m, 630f, 1216f, false);
        drawPartyRow(c, m);
        drawCombatRow(c, m);
    }

    private void drawDeckCard(Canvas c, WarriorsCompanionModel m, float l, float r, boolean rDeck) {
        drawZoneSurface(c, l, 108f, r, 646f, m, false);
        int accent = rDeck ? m.primaryColor() : m.secondaryColor();
        drawText(c, rDeck ? "RIGHT DECK (R + FACE)" : "LEFT DECK (L + FACE)", l + 22f, 146f, 20f, accent, true, Paint.Align.LEFT);
        float bw = (r - l - 56f) * 0.5f, bh = 208f;
        for (int i = 0; i < 4; i++) {
            float bl = l + 20f + (i % 2) * (bw + 16f);
            float bt = 172f + (i / 2) * (bh + 18f);
            int act = (rDeck ? WarriorsCompanionModel.ACT_R_BASE : WarriorsCompanionModel.ACT_L_BASE) + i;
            boolean firing = m.seqActive && m.seqTag == act;
            String title = rDeck ? WarriorsCompanionModel.rDeckTitle(m.gameKind, i) : WarriorsCompanionModel.lDeckTitle(m.gameKind, i);
            drawButton(c, bl, bt, bl + bw, bt + bh, m, firing, accent,
                    title, WarriorsCompanionModel.deckChord(rDeck ? "R" : "L", i), 24f);
        }
    }

    private void drawPartyRow(Canvas c, WarriorsCompanionModel m) {
        drawZoneSurface(c, 24f, 666f, 1216f, 830f, m, false);
        drawText(c, "PARTY SWAP (D-PAD)", 46f, 696f, 18f, m.secondaryColor(), true, Paint.Align.LEFT);
        for (int i = 0; i < 4; i++) {
            float bl = 46f + i * 290f;
            boolean active = (m.activeParty == i);
            boolean firing = m.seqActive && m.seqTag == (WarriorsCompanionModel.ACT_PARTY_BASE + i);
            drawButton(c, bl, 700f, bl + 274f, 808f, m, active || firing,
                    active ? m.primaryColor() : m.secondaryColor(),
                    WarriorsCompanionModel.partyDirLabel(i), active ? "ACTIVE" : "SWAP", 22f);
        }
    }

    private void drawCombatRow(Canvas c, WarriorsCompanionModel m) {
        drawZoneSurface(c, 24f, 848f, 1216f, 1048f, m, false);
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
        drawZoneSurface(c, 24f, 108f, 1216f, 190f, m, false);
        drawText(c, "COMBOS", 46f, 158f, 22f, m.primaryColor(), true, Paint.Align.LEFT);
        drawButton(c, 680f, 120f, 990f, 178f, m, m.macroMode, m.primaryColor(),
                m.macroMode ? "TAP TO FIRE: ON" : "TAP TO FIRE: OFF", null, 18f);
        drawButton(c, 1006f, 120f, 1198f, 178f, m, m.seqActive, 0xFFEF4444, "ABORT", null, 18f);

        for (int i = 0; i < 6; i++) {
            float bl = 24f + (i % 3) * 402f, bt = 208f + (i / 3) * 422f;
            int act = WarriorsCompanionModel.ACT_COMBO_BASE + i;
            boolean firing = m.seqActive && m.seqTag == act;
            drawZoneSurface(c, bl, bt, bl + 388f, bt + 400f, m, false);
            drawText(c, WarriorsCompanionModel.comboTitle(i), bl + 24f, bt + 56f, 34f, m.primaryColor(), true, Paint.Align.LEFT);
            drawText(c, WarriorsCompanionModel.comboRole(i), bl + 364f, bt + 50f, 20f, m.secondaryColor(), true, Paint.Align.RIGHT);
            drawComboSequencePills(c, bl + 24f, bt + 140f, i, m);
            drawButton(c, bl + 24f, bt + 298f, bl + 364f, bt + 374f, m, firing,
                    m.macroMode ? m.primaryColor() : m.secondaryColor(),
                    firing ? "FIRING..." : (m.macroMode ? "FIRE " + WarriorsCompanionModel.comboTitle(i) : "REF ONLY"), null, 19f);
        }
    }

    private void drawComboSequencePills(Canvas c, float x, float y, int idx, WarriorsCompanionModel m) {
        float cx = x;
        for (int k = 0; k < idx; k++) {
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
        drawText(c, txt, x + w * 0.5f, y + h * 0.68f, 20f, 0xFFF4EFE0, true, Paint.Align.CENTER);
    }

    private void drawStatusTab(Canvas c, WarriorsCompanionModel m, Bitmap emblem) {
        drawPerfAndDevCard(c, m, emblem);
        drawControlsAndHardwareCard(c, m);
    }

    private void drawPerfAndDevCard(Canvas c, WarriorsCompanionModel m, Bitmap emblem) {
        drawZoneSurface(c, 24f, 108f, 756f, 1048f, m, false);
        drawText(c, "DEV TELEMETRY", 46f, 146f, 20f, m.primaryColor(), true, Paint.Align.LEFT);
        if (emblem != null && !emblem.isRecycled()) {
            dstRect.set(652, 122, 736, 206);
            c.drawBitmap(emblem, null, dstRect, imgPaint);
        }
        String fpsStr = m.fps > 0.5f ? String.format(Locale.ROOT, "%.1f", m.fps) : "\u2014";
        float low1 = m.fpsOnePercentLow();
        String lowStr = low1 > 0.5f ? String.format(Locale.ROOT, "%.1f", low1) : "\u2014";
        String msStr = m.frameMs > 0.1f ? String.format(Locale.ROOT, "%.1f ms", m.frameMs) : "\u2014";

        drawText(c, fpsStr + " FPS", 46f, 214f, 44f, 0xFFE9E5D5, true, Paint.Align.LEFT);
        drawText(c, "1% LOW " + lowStr + "   \u00b7   " + msStr, 46f, 248f, 18f, m.secondaryColor(), true, Paint.Align.LEFT);
        drawFpsGraph(c, m, 46f, 272f, 688f, 224f);

        drawText(c, "CORNER HUD READOUT", 46f, 542f, 18f, m.primaryColor(), true, Paint.Align.LEFT);
        float y = 582f;
        if (m.devBuildTag != null && !m.devBuildTag.isEmpty()) {
            drawText(c, m.devBuildTag, 46f, y, 18f, m.secondaryColor(), true, Paint.Align.LEFT);
            y += 30f;
        }
        if (m.devOverlayText != null && !m.devOverlayText.isEmpty()) {
            String[] lines = m.devOverlayText.split("\n");
            for (int i = 0; i < Math.min(lines.length, 7); i++) {
                drawText(c, lines[i].trim(), 46f, y, 18f, 0xFFE9E5D5, false, Paint.Align.LEFT);
                y += 28f;
            }
        } else {
            drawText(c, "Enable Dev HUD -> Status to stream corner stats here", 46f, y, 17f, 0xFF7FA39E, false, Paint.Align.LEFT);
        }

        String st = m.moviePlaying ? "VIDEO" : m.hostPaused ? "PAUSED" : m.writeSafe ? "READY" : "WAIT";
        drawKvRow(c, 46f, 734f, 870f, "STATE / FRAMES", st + "  \u00b7  " + m.presents, 0xFFE9E5D5);
        String last = m.saveBusy ? "Saving" : (m.lastSaveAgeS < 0 ? "\u2014" : (m.lastSaveAgeS + "s ago"));
        drawKvRow(c, 46f, 734f, 922f, "SAVES (" + m.saveFileName() + ")", m.saveCommits + "  \u00b7  " + last, m.secondaryColor());
        String tid = m.gameKind == WarriorsCompanionModel.GAME_AOC ? "01002B00111A2000" : "04003F40232DE000";
        drawKvRow(c, 46f, 734f, 974f, "TITLE / INPUT", tid + "  \u00b7  " + (m.inputActive ? "ACTIVE" : "IDLE"), 0xFF7FA39E);
    }

    private void drawFpsGraph(Canvas c, WarriorsCompanionModel m, float gx, float gy, float gw, float gh) {
        p.setStyle(Paint.Style.FILL);
        p.setColor(0xFF05080B);
        c.drawRect(gx, gy, gx + gw, gy + gh, p);
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(1.5f);
        p.setColor(0x335CE6C4);
        c.drawLine(gx, gy + gh * 0.5f, gx + gw, gy + gh * 0.5f, p);
        drawText(c, "60", gx + 8f, gy + 20f, 15f, 0xFF6B7F7D, false, Paint.Align.LEFT);
        drawText(c, "30", gx + 8f, gy + gh * 0.5f - 6f, 15f, 0xFF6B7F7D, false, Paint.Align.LEFT);
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
        drawZoneSurface(c, 774f, 108f, 1216f, 1048f, m, false);
        drawText(c, "SECOND SCREEN & LEDs", 796f, 146f, 19f, m.primaryColor(), true, Paint.Align.LEFT);
        drawButton(c, 796f, 170f, 1194f, 264f, m, m.devOnStatus, m.primaryColor(),
                "DEV HUD -> STATUS", m.devOnStatus ? "CORNERS HIDDEN ON TOP" : "SHOWN IN TOP CORNERS", 20f);
        drawButton(c, 796f, 280f, 1194f, 374f, m, m.stickLights, m.primaryColor(),
                "STICK LIGHTS", m.stickLights ? ("ON  \u00b7  " + m.bifrostLookDesc()) : "OFF  \u00b7  LEDS CLEARED", 20f);
        drawButton(c, 796f, 390f, 1194f, 484f, m, m.stickLights && m.stickSync, m.secondaryColor(),
                "STICK LED = SCREEN", m.stickSyncLabel() + (m.stickLights ? "  \u00b7  BRIGHTNESS" : "  \u00b7  MUTED"), 20f);
        drawButton(c, 796f, 500f, 1194f, 594f, m, m.autoDim, m.primaryColor(),
                "CUTSCENE AUTO-DIM", m.autoDim ? "ON" : "OFF", 20f);

        drawText(c, "PURAH PAD GAME ART", 796f, 640f, 18f, m.secondaryColor(), true, Paint.Align.LEFT);
        drawKvRow(c, 796f, 1194f, 678f, "BACKDROP / TEX", m.hasBackdrop || m.hasPurahTex ? m.assetSource : "NONE",
                m.hasBackdrop || m.hasPurahTex ? m.primaryColor() : 0xFF7FA39E);
        drawKvRow(c, 796f, 1194f, 714f, "EMBLEM", m.hasEmblem ? "HARVESTED" : "NONE",
                m.hasEmblem ? m.primaryColor() : 0xFF7FA39E);

        drawButton(c, 796f, 746f, 1194f, 840f, m, false, m.primaryColor(),
                "RESCAN GAME ART", "NUP / PACK / GPU TEXTURES", 20f);
        drawButton(c, 796f, 856f, 1194f, 950f, m, false, m.secondaryColor(),
                "CLEAR HARVESTED ART", "PURE BLACK OLED", 19f);
    }

    private void drawDimVeil(Canvas c, WarriorsCompanionModel m) {
        p.setStyle(Paint.Style.FILL);
        p.setColor(0xC8000000);
        c.drawRect(0f, 0f, LOGIC_W, LOGIC_H, p);
        drawText(c, "CUTSCENE", LOGIC_W * 0.5f, LOGIC_H * 0.5f, 32f, m.secondaryColor(), true, Paint.Align.CENTER);
    }

    private void drawZoneSurface(
        Canvas c, float l, float t, float r, float b, WarriorsCompanionModel m, boolean pureBlack) {
        rf.set(l, t, r, b);
        p.setStyle(Paint.Style.FILL);
        boolean hasArt = m.hasBackdrop || m.hasPurahTex;
        p.setColor(pureBlack ? 0xFF000000 : (hasArt ? 0xB8080D12 : 0xFF000000));
        c.drawRoundRect(rf, 14f, 14f, p);
        if (!pureBlack) drawPurahOverlay(c, l, t, r, b, 20);
    }

    private void drawPurahOverlay(Canvas c, float l, float t, float r, float b, int alpha) {
        if (purahShader == null) return;
        rf.set(l, t, r, b);
        texPaint.setShader(purahShader);
        texPaint.setAlpha(alpha);
        c.drawRoundRect(rf, 14f, 14f, texPaint);
    }

    private void drawCornerTicks(Canvas c, float l, float t, float r, float b, int col, float len) {
        p.setStyle(Paint.Style.FILL);
        p.setColor((col & 0x00FFFFFF) | 0x88000000);
        c.drawRect(l, t, l + len, t + 2f, p);
        c.drawRect(l, t, l + 2f, t + len, p);
        c.drawRect(r - len, b - 2f, r, b, p);
        c.drawRect(r - 2f, b - len, r, b, p);
    }

    private void drawButton(
        Canvas c, float l, float t, float r, float b, WarriorsCompanionModel m,
        boolean lit, int accent, String title, String sub, float titleSp) {
        rf.set(l, t, r, b);
        p.setStyle(Paint.Style.FILL);
        boolean hasArt = m.hasBackdrop || m.hasPurahTex;
        p.setColor(lit ? ((accent & 0x00FFFFFF) | 0x3A000000) : (hasArt ? 0xC80D141B : 0xFF0B1015));
        c.drawRoundRect(rf, 12f, 12f, p);
        drawPurahOverlay(c, l, t, r, b, lit ? 38 : 22);
        p.setStyle(Paint.Style.FILL);
        p.setColor(lit ? accent : ((accent & 0x00FFFFFF) | 0x70000000));
        c.drawRect(l + 6f, t + 14f, l + 10f, b - 14f, p);

        float cx = (l + r) * 0.5f, cy = (t + b) * 0.5f;
        int titleCol = lit ? 0xFFFFFFFF : 0xFFE9E5D5;
        if (sub == null) {
            drawText(c, title, cx, cy + titleSp * 0.34f, titleSp, titleCol, true, Paint.Align.CENTER);
        } else {
            drawText(c, title, cx, cy - 4f, titleSp, titleCol, true, Paint.Align.CENTER);
            drawText(c, sub, cx, cy + 24f, Math.max(15f, titleSp - 6f), accent, false, Paint.Align.CENTER);
        }
    }

    private void drawKvRow(Canvas c, float x, float rx, float y, String k, String v, int vc) {
        drawText(c, k, x, y, 17f, 0xFF7FA39E, true, Paint.Align.LEFT);
        drawText(c, v, rx, y, 18f, vc, true, Paint.Align.RIGHT);
    }

    private void drawText(Canvas c, String s, float x, float y, float sp, int col, boolean bold, Paint.Align align) {
        if (s == null || s.isEmpty()) return;
        p.setStyle(Paint.Style.FILL);
        p.setColor(col);
        p.setTextSize(sp);
        p.setTextAlign(align);
        p.setTypeface(bold ? Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD) : Typeface.SANS_SERIF);
        c.drawText(s, x, y, p);
    }
}
