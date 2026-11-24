// SPDX-License-Identifier: MIT
package dev.nxnative.android;

import android.content.Context;
import android.graphics.Bitmap;
import android.hardware.display.DeviceProductInfo;
import android.hardware.display.DisplayManager;
import android.os.Handler;
import android.os.SystemClock;
import android.util.Log;
import android.view.Display;
import android.view.Gravity;
import android.view.View;
import android.widget.FrameLayout;
import java.io.File;
import java.nio.ByteBuffer;

/**
 * Coordinates the Warriors second-screen Presentation (AYN Thor Display 4 via PnP match),
 * native Vulkan offscreen minimap RT streaming, runtime BYOG asset harvesting,
 * Display 0 dev-HUD migration, and opt-in single-screen drawer fallback.
 */
final class WarriorsCompanionController implements DisplayManager.DisplayListener {
    static final String AOC_TITLE_ID = "01002B00111A2000";
    static final String AOI_TITLE_ID = "04003F40232DE000";
    private static final String TAG = "WarriorsCompanion";
    private static final long POLL_MS = 50L;
    private static final int MINIMAP_MAX_BYTES = 768 * 768 * 4;
    private static final int OBJ_MAX_BYTES = 768 * 224 * 4, UI_TEX_BYTES = 512 * 512 * 4;

    private final GameActivity activity;
    private final FrameLayout rootView;
    private final Handler ui;
    private final String titleId;
    private final DisplayManager dm;
    private final WarriorsCompanionModel model = new WarriorsCompanionModel();
    private final WarriorsAssetHarvester art = new WarriorsAssetHarvester();
    private final int[] ints = new int[WarriorsCompanionModel.DETAIL_INTS];
    private final float[] flts = new float[WarriorsCompanionModel.DETAIL_FLOATS];
    private final byte[] minimapBytes = new byte[MINIMAP_MAX_BYTES];
    private final int[] minimapMeta = new int[5];
    private final byte[] objBytes = new byte[OBJ_MAX_BYTES];
    private final int[] objMeta = new int[4];
    private final byte[] uiBytes = new byte[UI_TEX_BYTES];
    private final int[] uiMeta = new int[4];

