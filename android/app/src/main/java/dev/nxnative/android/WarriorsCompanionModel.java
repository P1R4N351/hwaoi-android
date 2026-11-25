// SPDX-License-Identifier: MIT
package dev.nxnative.android;

import java.util.Locale;

/**
 * Pure-JVM model for the converged Age of Calamity / Age of Imprisonment second screen.
 * Holds telemetry unpacked from ktgl_companion.h, live Vulkan minimap & objective metadata,
 * Purah Pad theme palette, cutscene auto-dimming, and AMOLED pixel orbit.
 */
public final class WarriorsCompanionModel {
    public static final int DETAIL_INTS = 32, DETAIL_FLOATS = 416;
    public static final int TRAIL_CAP = 128, PIN_CAP = 8, FPS_CAP = 64;
    public static final int TAB_TACTICAL = 0, TAB_LEDGER = 1, TAB_COUNT = 2;
    public static final int GAME_AOC = 1, GAME_AOI = 2;

    public int status, gameKind = GAME_AOI, activeParty, presents, saveCommits, lastSaveAgeS = -1;
    public int trailCount, seqTag = -1, seqStep, seqCount;
    public boolean inGame, writeSafe, moviePlaying, hostPaused, saveBusy, seqActive, inputActive;
    public boolean headingUp = true, macroMode = true, autoDim = true;
    public boolean devOnStatus = true, stickLights = true, stickSync = true;
    public boolean hasBackdrop, hasFilter, hasGlyph, hasBorders, hasLiveMinimap, hasLiveObjective, hasLiveLeftHud;
    public int screenBrightnessPct = -1, minimapW, minimapH, minimapCandCount, minimapCandIdx;
    public int objectiveW, objectiveH, leftHudW, leftHudH;
    public String devBuildTag = "", devOverlayText = "", assetSource = "NONE";
    public float posX, posY, posZ, yawDeg, fps, frameMs;
    public final float[] trailX = new float[TRAIL_CAP], trailY = new float[TRAIL_CAP], trailZ = new float[TRAIL_CAP];
    public final boolean[] pinActive = new boolean[PIN_CAP];
    public final float[] pinX = new float[PIN_CAP], pinY = new float[PIN_CAP], pinZ = new float[PIN_CAP];
    public final String[] pinLabels = new String[PIN_CAP];
    public final float[] fpsHist = new float[FPS_CAP];
    public int fpsHistCount, fpsHistHead, pinPresetIdx;

    private static final float[] ZOOM_METERS = {40f, 80f, 160f};
    private int tab = TAB_TACTICAL, zoomIdx = 1, orbitStep;
    private boolean seedApplied;
    private long lastOrbitMs = -1L;

    public boolean update(int[] ints, float[] flts, String txt, long nowMs) {
        updateOrbit(nowMs);
        if (ints == null || flts == null || ints.length < DETAIL_INTS || flts.length < DETAIL_FLOATS) {
            clearTelemetry();
            return false;
        }
        unpackScalarInts(ints);
        unpackScalarFloats(flts);
        unpackTrail(flts);
        unpackPins(ints[16], flts, txt);
        recordFps(fps);
        if (!seedApplied) {
            seedApplied = true;
            if (ints[18] == 1) headingUp = true;
            if (ints[19] >= 0 && ints[19] < TAB_COUNT) tab = ints[19];
        }
        return true;
    }

    private void clearTelemetry() {
        status = CompanionState.NONE;
        inGame = false; writeSafe = false; moviePlaying = false; hostPaused = false;
        saveBusy = false; seqActive = false; inputActive = false;
        trailCount = 0; seqTag = -1; seqStep = 0; seqCount = 0; lastSaveAgeS = -1;
        fps = 0f; frameMs = 0f;
        for (int i = 0; i < PIN_CAP; i++) { pinActive[i] = false; pinLabels[i] = ""; }
    }

