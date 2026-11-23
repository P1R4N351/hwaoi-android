// SPDX-License-Identifier: MIT
package dev.nxnative.android;

import android.content.Context;
import android.hardware.display.DeviceProductInfo;
import android.hardware.display.DisplayManager;
import android.os.Handler;
import android.os.SystemClock;
import android.util.Log;
import android.view.Display;
import android.view.Gravity;
import android.view.View;
import android.widget.FrameLayout;

/**
 * Coordinates the Warriors second-screen Presentation (AYN Thor Display 4 via PnP match),
 * opt-in single-screen drawer fallback, 50 ms native polling, and safe cleanup.
 */
final class WarriorsCompanionController implements DisplayManager.DisplayListener {
    static final String AOC_TITLE_ID = "01002B00111A2000";
    static final String AOI_TITLE_ID = "04003F40232DE000";
    private static final String TAG = "WarriorsCompanion";
    private static final long POLL_MS = 50L;

    private final GameActivity activity;
    private final FrameLayout rootView;
    private final Handler ui;
    private final String titleId;
    private final DisplayManager dm;
    private final WarriorsCompanionModel model = new WarriorsCompanionModel();
    private final int[] ints = new int[WarriorsCompanionModel.DETAIL_INTS];
    private final float[] flts = new float[WarriorsCompanionModel.DETAIL_FLOATS];

    private WarriorsCompanionPresentation presentation;
    private WarriorsCompanionView presView, drawerView;
    private int presDisplayId = -1, lastPublishedTab = -1;
    private boolean started, resumed, closed, drawerOpen;

    private final Runnable pollTask = new Runnable() {
        @Override public void run() {
            if (closed || !resumed || !anyVisible()) return;
            refreshNow();
            ui.postDelayed(this, POLL_MS);
        }
    };

    static WarriorsCompanionController create(
        GameActivity activity, FrameLayout rootView, Handler ui, String gameId) {
        if (!AOI_TITLE_ID.equals(gameId) && !AOC_TITLE_ID.equals(gameId)) return null;
        return new WarriorsCompanionController(activity, rootView, ui, gameId);
    }

    private WarriorsCompanionController(
        GameActivity activity, FrameLayout rootView, Handler ui, String titleId) {
        this.activity = activity;
        this.rootView = rootView;
        this.ui = ui;
        this.titleId = titleId;
        this.model.gameKind = AOC_TITLE_ID.equals(titleId)
                ? WarriorsCompanionModel.GAME_AOC : WarriorsCompanionModel.GAME_AOI;
        this.drawerOpen = Prefs.companionDrawer(activity, titleId);
        this.dm = (DisplayManager) activity.getSystemService(Context.DISPLAY_SERVICE);
        if (this.dm != null) this.dm.registerDisplayListener(this, ui);
        ensurePresentation();
        applyDrawerVisibility();
    }

    private WarriorsCompanionView.Actions makeActions() {
        return new WarriorsCompanionView.Actions() {
            @Override public void onTabSelected(int tab) { syncPublishedTab(); refreshNow(); }
            @Override public void onFireAction(int actionId) { Native.companionFire(actionId); refreshNow(); }
            @Override public void onAbortChord() { Native.companionAbortSeq(); refreshNow(); }
            @Override public int onPinAdd(String label) { int r = Native.companionPin(0, -1, label); refreshNow(); return r; }
            @Override public void onPinRemove(int slot) { Native.companionPin(1, slot, null); refreshNow(); }
            @Override public void onPinClear() { Native.companionPin(2, -1, null); refreshNow(); }
            @Override public void onTouchPulse() { BifrostBridge.touched(); }
        };
    }

    private void syncPublishedTab() {
        int tab = model.activeTab();
        if (tab != lastPublishedTab) {
            lastPublishedTab = tab;
            Native.companionSetPage(tab);
        }
    }

    private static String mainPnp(DisplayManager dm) {
        Display main = dm.getDisplay(Display.DEFAULT_DISPLAY);
        DeviceProductInfo info = main == null ? null : main.getDeviceProductInfo();
        return info == null ? null : info.getManufacturerPnpId();
    }