    private WarriorsCompanionPresentation presentation;
    private WarriorsCompanionView presView, drawerView;
    private Bitmap minimapBmp, objBmp;
    private int presDisplayId = -1, lastPublishedTab = -1, uiTexTick, uiTexTries;
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
        this.model.devOnStatus = Prefs.overlayOnSecondScreen(activity);
        this.model.stickLights = Prefs.bifrost(activity, titleId);
        this.model.stickSync = Prefs.bifrostScreenSync(activity);
        BifrostBridge.setEnabled(this.model.stickLights);
        this.drawerOpen = Prefs.companionDrawer(activity, titleId);
        this.art.loadCached(activity);
        syncArtFlags();
        this.art.harvestFromGameDirsAsync(activity, harvestDirs(), model.gameKind, this::onArtUpdated);
        this.dm = (DisplayManager) activity.getSystemService(Context.DISPLAY_SERVICE);
        if (this.dm != null) this.dm.registerDisplayListener(this, ui);
        ensurePresentation();
        applyDrawerVisibility();
    }

    boolean isSecondaryActive() { return presentation != null && started && !closed; }

    void setDevOverlay(String buildTag, String overlayText, boolean movedToStatus) {
        model.devBuildTag = buildTag != null ? buildTag : "";
        model.devOverlayText = overlayText != null ? overlayText : "";
        model.devOnStatus = movedToStatus;
    }

    private void onArtUpdated() {
        syncArtFlags();
        invalidateViews();
    }

    private void syncArtFlags() {
        model.hasBackdrop = art.backdrop() != null;
        model.hasEmblem = art.emblem() != null;
        model.hasLiveMinimap = art.minimap() != null;
        model.hasLiveObjective = art.objective() != null;
        model.hasPurahTex = art.purahTex() != null;
        model.assetSource = art.source();
    }

    private WarriorsCompanionView.Actions makeActions() {
        return new WarriorsCompanionView.Actions() {
            @Override public void onTabSelected(int tab) {
                Native.companionMinimapEnable(minimapWantActive());
                syncPublishedTab();
                refreshNow();
            }
            @Override public void onFireAction(int actionId) { Native.companionFire(actionId); refreshNow(); }
            @Override public void onAbortChord() { Native.companionAbortSeq(); refreshNow(); }
            @Override public void onToggleDevOnStatus() {
                boolean next = !Prefs.overlayOnSecondScreen(activity);
                Prefs.overlayOnSecondScreen(activity, next);
                model.devOnStatus = next;
                invalidateViews();
            }
            @Override public void onToggleStickLights() {
                boolean next = !Prefs.bifrost(activity, titleId);
                Prefs.bifrost(activity, titleId, next);
                BifrostBridge.setEnabled(next);
                model.stickLights = next;
                invalidateViews();
            }
            @Override public void onToggleStickSync() {
                boolean next = !Prefs.bifrostScreenSync(activity);
                Prefs.bifrostScreenSync(activity, next);
                BifrostBridge.syncScreenBrightness = next;
                model.stickSync = next;
                invalidateViews();
            }
            @Override public void onCycleRt() {
                model.minimapCandIdx = Native.companionMinimapCycle();
                minimapMeta[2] = 0;
                refreshNow();
            }
            @Override public void onRescanArt() {
                uiTexTries = 0; uiTexTick = 0;
                art.harvestFromGameDirsAsync(activity, harvestDirs(), model.gameKind, WarriorsCompanionController.this::onArtUpdated);
            }
            @Override public void onClearArt() {
                art.clearCache(activity);
                onArtUpdated();
            }
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
            WarriorsCompanionPresentation p = new WarriorsCompanionPresentation(activity, target, model, art, makeActions());
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
                drawerView = new WarriorsCompanionView(activity, model, art, makeActions());
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

    private void invalidateViews() {
        if (presView != null) presView.invalidate();
        if (drawerView != null && drawerOpen) drawerView.invalidate();
    }

    private void refreshNow() {
        long now = SystemClock.elapsedRealtime();
        String text = Native.companionDetail(ints, flts);
        if (text != null) model.update(ints, flts, text, now);
        else model.updateOrbit(now);
        model.stickLights = BifrostBridge.enabled;
        model.stickSync = BifrostBridge.syncScreenBrightness;
        model.screenBrightnessPct = BifrostBridge.lastScreenPct;
        model.devOnStatus = Prefs.overlayOnSecondScreen(activity);
        if (minimapWantActive()) {
            Native.companionMinimapEnable(true);
            pollMinimapStream();
            pollObjectiveStream();
            pollGpuUiTex();
        } else {
            Native.companionMinimapEnable(false);
            art.setObjective(null);
        }
        syncArtFlags();
        syncPublishedTab();
        invalidateViews();
    }

    void onStart() {
        started = true;
        ensurePresentation();
        if (presentation != null) {
            try { presentation.show(); } catch (RuntimeException ignored) { dismissPresentation(); }
        }
        schedulePoll();
    }

    private void pollMinimapStream() {
        int got = Native.companionMinimap(minimapBytes, minimapMeta);
        int rw = minimapMeta[0], rh = minimapMeta[1];
        model.minimapW = rw;
        model.minimapH = rh;
        model.minimapCandCount = minimapMeta[3];
        model.minimapCandIdx = minimapMeta[4];
        if (got == 1 && rw > 0 && rw <= 768 && rh > 0 && rh <= 768 && rw * rh * 4 <= minimapBytes.length) {
            if (minimapBmp == null || minimapBmp.getWidth() != rw || minimapBmp.getHeight() != rh) {
                minimapBmp = Bitmap.createBitmap(rw, rh, Bitmap.Config.ARGB_8888);
            }
            minimapBmp.copyPixelsFromBuffer(ByteBuffer.wrap(minimapBytes, 0, rw * rh * 4));
            art.setMinimap(minimapBmp);
        }
    }

    private void pollObjectiveStream() {
        int got = Native.companionObjective(objBytes, objMeta);
        int ow = objMeta[0], oh = objMeta[1], active = objMeta[3];
        model.objectiveW = ow;
        model.objectiveH = oh;
        if (active == 0) {
            art.setObjective(null);
        } else if (got == 1 && ow > 0 && ow <= 768 && oh > 0 && oh <= 224 && ow * oh * 4 <= objBytes.length) {
            if (objBmp == null || objBmp.getWidth() != ow || objBmp.getHeight() != oh) {
                objBmp = Bitmap.createBitmap(ow, oh, Bitmap.Config.ARGB_8888);
            }
            objBmp.copyPixelsFromBuffer(ByteBuffer.wrap(objBytes, 0, ow * oh * 4));
            art.setObjective(objBmp);
        }
    }

    private void pollGpuUiTex() {
        if (art.purahTex() != null || art.isSuppressed() || uiTexTries >= 8) return;
        boolean req = (uiTexTick % 30) == 0 && uiMeta[3] == 0;
        uiTexTick++;
        if (Native.companionUiTex(uiBytes, uiMeta, req) == 1) {
            uiTexTries++;
            art.ingestGpuUiTex(activity, uiBytes, uiMeta[0], uiMeta[1], model.gameKind);
        }
    }

    private boolean minimapWantActive() {
        return !closed && resumed && anyVisible() && model.inGame && !model.moviePlaying && !model.hostPaused;
    }

    private File[] harvestDirs() {
        String g = activity.getIntent() != null ? activity.getIntent().getStringExtra("game") : null;
        String p = activity.getIntent() != null ? activity.getIntent().getStringExtra("pack") : null;
        File gd = g != null && !g.isEmpty() ? new File(g) : null;
        return new File[] {
            gd != null && gd.isDirectory() ? new File(gd, "romfs/Movie") : null,
            gd != null && gd.isDirectory() ? new File(gd, "romfs") : null,
            gd,
            p != null && !p.isEmpty() ? new File(p) : null,
            AppStorage.pack(activity, titleId),
            activity.getExternalFilesDir(null),
        };
    }

    void onResume() {
        resumed = true;
        Native.companionMinimapEnable(minimapWantActive());
        schedulePoll();
    }

    void onPause() {
        resumed = false;
        ui.removeCallbacks(pollTask);
        Native.companionMinimapEnable(false);
        Native.companionAbortSeq();
    }

    void onStop() {
        started = false;
        Native.companionMinimapEnable(false);
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
        Native.companionMinimapEnable(false);
        Native.companionAbortSeq();
        art.setMinimap(null);
        art.setObjective(null);
        minimapBmp = null;
        objBmp = null;
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
