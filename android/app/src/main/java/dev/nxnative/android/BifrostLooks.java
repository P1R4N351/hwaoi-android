// SPDX-License-Identifier: MIT
package dev.nxnative.android;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * Pure-JVM state machine mapping OoT's {@link CompanionState} to AYN Thor Bifrost stick-LED
 * commands ({@code com.moonbench.bifrost.external.ExternalApiReceiver}).
 */
final class BifrostLooks {
    private BifrostLooks() {}

    static final String PACKAGE = "com.moonbench.bifrost";
    static final String RECEIVER = "com.moonbench.bifrost.external.ExternalApiReceiver";
    static final String ACTION_DISPLAY = "com.moonbench.bifrost.api.ACTION_DISPLAY";
    static final String ACTION_CLEAR = "com.moonbench.bifrost.api.ACTION_CLEAR";
    static final String ACTION_PULSE = "com.moonbench.bifrost.api.ACTION_PULSE";
    static final int API_VERSION = 1;
    static final int RESULT_ACCEPTED = 0, RESULT_DISABLED = -1, RESULT_VALIDATION = -3,
            RESULT_RATE_LIMITED = -5, RESULT_UNHANDLED = 7001;

    static final class Look {
        final String name;
        final int color;
        final int intensity;
        final boolean flicker;
        final int priority;
        final String classicEffect;
        final float classicSpeed;
        final int classicIntensity;

        Look(String name, int rgb, int intensity, boolean flicker, int priority,
             String classicEffect, float classicSpeed, int classicIntensity) {
            this.name = name;
            this.color = 0xff000000 | rgb;
            this.intensity = intensity;
            this.flicker = flicker;
            this.priority = priority;
            this.classicEffect = classicEffect;
            this.classicSpeed = classicSpeed;
            this.classicIntensity = classicIntensity;
        }
    }

    /** Strips white offset and scales remaining RGB channels to full pure hue for LED rings. */
    static int hue(int rgb) {
        int r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
        int min = Math.min(r, Math.min(g, b));
        r -= min;
        g -= min;
        b -= min;
        int max = Math.max(r, Math.max(g, b));
        if (max == 0) return 0xffffff;
        return ((r * 255 / max) << 16) | ((g * 255 / max) << 8) | (b * 255 / max);
    }

    static final Look BASE = new Look("kokiri-emerald", hue(0x2ecc71), 77, false, 60, "BREATH", 0.10f, 150);
    static final Look OPEN = new Look("pause-subscreen", 0xf0d678, 85, false, 60, "STATIC", 0.5f, 255);
    static final Look OCARINA = new Look("ocarina-play", hue(0x3b9eff), 95, false, 70, "BREATH", 0.25f, 200);
    static final Look LOW = new Look("low-hearts", hue(0xff2a1a), 110, true, 80, "PULSE", 0.8f, 255);
    static final Look AOI_BASE = new Look("zonai-jade", hue(0x5ac8b8), 77, false, 60, "BREATH", 0.10f, 150);
    static final Look AOI_OPEN = new Look("zonai-open", 0xe8b84b, 85, false, 60, "STATIC", 0.5f, 255);

    static final class State {
        boolean inGame;
        boolean open;
        boolean ocarina;
        int life = -1, maxLife = -1;

        void clear() {
            inGame = false;
            open = false;
            ocarina = false;
            life = -1;
            maxLife = -1;
        }
    }

    /** Quarter-heart thresholds with 1-heart hysteresis to prevent edge chatter. */
    /** Scales a look's base LED intensity by the device screen brightness ([0..1], or <0 to disable). */
    static int scaleIntensity(int base, float screenLevel) {
        int clamped = Math.max(0, Math.min(255, base));
        if (!Float.isFinite(screenLevel) || screenLevel < 0f) return clamped;
        if (screenLevel <= 0.001f) return 0;
        float s = Math.min(1f, screenLevel);
        float factor = 0.08f + 0.92f * (float) Math.pow(s, 0.85);
        return Math.max(1, Math.min(255, Math.round(clamped * factor)));
    }

    static int lowOn(int maxLife) { return Math.max(4, (maxLife + 3) / 4); }
    static int lowOff(int maxLife) { return Math.min(maxLife, lowOn(maxLife) + 4); }

