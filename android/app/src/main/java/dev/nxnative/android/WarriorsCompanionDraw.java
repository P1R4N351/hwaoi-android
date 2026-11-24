// SPDX-License-Identifier: MIT
package dev.nxnative.android;

import android.graphics.Bitmap;
import android.graphics.BitmapShader;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.PorterDuff;
import android.graphics.PorterDuffColorFilter;
import android.graphics.Rect;
import android.graphics.Shader;
import android.graphics.Typeface;
import java.util.Locale;

/**
 * Pure Purah Pad Canvas renderer for {@link WarriorsCompanionView}.
 * Zero card boxes or cell borders: renders tiled Zonai backdrop, edge fade filter,
 * glowing perimeter borders, borderless 2-tab navigation, passive live minimap + objective,
 * and unboxed telemetry + 7-row Status settings.
 */
final class WarriorsCompanionDraw {
    static final float LOGIC_W = 1240f, LOGIC_H = 1080f;
    private static final int COL_SIDE_TINT = 0x785CE6C4, COL_FRIEZE_TINT = 0xCC5CE6C4;
    private static final int COL_GLYPH_NAV = 0xFF5CE6C4, COL_GLYPH_OFF = 0xAA7FA39E, COL_GLYPH_DIM = 0xCC5CE6C4;

    private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint imgPaint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final Paint texPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint sidePaint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final Paint friezePaint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final Paint glyphNavPaint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final Paint glyphOffPaint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final Paint glyphDimPaint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final Rect dstRect = new Rect();
    private final Path clipPath = new Path();
    private final Path diamondPath = new Path();
    private Bitmap lastBackdrop;
    private BitmapShader backdropShader;

    WarriorsCompanionDraw() {
        sidePaint.setColorFilter(new PorterDuffColorFilter(COL_SIDE_TINT, PorterDuff.Mode.SRC_IN));
        friezePaint.setColorFilter(new PorterDuffColorFilter(COL_FRIEZE_TINT, PorterDuff.Mode.SRC_IN));
        glyphNavPaint.setColorFilter(new PorterDuffColorFilter(COL_GLYPH_NAV, PorterDuff.Mode.SRC_IN));
        glyphOffPaint.setColorFilter(new PorterDuffColorFilter(COL_GLYPH_OFF, PorterDuff.Mode.SRC_IN));
        glyphDimPaint.setColorFilter(new PorterDuffColorFilter(COL_GLYPH_DIM, PorterDuff.Mode.SRC_IN));
    }

    void drawAll(
        Canvas c, WarriorsCompanionModel m, Bitmap backdrop, Bitmap filter, Bitmap glyph,
        Bitmap borderFrieze, Bitmap borderEdge, Bitmap borderSide,
        Bitmap minimap, Bitmap objective, Bitmap leftHud, int w, int h, long nowMs) {
        c.drawColor(0xFF000000);
        if (w <= 0 || h <= 0) return;
        syncBackdropShader(backdrop);
        float sc = Math.min(w / LOGIC_W, h / LOGIC_H);
        float ox = (w - LOGIC_W * sc) * 0.5f + m.orbitDx();
        float oy = (h - LOGIC_H * sc) * 0.5f + m.orbitDy();
        c.save();
        c.translate(ox, oy);
        c.scale(sc, sc, 0f, 0f);
        drawChrome(c, filter, borderFrieze, borderEdge, borderSide);
        drawTopNav(c, m, glyph);
        if (m.activeTab() == WarriorsCompanionModel.TAB_TACTICAL) drawMapTab(c, m, minimap, objective, leftHud);
        else drawStatusTab(c, m);
        if (m.dimmed()) drawDimVeil(c, glyph);
        c.restore();
    }

    private void syncBackdropShader(Bitmap backdrop) {
        if (backdrop == lastBackdrop) return;
        lastBackdrop = backdrop;
        backdropShader = (backdrop != null && !backdrop.isRecycled())
                ? new BitmapShader(backdrop, Shader.TileMode.REPEAT, Shader.TileMode.REPEAT) : null;
    }