    private void unpackScalarInts(int[] ints) {
        status = ints[0];
        inGame = ints[1] != 0;
        writeSafe = ints[2] != 0;
        moviePlaying = ints[3] != 0;
        hostPaused = ints[4] != 0;
        saveBusy = ints[5] != 0;
        gameKind = (ints[6] == GAME_AOC) ? GAME_AOC : GAME_AOI;
        activeParty = clamp(ints[7], 0, 3);
        seqActive = ints[8] != 0;
        seqTag = ints[9];
        seqStep = ints[10];
        seqCount = ints[11];
        presents = ints[12];
        saveCommits = ints[13];
        lastSaveAgeS = ints[14];
        trailCount = clamp(ints[15], 0, TRAIL_CAP);
        inputActive = ints[21] != 0;
    }

    private void unpackScalarFloats(float[] flts) {
        posX = flts[0]; posY = flts[1]; posZ = flts[2];
        yawDeg = flts[3]; fps = flts[4]; frameMs = flts[5];
    }

    private void unpackTrail(float[] flts) {
        for (int i = 0; i < trailCount; i++) {
            int b = 6 + i * 3;
            trailX[i] = flts[b]; trailY[i] = flts[b + 1]; trailZ[i] = flts[b + 2];
        }
    }

    private void unpackPins(int mask, float[] flts, String txt) {
        String[] lines = (txt != null && !txt.isEmpty()) ? txt.split("\n", -1) : new String[0];
        for (int i = 0; i < PIN_CAP; i++) {
            pinActive[i] = (mask & (1 << i)) != 0;
            int b = 390 + i * 3;
            pinX[i] = flts[b]; pinY[i] = flts[b + 1]; pinZ[i] = flts[b + 2];
            String lbl = (i < lines.length && lines[i] != null) ? lines[i].trim() : "";
            pinLabels[i] = lbl.isEmpty() ? ("Pin " + (i + 1)) : lbl;
        }
    }

    private void recordFps(float val) {
        if (val <= 0f || Float.isNaN(val)) return;
        fpsHist[fpsHistHead] = val;
        fpsHistHead = (fpsHistHead + 1) % FPS_CAP;
        if (fpsHistCount < FPS_CAP) fpsHistCount++;
    }

    public void updateOrbit(long nowMs) {
        if (lastOrbitMs < 0L) lastOrbitMs = nowMs;
        if (nowMs - lastOrbitMs >= 60_000L) {
            lastOrbitMs = nowMs;
            orbitStep = (orbitStep + 1) & 7;
        }
    }

    public int orbitDx() {
        switch (orbitStep) {
            case 1: case 2: case 3: return 1;
            case 5: case 6: case 7: return -1;
            default: return 0;
        }
    }

    public int orbitDy() {
        switch (orbitStep) {
            case 3: case 4: case 5: return 1;
            case 0: case 1: case 7: return -1;
            default: return 0;
        }
    }

    public int activeTab() { return tab; }
    public void selectTab(int t) { if (t >= 0 && t < TAB_COUNT) tab = t; }
    public void toggleHeadingUp() { headingUp = !headingUp; }
    public void toggleMacroMode() { macroMode = !macroMode; }
    public void toggleAutoDim() { autoDim = !autoDim; }

    public static final int STREAM_MINIMAP = 0, STREAM_OBJECTIVE = 1, STREAM_HUD = 2, STREAM_COUNT = 3;
    public static final int ON_SECOND = 0, ON_FIRST = 1, ON_NEITHER = 2;
    /** Where each captured module is shown; ON_SECOND is the default. */
    public final int[] streamMode = new int[STREAM_COUNT];
    public static int nextStreamMode(int mode) { return mode == ON_SECOND ? ON_FIRST : mode == ON_FIRST ? ON_NEITHER : ON_SECOND; }
    public boolean onSecond(int stream) { return streamMode[stream] == ON_SECOND; }
    public String streamModeLabel(int stream) {
        int m = streamMode[stream];
        return m == ON_SECOND ? "2ND SCREEN" : m == ON_FIRST ? "TOP SCREEN" : "HIDDEN";
    }
    public int cycleZoom() { zoomIdx = (zoomIdx + 1) % ZOOM_METERS.length; return zoomIdx; }
    public float radiusMeters() { return ZOOM_METERS[zoomIdx]; }
    public int cyclePreset() { pinPresetIdx = (pinPresetIdx + 1) % 5; return pinPresetIdx; }
    public boolean dimmed() { return autoDim && moviePlaying; }
    public boolean touchBlocked() { return dimmed(); }

