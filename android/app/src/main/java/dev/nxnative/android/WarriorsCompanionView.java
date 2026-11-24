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
        draw.drawAll(canvas, model, art.backdrop(), art.filter(), art.glyph(),
                art.borderFrieze(), art.borderEdge(), art.borderSide(),
                art.minimap(), art.objective(), art.leftHud(),
                getWidth(), getHeight(), SystemClock.elapsedRealtime());
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
        if (model.touchBlocked()) return true;
        if (handleHeaderTap(lx, ly)) { invalidate(); return true; }
        if (model.activeTab() == WarriorsCompanionModel.TAB_LEDGER && handleStatusTap(lx, ly)) {
            performClick();
            invalidate();
        }
        return true;
    }

    private boolean handleHeaderTap(float lx, float ly) {
        if (ly > 92f) return false;
        int next = lx <= 200f ? WarriorsCompanionModel.TAB_TACTICAL
                : (lx >= 1040f ? WarriorsCompanionModel.TAB_LEDGER : -1);
        if (next < 0) return false;
        model.selectTab(next);
        actions.onTabSelected(next);
        return true;
    }

    private boolean handleStatusTap(float lx, float ly) {
        if (lx < 720f || lx > 1180f || ly < 140f) return false;
        int row = (int) ((ly - 140f) / 112f);
        if (row < 0 || row > 6 || (ly - 140f - row * 112f) > 96f) return false;
        if (row == 0) actions.onToggleDevOnStatus();
        else if (row == 1) actions.onToggleStickLights();
        else if (row == 2) actions.onToggleStickSync();
        else if (row == 3) model.toggleAutoDim();
        else if (row == 4) actions.onCycleRt();
        else if (row == 5) actions.onRescanArt();
        else actions.onClearArt();
        return true;
    }
}