    private void drawChrome(
        Canvas c, Bitmap filter, Bitmap borderFrieze, Bitmap borderEdge, Bitmap borderSide) {
        p.setStyle(Paint.Style.FILL);
        p.setColor(0xFF000000);
        c.drawRect(0f, 0f, LOGIC_W, LOGIC_H, p);
        if (backdropShader != null) {
            texPaint.setShader(backdropShader);
            c.drawRect(0f, 0f, LOGIC_W, LOGIC_H, texPaint);
        }
        drawFilter(c, filter);
        drawBorders(c, borderFrieze, borderEdge, borderSide);
    }

    private void drawFilter(Canvas c, Bitmap filter) {
        if (filter == null || filter.isRecycled()) return;
        dstRect.set(840, 0, 1240, 1080);
        c.drawBitmap(filter, null, dstRect, imgPaint);
        c.save();
        c.scale(-1f, 1f, 200f, 0f);
        dstRect.set(0, 0, 400, 1080);
        c.drawBitmap(filter, null, dstRect, imgPaint);
        c.restore();
    }

    private void drawBorders(Canvas c, Bitmap frieze, Bitmap edge, Bitmap side) {
        if (side != null && !side.isRecycled()) {
            dstRect.set(20, 0, 116, 1080);
            c.drawBitmap(side, null, dstRect, sidePaint);
            c.save();
            c.scale(-1f, 1f, 1172f, 0f);
            dstRect.set(1124, 0, 1220, 1080);
            c.drawBitmap(side, null, dstRect, sidePaint);
            c.restore();
        }
        if (edge != null && !edge.isRecycled()) {
            c.save();
            c.scale(1f, -1f, 0f, 42f);
            dstRect.set(0, 0, 1240, 84);
            c.drawBitmap(edge, null, dstRect, imgPaint);
            c.restore();
            dstRect.set(0, 996, 1240, 1080);
            c.drawBitmap(edge, null, dstRect, imgPaint);
        }
        drawWallFrieze(c, frieze);
    }

    private void drawWallFrieze(Canvas c, Bitmap frieze) {
        if (frieze == null || frieze.isRecycled()) return;
        int span = 872, thick = 20;
        int segW = Math.max(140, Math.min(span, Math.round(frieze.getWidth() * ((float) thick / Math.max(1, frieze.getHeight())))));
        drawWallStrip(c, frieze, 8f, 976f, -90f, span, thick, segW);
        drawWallStrip(c, frieze, 1232f, 104f, 90f, span, thick, segW);
    }

    private void drawWallStrip(
        Canvas c, Bitmap frieze, float tx, float ty, float deg, int span, int thick, int segW) {
        c.save();
        c.translate(tx, ty);
        c.rotate(deg, 0f, 0f);
        c.clipRect(0, 0, span, thick);
        for (int x = 0, n = 0; x < span && n < 8; x += segW, n++) {
            dstRect.set(x, 0, x + segW, thick);
            c.drawBitmap(frieze, null, dstRect, friezePaint);
        }
        c.restore();
    }

    private void drawTopNav(Canvas c, WarriorsCompanionModel m, Bitmap glyph) {
        boolean padSel = m.activeTab() == WarriorsCompanionModel.TAB_TACTICAL;
        if (glyph != null && !glyph.isRecycled()) {
            dstRect.set(56, 32, 92, 68);
            c.drawBitmap(glyph, null, dstRect, padSel ? glyphNavPaint : glyphOffPaint);
        } else {
            drawDiamond(c, 74f, 50f, 11f, padSel ? m.primaryColor() : COL_GLYPH_OFF, true);
        }
        if (padSel) {
            p.setStyle(Paint.Style.FILL);
            p.setColor(m.primaryColor());
            c.drawRect(46f, 74f, 102f, 77f, p);
        }
        int statusCol = !padSel ? m.primaryColor() : COL_GLYPH_OFF;
        drawStatusIcon(c, 1166f, 50f, statusCol);
        if (!padSel) {
            p.setStyle(Paint.Style.FILL);
            p.setColor(m.primaryColor());
            c.drawRect(1138f, 74f, 1194f, 77f, p);
        }
    }