    static boolean fromState(CompanionState s, State out) {
        out.clear();
        if (s == null || !s.inGame) return false;
        out.inGame = true;
        out.open = s.menuOpen;
        out.ocarina = s.page == CompanionState.PAGE_OCARINA;
        out.life = s.life;
        out.maxLife = s.maxLife;
        return true;
    }

    static final class Command {
        final String action;
        final Map<String, Object> extras = new LinkedHashMap<>();
        final boolean wantsResult;

        Command(String action, boolean wantsResult) {
            this.action = action;
            this.wantsResult = wantsResult;
            extras.put("apiVersion", API_VERSION);
        }

        @Override public String toString() {
            return action.substring(action.lastIndexOf('.') + 1) + extras;
        }
    }

    static final class Feed {
        static final long POLL_MS = 125;
        static final int DEBOUNCE_POLLS = 2;
        static final long HEARTBEAT_MS = 500;
        static final long GAP_MS = 300;
        static final int DISPLAYS_PER_SECOND = 3, COMMANDS_PER_SECOND = 4;
        static final long PULSE_GAP_MS = 200;
        static final long MUTE_MS = 30_000, RATE_MUTE_MS = 2_000;
        static final long ENTRY_PULSE_DELAY_MS = 600;

        private boolean classic;
        private final boolean aoiPalette;
        private Look shown, candidate;
        private int candidateCount;
        private boolean low, wasOpen, wasOcarina, sawNoGame, dirty, active;
        private int lastLife = -1;
        private long epoch = -1, lastDisplay = Long.MIN_VALUE / 2, lastPulse = Long.MIN_VALUE / 2;
        private long burstAt, mutedUntil = Long.MIN_VALUE, entryPulseAt = -1;
        private final long[] sent = new long[COMMANDS_PER_SECOND];
        private int sentCount;
        private String muteReason = "";

        Feed(boolean classic) { this(classic, false); }
        Feed(boolean classic, boolean aoiPalette) { this.classic = classic; this.aoiPalette = aoiPalette; }

        boolean classic() { return classic; }
        Look shown() { return shown; }
        String muteReason(long now) { return now < mutedUntil ? muteReason : ""; }

        Look want(State s) {
            if (s == null || !s.inGame) {
                low = false;
                return null;
            }
            if (s.life >= 0 && s.maxLife >= 4) {
                if (s.life <= lowOn(s.maxLife)) low = true;
                else if (s.life >= lowOff(s.maxLife)) low = false;
            }
            if (low) return LOW;
            if (s.ocarina) return OCARINA;
            if (aoiPalette) return s.open ? AOI_OPEN : AOI_BASE;
            return s.open ? OPEN : BASE;
        }

        List<Command> poll(long now, State s) {
            List<Command> out = new ArrayList<>(2);
            if (epoch < 0) epoch = now;
            Look want = want(s);
            if (want == null) sawNoGame = true;
            if (want != candidate) {
                candidate = want;
                candidateCount = 1;
            } else if (candidateCount < DEBOUNCE_POLLS) {
                candidateCount++;
            }
            boolean pulse = false;
            if (candidate != shown && candidateCount >= DEBOUNCE_POLLS) {
                boolean entering = shown == null && sawNoGame;
                shown = candidate;
                dirty = true;
                if (shown != null) {
                    if (entering) entryPulseAt = now + ENTRY_PULSE_DELAY_MS;
                    else if (s.open != wasOpen || s.ocarina != wasOcarina) pulse = true;
                    wasOpen = s.open;
                    wasOcarina = s.ocarina;
                    sawNoGame = false;
                }
            }
            int life = s != null && s.inGame ? s.life : -1;
            if (!classic && shown != null && life >= 0 && lastLife >= 0 && life < lastLife) {
                burstAt = now;
                dirty = true;
            }
            lastLife = life;

            if (now < mutedUntil) return out;
            if (shown == null) {
                if (active && room(now, COMMANDS_PER_SECOND)) out.add(clear(now));
                return out;
            }
            if ((dirty || now - lastDisplay >= HEARTBEAT_MS) && now - lastSent() >= GAP_MS
                    && room(now, DISPLAYS_PER_SECOND)) {
                out.add(display(now));
            }
            if (!classic && active) {
                if (entryPulseAt >= 0 && now >= entryPulseAt) {
                    entryPulseAt = -1;
                    pulse(now, "STATIC", out);
                } else if (pulse) {
                    pulse(now, "PULSE", out);
                }
            }
            return out;
        }

