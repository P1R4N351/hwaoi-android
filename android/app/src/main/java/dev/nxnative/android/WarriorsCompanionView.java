// SPDX-License-Identifier: MIT
package dev.nxnative.android;

import android.content.Context;
import android.graphics.Canvas;
import android.os.SystemClock;
import android.view.MotionEvent;
import android.view.View;

/**
 * Interactive Canvas view for the converged Age of Calamity /
 * Age of Imprisonment companion across Display 4 and the single-screen drawer.
 */
final class WarriorsCompanionView extends View {
    interface Actions {
        void onTabSelected(int tab);
        void onFireAction(int actionId);
        void onAbortChord();
        void onToggleDevOnStatus();
        void onToggleStickLights();
        void onToggleStickSync();
        void onCycleRt();
        void onRescanArt();
        void onClearArt();
        void onTouchPulse();
    }

    private final WarriorsCompanionModel model;
    private final WarriorsAssetHarvester art;
    private final Actions actions;
    private final WarriorsCompanionDraw draw = new WarriorsCompanionDraw();

    WarriorsCompanionView(Context ctx, WarriorsCompanionModel model, WarriorsAssetHarvester art, Actions actions) {
        super(ctx);
        this.model = model;
        this.art = art;
        this.actions = actions;
        setBackgroundColor(0xFF000000);
        setFocusable(false);
    }

    @Override protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        draw.drawAll(canvas, model, art.backdrop(), art.emblem(), art.minimap(),
                art.objective(), art.purahTex(), getWidth(), getHeight(), SystemClock.elapsedRealtime());
    }

    @Override public boolean performClick() { return super.performClick(); }

    @Override public boolean onTouchEvent(MotionEvent ev) {
        if (ev.getActionMasked() != MotionEvent.ACTION_DOWN) return true;
        int w = getWidth(), h = getHeight();
        if (w <= 0 || h <= 0) return true;
        float sc = Math.min(w / WarriorsCompanionDraw.LOGIC_W, h / WarriorsCompanionDraw.LOGIC_H);
        float ox = (w - WarriorsCompanionDraw.LOGIC_W * sc) * 0.5f + model.orbitDx();
        float oy = (h - WarriorsCompanionDraw.LOGIC_H * sc) * 0.5f + model.orbitDy();
        float lx = (ev.getX() - ox) / sc, ly = (ev.getY() - oy) / sc;
        if (lx < 0f || lx > WarriorsCompanionDraw.LOGIC_W || ly < 0f || ly > WarriorsCompanionDraw.LOGIC_H) {
            return true;
        }
        actions.onTouchPulse();
        if (handleHeaderTap(lx, ly)) { invalidate(); return true; }
        boolean hit = dispatchTabTap(lx, ly);
        if (hit) { performClick(); invalidate(); }
        return true;
    }

    private boolean handleHeaderTap(float lx, float ly) {
        if (ly > 92f) return false;
        for (int i = 0; i < WarriorsCompanionModel.TAB_COUNT; i++) {
            float l = 24f + i * 302f;
            if (lx >= l && lx <= l + 286f) {
                model.selectTab(i);
                actions.onTabSelected(i);
                return true;
            }
        }
        return false;
    }

    private boolean dispatchTabTap(float lx, float ly) {
        int tab = model.activeTab();
        if (tab == WarriorsCompanionModel.TAB_TACTICAL) return handleMapTap(lx, ly);
        if (tab == WarriorsCompanionModel.TAB_COMMAND) return handleCommandTap(lx, ly);
        if (tab == WarriorsCompanionModel.TAB_GRIMOIRE) return handleGrimoireTap(lx, ly);
        return handleStatusTap(lx, ly);
    }

    private boolean handleMapTap(float lx, float ly) {
        if (lx >= 24f && lx <= 748f && ly >= 108f && ly <= 1048f) {
            actions.onCycleRt();
            return true;
        }
        if (lx < 784f || lx > 1200f) return false;
        if (ly >= 404f && ly <= 496f) { actions.onFireAction(WarriorsCompanionModel.ACT_LOCKON); return true; }
        if (ly >= 512f && ly <= 604f) { actions.onCycleRt(); return true; }
        if (ly >= 620f && ly <= 716f) {
            actions.onFireAction(lx <= 992f ? (WarriorsCompanionModel.ACT_R_BASE + 1) : WarriorsCompanionModel.ACT_SPECIAL);
            return true;
        }
        if (ly >= 740f && ly <= 828f) { actions.onRescanArt(); return true; }
        return false;
    }

    private boolean handleCommandTap(float lx, float ly) {
        if (ly >= 172f && ly <= 620f) {
            boolean rSide = lx <= 610f;
            float baseL = rSide ? 24f : 630f;
            float bw = (586f - 56f) * 0.5f;
            for (int i = 0; i < 4; i++) {
                float bl = baseL + 20f + (i % 2) * (bw + 16f);
                float bt = 172f + (i / 2) * (208f + 18f);
                if (lx >= bl && lx <= bl + bw && ly >= bt && ly <= bt + 208f) {
                    actions.onFireAction((rSide ? WarriorsCompanionModel.ACT_R_BASE : WarriorsCompanionModel.ACT_L_BASE) + i);
                    return true;
                }
            }
        }
        if (ly >= 700f && ly <= 808f) {
            for (int i = 0; i < 4; i++) {
                float bl = 46f + i * 290f;
                if (lx >= bl && lx <= bl + 274f) {
                    actions.onFireAction(WarriorsCompanionModel.ACT_PARTY_BASE + i);
                    return true;
                }
            }
        }
        if (ly >= 880f && ly <= 1018f) {
            if (lx >= 46f && lx <= 324f) { actions.onFireAction(WarriorsCompanionModel.ACT_LOCKON); return true; }
            if (lx >= 340f && lx <= 618f) { actions.onFireAction(WarriorsCompanionModel.ACT_SPECIAL); return true; }
            if (lx >= 634f && lx <= 912f) { actions.onFireAction(WarriorsCompanionModel.ACT_DODGE); return true; }
            if (lx >= 928f && lx <= 1194f) { actions.onAbortChord(); return true; }
        }
        return false;
    }

    private boolean handleGrimoireTap(float lx, float ly) {
        if (ly >= 120f && ly <= 178f) {
            if (lx >= 680f && lx <= 990f) { model.toggleMacroMode(); return true; }
            if (lx >= 1006f && lx <= 1198f) { actions.onAbortChord(); return true; }
        }
        for (int i = 0; i < 6; i++) {
            float bl = 24f + (i % 3) * 402f, bt = 208f + (i / 3) * 422f;
            if (lx >= bl && lx <= bl + 388f && ly >= bt && ly <= bt + 400f) {
                if (model.macroMode) actions.onFireAction(WarriorsCompanionModel.ACT_COMBO_BASE + i);
                return true;
            }
        }
        return false;
    }

    private boolean handleStatusTap(float lx, float ly) {
        if (lx < 796f || lx > 1194f) return false;
        if (ly >= 170f && ly <= 264f) { actions.onToggleDevOnStatus(); return true; }
        if (ly >= 280f && ly <= 374f) { actions.onToggleStickLights(); return true; }
        if (ly >= 390f && ly <= 484f) { actions.onToggleStickSync(); return true; }
        if (ly >= 500f && ly <= 594f) { model.toggleAutoDim(); return true; }
        if (ly >= 746f && ly <= 840f) { actions.onRescanArt(); return true; }
        if (ly >= 856f && ly <= 950f) { actions.onClearArt(); return true; }
        return false;
    }
}