    private void drawStatusIcon(Canvas c, float cx, float cy, int col) {
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(2f);
        p.setColor(col);
        c.drawLine(cx - 16f, cy - 9f, cx + 16f, cy - 9f, p);
        c.drawLine(cx - 16f, cy, cx + 16f, cy, p);
        c.drawLine(cx - 16f, cy + 9f, cx + 16f, cy + 9f, p);
        drawDiamond(c, cx - 6f, cy - 9f, 3.6f, col, true);
        drawDiamond(c, cx + 7f, cy, 3.6f, col, true);
        drawDiamond(c, cx - 2f, cy + 9f, 3.6f, col, true);
    }

    private void drawMapTab(Canvas c, WarriorsCompanionModel m, Bitmap minimap, Bitmap objective, Bitmap leftHud) {
        drawLiveLeftHud(c, m, leftHud);
        drawLiveObjective(c, m, objective);
        drawLiveMinimap(c, m, minimap);
    }

    private void drawLiveLeftHud(Canvas c, WarriorsCompanionModel m, Bitmap leftHud) {
        if (leftHud == null || leftHud.isRecycled() || !m.hasLiveLeftHud) return;
        int dt = 198, dh = 852;
        int dw = Math.min(420, Math.round(leftHud.getWidth() * (852f / Math.max(1, leftHud.getHeight()))));
        dstRect.set(42, dt, 42 + dw, dt + dh);
        c.drawBitmap(leftHud, null, dstRect, imgPaint);
    }

    private void drawLiveObjective(Canvas c, WarriorsCompanionModel m, Bitmap objective) {
        if (objective == null || objective.isRecycled() || !m.hasLiveObjective) return;
        float sc = Math.min(980f / Math.max(1, objective.getWidth()), 280f / Math.max(1, objective.getHeight()));
        int dw = Math.round(objective.getWidth() * sc), dh = Math.round(objective.getHeight() * sc);
        int dl = Math.round((LOGIC_W - dw) * 0.5f), dt = Math.max(4, Math.round(144f - dh * 0.5f));
        dstRect.set(dl, dt, dl + dw, dt + dh);
        c.drawBitmap(objective, null, dstRect, imgPaint);
    }

    private void drawLiveMinimap(Canvas c, WarriorsCompanionModel m, Bitmap minimap) {
        if (minimap == null || minimap.isRecycled() || !m.hasLiveMinimap) return;
        float cx = m.hasLiveLeftHud ? 816f : 620f, cy = 624f, rad = m.hasLiveLeftHud ? 328f : 340f;
        p.setStyle(Paint.Style.FILL);
        p.setColor(0xFF000000);
        c.drawCircle(cx, cy, rad, p);
        c.save();
        clipPath.reset();
        clipPath.addCircle(cx, cy, rad - 2f, Path.Direction.CW);
        c.clipPath(clipPath);
        dstRect.set(Math.round(cx - rad), Math.round(cy - rad), Math.round(cx + rad), Math.round(cy + rad));
        c.drawBitmap(minimap, null, dstRect, imgPaint);
        c.restore();
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(1.5f);
        p.setColor(0x665CE6C4);
        c.drawCircle(cx, cy, rad, p);
    }

    private void drawStatusTab(Canvas c, WarriorsCompanionModel m) {
        drawStatusTelemetry(c, m);
        drawStatusSettings(c, m);
    }

    private void drawStatusTelemetry(Canvas c, WarriorsCompanionModel m) {
        String fpsStr = m.fps > 0.5f ? String.format(Locale.ROOT, "%.1f", m.fps) : "\u2014";
        float low1 = m.fpsOnePercentLow();
        String lowStr = low1 > 0.5f ? String.format(Locale.ROOT, "%.1f", low1) : "\u2014";
        String msStr = m.frameMs > 0.1f ? String.format(Locale.ROOT, "%.1f ms", m.frameMs) : "\u2014";
        drawText(c, fpsStr + " FPS", 76f, 176f, 42f, 0xFFE9E5D5, true, Paint.Align.LEFT);
        drawText(c, lowStr + "  \u00b7  " + msStr, 76f, 210f, 18f, m.secondaryColor(), true, Paint.Align.LEFT);
        drawFpsGraph(c, m, 76f, 236f, 560f, 190f);
        float y = 472f;
        if (m.devBuildTag != null && !m.devBuildTag.isEmpty()) {
            drawText(c, m.devBuildTag, 76f, y, 18f, m.secondaryColor(), true, Paint.Align.LEFT);
            y += 30f;
        }
        if (m.devOverlayText != null && !m.devOverlayText.isEmpty()) {
            String[] lines = m.devOverlayText.split("\n");
            for (int i = 0; i < lines.length && y <= 740f; i++) {
                drawText(c, lines[i].trim(), 76f, y, 18f, 0xFFE9E5D5, false, Paint.Align.LEFT);
                y += 28f;
            }
        }
        String saveAge = m.lastSaveAgeS >= 0 ? ("  \u00b7  " + m.lastSaveAgeS + "s") : "";
        String saveFooter = m.saveFileName() + "  \u00b7  " + m.saveCommits + saveAge;
        drawText(c, saveFooter, 76f, 950f, 17f, m.secondaryColor(), false, Paint.Align.LEFT);
    }