        List<Command> stop(long now) {
            List<Command> out = new ArrayList<>(1);
            shown = null;
            candidate = null;
            candidateCount = 0;
            low = false;
            wasOpen = false;
            wasOcarina = false;
            lastLife = -1;
            sawNoGame = false;
            dirty = false;
            burstAt = 0;
            entryPulseAt = -1;
            if (active && room(now, COMMANDS_PER_SECOND)) out.add(clear(now));
            active = false;
            return out;
        }

        void result(long now, int code) {
            if (code == RESULT_ACCEPTED) return;
            if (code == RESULT_VALIDATION && !classic) {
                classic = true;
                dirty = true;
                return;
            }
            active = false;
            dirty = true;
            if (code == RESULT_RATE_LIMITED) {
                mutedUntil = now + RATE_MUTE_MS;
                muteReason = "Bifrost is rate limiting";
            } else {
                mutedUntil = now + MUTE_MS;
                muteReason = code == RESULT_DISABLED ? "\"Allow third-party LED control\" is off in Bifrost"
                        : code == RESULT_UNHANDLED ? "Bifrost did not take the command"
                        : "Bifrost refused the command (" + code + ")";
            }
        }

        private long lastSent() { return sentCount == 0 ? Long.MIN_VALUE / 2 : sent[(sentCount - 1) % sent.length]; }

        private boolean room(long now, int limit) {
            if (sentCount < limit) return true;
            return now - sent[(sentCount - limit) % sent.length] > 1000;
        }

        private void count(long now) {
            sent[sentCount % sent.length] = now;
            sentCount++;
            if (sentCount >= 2 * sent.length) sentCount -= sent.length;
        }

        private float screenLevel = -1f;

        /** Updates screen brightness ([0..1], or <0 for unscaled); marks dirty on >=4% change. */
        void setScreenBrightness(float level) {
            float next = (!Float.isFinite(level) || level < 0f) ? -1f : Math.min(1f, level);
            boolean modeFlip = (next < 0f) != (screenLevel < 0f);
            if (modeFlip || Math.abs(next - screenLevel) >= 0.04f) {
                screenLevel = next;
                if (shown != null) dirty = true;
            }
        }

        private Command display(long now) {
            Look look = shown;
            Command c = new Command(ACTION_DISPLAY, true);
            c.extras.put("effect", classic ? look.classicEffect : "PIPBOY");
            c.extras.put("color", look.color);
            c.extras.put("colorRight", look.color);
            c.extras.put("intensity", scaleIntensity(classic ? look.classicIntensity : look.intensity, screenLevel));
            c.extras.put("intensityScale", 255);
            c.extras.put("speed", classic ? look.classicSpeed : 0.5f);
            c.extras.put("priority", look.priority);
            c.extras.put("until", "EXPLICIT_CLEAR");
            if (!classic) {
                c.extras.put("phaseSeconds", Math.max(0.001f, (now - epoch) / 1000f));
                c.extras.put("flickering", look.flicker);
                if (burstAt > 0) {
                    c.extras.put("burstWallMs", burstAt);
                    burstAt = 0;
                }
            }
            c.extras.put("requestId", look.name);
            count(now);
            lastDisplay = now;
            dirty = false;
            active = true;
            return c;
        }

        private Command clear(long now) {
            Command c = new Command(ACTION_CLEAR, false);
            c.extras.put("requestId", "clear");
            count(now);
            active = false;
            dirty = false;
            return c;
        }

        List<Command> touch(long now) {
            List<Command> out = new ArrayList<>(1);
            if (now >= mutedUntil && !classic && active && shown != null && room(now, COMMANDS_PER_SECOND)) {
                pulse(now, "STATIC", out);
            }
            return out;
        }

        private void pulse(long now, String kind, List<Command> out) {
            if (now - lastPulse < PULSE_GAP_MS) return;
            Command c = new Command(ACTION_PULSE, false);
            c.extras.put("pulseKind", kind);
            lastPulse = now;
            out.add(c);
        }
    }
}