    private Display selectSecondaryDisplay() {
        if (dm == null) return null;
        Display own = activity.getDisplay();
        Display[] all = dm.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION);
        if (all == null || all.length == 0) return null;
        CompanionDisplay.Screen[] screens = new CompanionDisplay.Screen[all.length];
        for (int i = 0; i < all.length; i++) {
            Display d = all[i];
            DeviceProductInfo info = d.getDeviceProductInfo();
            boolean usable = d.isValid() && (d.getFlags() & Display.FLAG_PRIVATE) == 0
                    && (own == null || d.getDisplayId() != own.getDisplayId());
            screens[i] = new CompanionDisplay.Screen(
                    d.getDisplayId(), usable,
                    info == null ? CompanionDisplay.NO_INFO : info.getConnectionToSinkType(),
                    info == null ? null : info.getManufacturerPnpId());
        }
        boolean allowExt = "1".equals(System.getenv("DM_COMPANION_EXTERNAL"));
        int chosen = CompanionDisplay.select(screens, mainPnp(dm), allowExt);
        for (Display d : all) if (d.getDisplayId() == chosen) return d;
        return null;
    }

    private void ensurePresentation() {
        if (closed || activity.isFinishing() || activity.isDestroyed()) return;
        Display target = selectSecondaryDisplay();
        if (target == null) { dismissPresentation(); return; }
        if (presentation != null && target.getDisplayId() == presDisplayId) return;
        dismissPresentation();
        try {
            WarriorsCompanionPresentation p = new WarriorsCompanionPresentation(activity, target, model, makeActions());
            p.setOnDismissListener(dlg -> {
                if (presentation == p) { presentation = null; presView = null; presDisplayId = -1; }
            });
            p.show();
            presentation = p;
            presView = p.companionView();
            presDisplayId = target.getDisplayId();
            android.graphics.Point sz = new android.graphics.Point();
            target.getRealSize(sz);
            Log.i(TAG, "presentation attached displayId=" + presDisplayId + " (" + sz.x + "x" + sz.y + ")");
        } catch (RuntimeException e) {
            Log.w(TAG, "presentation attach failed: " + e.getMessage());
            dismissPresentation();
        }
    }

    private void dismissPresentation() {
        WarriorsCompanionPresentation prev = presentation;
        presentation = null;
        presView = null;
        presDisplayId = -1;
        if (prev != null) {
            try { prev.setOnDismissListener(null); prev.dismiss(); } catch (RuntimeException ignored) {}
        }
    }

    private void applyDrawerVisibility() {
        if (drawerOpen) {
            if (drawerView == null && rootView != null) {
                drawerView = new WarriorsCompanionView(activity, model, makeActions());
                int rootW = rootView.getWidth() > 0 ? rootView.getWidth()
                        : activity.getResources().getDisplayMetrics().widthPixels;
                int dw = Math.max(460, Math.round(rootW * 0.48f));
                FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(
                        dw, FrameLayout.LayoutParams.MATCH_PARENT, Gravity.START);
                View panel = activity.panelView();
                int idx = panel != null ? Math.max(0, rootView.indexOfChild(panel)) : rootView.getChildCount();
                rootView.addView(drawerView, idx, lp);
            }
            if (drawerView != null) drawerView.setVisibility(View.VISIBLE);
        } else if (drawerView != null) {
            drawerView.setVisibility(View.GONE);
        }
        schedulePoll();
    }

    void setDrawerOpen(boolean on) {
        drawerOpen = on;
        Prefs.companionDrawer(activity, titleId, on);
        applyDrawerVisibility();
        if (on) refreshNow();
    }

    boolean isDrawerOpen() { return drawerOpen; }

    private boolean anyVisible() { return (presentation != null && started) || drawerOpen; }

    private void schedulePoll() {
        ui.removeCallbacks(pollTask);
        if (!closed && resumed && anyVisible()) ui.post(pollTask);
    }

    private void refreshNow() {
        String text = Native.companionDetail(ints, flts);
        if (text != null) model.update(ints, flts, text, SystemClock.elapsedRealtime());
        else model.updateOrbit(SystemClock.elapsedRealtime());
        syncPublishedTab();
        if (presView != null) presView.invalidate();
        if (drawerView != null && drawerOpen) drawerView.invalidate();
    }

    void onStart() {
        started = true;
        ensurePresentation();
        if (presentation != null) {
            try { presentation.show(); } catch (RuntimeException ignored) { dismissPresentation(); }
        }
        schedulePoll();
    }

    void onResume() { resumed = true; schedulePoll(); }

    void onPause() {
        resumed = false;
        ui.removeCallbacks(pollTask);
        Native.companionAbortSeq();
    }

    void onStop() {
        started = false;
        if (presentation != null) {
            try { presentation.hide(); } catch (RuntimeException ignored) {}
        }
        schedulePoll();
    }

    void close() {
        if (closed) return;
        closed = true;
        resumed = false;
        started = false;
        ui.removeCallbacks(pollTask);
        Native.companionAbortSeq();
        dismissPresentation();
        if (drawerView != null && rootView != null) {
            rootView.removeView(drawerView);
            drawerView = null;
        }
        if (dm != null) dm.unregisterDisplayListener(this);
    }

    @Override public void onDisplayAdded(int id) { ensurePresentation(); schedulePoll(); }
    @Override public void onDisplayRemoved(int id) { ensurePresentation(); schedulePoll(); }
    @Override public void onDisplayChanged(int id) { ensurePresentation(); }
}