    private void drawFpsGraph(Canvas c, WarriorsCompanionModel m, float gx, float gy, float gw, float gh) {
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(1f);
        p.setColor(0x335CE6C4);
        c.drawLine(gx, gy, gx + gw, gy, p);
        c.drawLine(gx, gy + gh * 0.5f, gx + gw, gy + gh * 0.5f, p);
        drawText(c, "60", gx + 4f, gy + 18f, 14f, 0xFF6B7F7D, false, Paint.Align.LEFT);
        drawText(c, "30", gx + 4f, gy + gh * 0.5f - 6f, 14f, 0xFF6B7F7D, false, Paint.Align.LEFT);
        if (m.fpsHistCount < 2) return;
        p.setStyle(Paint.Style.STROKE);
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

    private void drawStatusSettings(Canvas c, WarriorsCompanionModel m) {
        drawSettingRow(c, m, 0, "Corner HUD", m.devOnStatus ? "ON" : "TOP", m.devOnStatus);
        drawSettingRow(c, m, 1, "Stick Lights", m.stickLights ? "ON" : "OFF", m.stickLights);
        drawSettingRow(c, m, 2, "Stick Brightness", m.stickLights ? m.stickSyncLabel() : "OFF",
                m.stickLights && m.stickSync);
        drawSettingRow(c, m, 3, "Cutscene Dim", m.autoDim ? "ON" : "OFF", m.autoDim);
        drawSettingRow(c, m, 4, "Minimap Layer", m.rtCandLabel(), m.minimapCandCount > 0);
        drawSettingRow(c, m, 5, "Rescan Assets", "", false);
        drawSettingRow(c, m, 6, "Clear Assets", "", false);
    }

    private void drawSettingRow(
        Canvas c, WarriorsCompanionModel m, int row, String label, String val, boolean active) {
        float y = 140f + row * 112f;
        float cy = y + 44f;
        drawDiamond(c, 752f, cy, 7f, active ? m.primaryColor() : 0x665CE6C4, active);
        drawText(c, label, 774f, cy + 7f, 21f, 0xFFE9E5D5, true, Paint.Align.LEFT);
        if (val != null && !val.isEmpty()) {
            drawText(c, val, 1160f, cy + 7f, 19f, active ? m.primaryColor() : m.secondaryColor(),
                    true, Paint.Align.RIGHT);
        }
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(1f);
        p.setColor(0x285CE6C4);
        c.drawLine(740f, y + 96f, 1164f, y + 96f, p);
    }

    private void drawDimVeil(Canvas c, Bitmap glyph) {
        p.setStyle(Paint.Style.FILL);
        p.setColor(0xDC000000);
        c.drawRect(0f, 0f, LOGIC_W, LOGIC_H, p);
        if (glyph != null && !glyph.isRecycled()) {
            dstRect.set(536, 456, 704, 624);
            c.drawBitmap(glyph, null, dstRect, glyphDimPaint);
        }
    }

    private void drawDiamond(Canvas c, float cx, float cy, float r, int col, boolean filled) {
        diamondPath.reset();
        diamondPath.moveTo(cx, cy - r);
        diamondPath.lineTo(cx + r, cy);
        diamondPath.lineTo(cx, cy + r);
        diamondPath.lineTo(cx - r, cy);
        diamondPath.close();
        p.setStyle(filled ? Paint.Style.FILL : Paint.Style.STROKE);
        p.setStrokeWidth(1.5f);
        p.setColor(col);
        c.drawPath(diamondPath, p);
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