    public int pinCount() {
        int n = 0;
        for (int i = 0; i < PIN_CAP; i++) if (pinActive[i]) n++;
        return n;
    }

    public boolean projectXZ(float x, float z, float[] out) {
        if (out == null || out.length < 3 || !Float.isFinite(posX) || !Float.isFinite(posZ)) return false;
        float dx = x - posX, dz = z - posZ;
        float rx = dx, rz = dz;
        if (headingUp) {
            double rad = Math.toRadians(yawDeg);
            float c = (float) Math.cos(rad), s = (float) Math.sin(rad);
            rx = dx * c + dz * s;
            rz = -dx * s + dz * c;
        }
        float r = radiusMeters();
        float nx = rx / r, ny = rz / r;
        float mag = (float) Math.hypot(nx, ny);
        out[2] = mag > 1f ? 1f : 0f;
        out[0] = mag > 1f ? (nx / mag) : nx;
        out[1] = mag > 1f ? (ny / mag) : ny;
        return true;
    }

    public String formatPinDelta(int slot) {
        if (slot < 0 || slot >= PIN_CAP || !pinActive[slot]) return "";
        float dx = pinX[slot] - posX, dy = pinY[slot] - posY, dz = pinZ[slot] - posZ;
        float d = (float) Math.hypot(dx, dz);
        String dist = d >= 999.5f ? String.format(Locale.ROOT, "%.1fkm", d / 1000f) : (Math.round(d) + "m");
        return dy > 3f ? (dist + " \u25b2") : dy < -3f ? (dist + " \u25bc") : dist;
    }

    /** Pure #000000 OLED black unless filled by a harvested game asset. */
    public int bgColor() { return 0xFF000000; }
    public int cardColor() { return 0xCC0A0F14; }
    /** Borderless layout (zero cell borders). */
    public int borderColor() { return 0x00000000; }
    /** Purah Pad luminous cyan/teal accent. */
    public int primaryColor() { return 0xFF5CE6C4; }
    /** Purah Pad antique gold accent. */
    public int secondaryColor() { return 0xFFDCC08A; }
    public String saveFileName() { return gameKind == GAME_AOC ? "svdt" : "DATA.BIN"; }

    public static String tabName(int t) {
        switch (t) {
            case TAB_TACTICAL: return "PAD";
            case TAB_LEDGER: return "STATUS";
            default: return "";
        }
    }

    public String rtCandLabel() {
        if (minimapCandCount <= 0) return "WAIT";
        int cur = ((minimapCandIdx % minimapCandCount) + minimapCandCount) % minimapCandCount;
        return (cur + 1) + " / " + minimapCandCount;
    }

    public float fpsOnePercentLow() {
        if (fpsHistCount <= 0) return 0f;
        float min1 = Float.MAX_VALUE, min2 = Float.MAX_VALUE;
        int start = (fpsHistHead + FPS_CAP - fpsHistCount) % FPS_CAP;
        for (int i = 0; i < fpsHistCount; i++) {
            float v = fpsHist[(start + i) % FPS_CAP];
            if (v < min1) { min2 = min1; min1 = v; }
            else if (v < min2) { min2 = v; }
        }
        return fpsHistCount >= 4 ? min2 : (min1 == Float.MAX_VALUE ? 0f : min1);
    }

    public String stickSyncLabel() {
        if (!stickSync) return "OFF";
        return screenBrightnessPct >= 0 ? ("ON (" + screenBrightnessPct + "%)") : "ON";
    }

    private static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
}
