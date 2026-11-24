// SPDX-License-Identifier: MIT
package dev.nxnative.android;

import java.util.List;

/** Host JVM verification harness for {@link CompanionState} and {@link BifrostLooks}. */
public final class BifrostJvmTest {
    private static void check(boolean cond, String msg) {
        if (!cond) throw new AssertionError(msg);
    }

    private static CompanionState makeWaist(int status, int inGame, int menu, int life, int maxLife, int page, long age) {
        long[] words = new long[CompanionState.FIELDS];
        words[CompanionState.STATUS] = status;
        words[CompanionState.IN_GAME] = inGame;
        words[CompanionState.MENU_OPEN] = menu;
        words[CompanionState.TABLET_OWNED] = 1;
        words[CompanionState.LIFE] = life;
        words[CompanionState.MAX_LIFE] = maxLife;
        words[CompanionState.PAGE] = page;
        words[CompanionState.AGE_MS] = age;
        CompanionState cs = new CompanionState();
        check(cs.set(words), "expected valid waist words");
        return cs;
    }

    private static void testCompanionStateBounds() {
        CompanionState cs = new CompanionState();
        check(!cs.set(null), "null array must reject");
        check(!cs.set(new long[7]), "short array must reject");
        long[] stale = {CompanionState.READY, 1, 0, 1, 12, 12, -1, CompanionState.MAX_AGE_MS + 1};
        check(!cs.set(stale), "stale age must reject");
        long[] negativeAge = {CompanionState.READY, 1, 0, 1, 12, 12, -1, -1};
        check(!cs.set(negativeAge), "negative age must reject");
        long[] none = {CompanionState.NONE, 0, 0, 1, -1, -1, -1, 0};
        check(!cs.set(none), "NONE status must reject");
        long[] badStatus = {99, 1, 0, 1, 12, 12, -1, 0};
        check(!cs.set(badStatus), "out-of-range status must reject");

        long[] title = {CompanionState.TITLE, 0, 1, 1, 20, 20, 4, 10};
        check(cs.set(title), "TITLE sample is valid");
        check(!cs.inGame && !cs.menuOpen && cs.life == -1 && cs.maxLife == -1, "non-ready clears in-game fields");

        CompanionState ready = makeWaist(CompanionState.READY, 1, 1, 24, 36, CompanionState.PAGE_OCARINA, 50);
        check(ready.inGame && ready.menuOpen && ready.life == 24 && ready.maxLife == 36, "READY fields preserved");
        long k1 = ready.key();
        ready = makeWaist(CompanionState.READY, 1, 1, 24, 36, CompanionState.PAGE_OCARINA, 900);
        check(ready.key() == k1, "key ignores ageMs");
    }

    private static void testLookPriorityAndHysteresis() {
        BifrostLooks.Feed feed = new BifrostLooks.Feed(false);
        BifrostLooks.State s = new BifrostLooks.State();
        check(feed.want(null) == null, "null state -> null look");

        // Max 12 quarter hearts (3 hearts): lowOn=4, lowOff=8.
        check(BifrostLooks.lowOn(12) == 4 && BifrostLooks.lowOff(12) == 8, "3-heart hysteresis bounds");
        // Max 20 quarter hearts (5 hearts): lowOn=5, lowOff=9.
        check(BifrostLooks.lowOn(20) == 5 && BifrostLooks.lowOff(20) == 9, "5-heart hysteresis bounds");

        s.inGame = true; s.maxLife = 12; s.life = 12; s.open = false; s.ocarina = false;
        check(feed.want(s) == BifrostLooks.BASE, "normal world -> BASE");

        s.open = true;
        check(feed.want(s) == BifrostLooks.OPEN, "menu open -> OPEN");

        s.ocarina = true;
        check(feed.want(s) == BifrostLooks.OCARINA, "ocarina outranks menu open");

        s.life = 4;
        check(feed.want(s) == BifrostLooks.LOW, "low hearts outranks ocarina and open");

        s.life = 6;
        check(feed.want(s) == BifrostLooks.LOW, "stays LOW below lowOff=8");

        s.life = 8;
        check(feed.want(s) == BifrostLooks.OCARINA, "exits LOW at lowOff=8");
    }

    private static void testFeedBurstAndRateLimit() {
        BifrostLooks.Feed feed = new BifrostLooks.Feed(false);
        BifrostLooks.State s = new BifrostLooks.State();
        CompanionState cs = makeWaist(CompanionState.READY, 1, 0, 24, 36, -1, 10);
        check(BifrostLooks.fromState(cs, s), "fromState succeeds on READY");
        check(feed.poll(1000, s).isEmpty(), "poll 1 debounces");
        List<BifrostLooks.Command> c1 = feed.poll(1125, s);
        check(c1.size() == 1 && BifrostLooks.ACTION_DISPLAY.equals(c1.get(0).action), "poll 2 displays BASE");
        check("PIPBOY".equals(c1.get(0).extras.get("effect")), "default uses PIPBOY");
        check(Integer.valueOf(BifrostLooks.BASE.color).equals(c1.get(0).extras.get("color")), "BASE color matches");
        s.life = 20;
        List<BifrostLooks.Command> hit = feed.poll(1450, s);
        check(hit.size() == 1 && Long.valueOf(1450L).equals(hit.get(0).extras.get("burstWallMs")), "damage attaches burstWallMs");
        s.life = 16;
        check(feed.poll(1775, s).size() == 1, "display 3 within 1s allowed");
        s.life = 12;
        check(feed.poll(2085, s).isEmpty(), "display 4 within 1s window blocked by 3/s limit");
        check(feed.poll(2130, s).size() == 1, "display allowed once oldest leaves 1s window");
        testFeedFallbackMuteAndStop(feed, s);
    }

    private static void testFeedFallbackMuteAndStop(BifrostLooks.Feed feed, BifrostLooks.State s) {
        feed.result(2200, BifrostLooks.RESULT_VALIDATION);
        check(feed.classic(), "-3 switches to classic");
        List<BifrostLooks.Command> fb = feed.poll(2600, s);
        check(fb.size() == 1 && "BREATH".equals(fb.get(0).extras.get("effect")), "classic effect sent after -3");
        feed.result(3000, BifrostLooks.RESULT_DISABLED);
        check(!feed.muteReason(3500).isEmpty(), "disabled sets mute reason");
        check(feed.poll(10_000, s).isEmpty(), "muted suppresses commands within 30s");
        BifrostLooks.Feed activeFeed = new BifrostLooks.Feed(false);
        activeFeed.poll(1000, s);
        check(activeFeed.poll(1125, s).size() == 1, "active feed displays");
        List<BifrostLooks.Command> clr = activeFeed.stop(2500);
        check(clr.size() == 1 && BifrostLooks.ACTION_CLEAR.equals(clr.get(0).action), "stop emits immediate ACTION_CLEAR");
    }


    private static void testAoiPaletteAndTouch() {
        BifrostLooks.Feed feed = new BifrostLooks.Feed(false, true);
        BifrostLooks.State s = new BifrostLooks.State();
        s.inGame = true;
        check(feed.want(s) == BifrostLooks.AOI_BASE && BifrostLooks.AOI_BASE.color == 0xff00ff88
                && "zonai-teal".equals(BifrostLooks.AOI_BASE.name), "AoI world -> zonai-teal");
        s.open = true;
        check(feed.want(s) == BifrostLooks.AOI_OPEN && BifrostLooks.AOI_OPEN.color == 0xff24ffa8
                && "zonai-teal-open".equals(BifrostLooks.AOI_OPEN.name), "AoI menu -> zonai-teal-open");
        s.open = false;
        feed.poll(1000L, s);
        check(feed.poll(1125L, s).size() == 1, "AoI feed activates");
        List<BifrostLooks.Command> tcmds = feed.touch(1400L);
        check(tcmds.size() == 1 && BifrostLooks.ACTION_PULSE.equals(tcmds.get(0).action), "touch emits ACTION_PULSE");
        BifrostLooks.Feed aoc = new BifrostLooks.Feed(false, BifrostLooks.PAL_AOC);
        check(aoc.want(s) == BifrostLooks.AOC_BASE && BifrostLooks.AOC_BASE.color == 0xff0060ff
                && "sheikah-blue".equals(BifrostLooks.AOC_BASE.name), "AoC world -> sheikah-blue");
        s.open = true;
        check(aoc.want(s) == BifrostLooks.AOC_OPEN && BifrostLooks.AOC_OPEN.color == 0xff3888ff
                && "sheikah-blue-open".equals(BifrostLooks.AOC_OPEN.name), "AoC menu -> sheikah-blue-open");
    }

    private static void testScreenBrightnessScaling() {
        check(BifrostLooks.scaleIntensity(200, 0.0f) == 0, "0% brightness -> 0 intensity");
        check(BifrostLooks.scaleIntensity(200, 1.0f) == 200, "100% brightness -> base intensity");
        check(BifrostLooks.scaleIntensity(200, -1.0f) == 200, "negative disables scaling");
        check(BifrostLooks.scaleIntensity(200, Float.NaN) == 200, "NaN disables scaling");
        int b20 = BifrostLooks.scaleIntensity(200, 0.20f);
        int b50 = BifrostLooks.scaleIntensity(200, 0.50f);
        int b100 = BifrostLooks.scaleIntensity(200, 1.0f);
        check(b20 > 0 && b20 < b50 && b50 < b100, "monotonic brightness scaling");

        BifrostLooks.Feed feed = new BifrostLooks.Feed(false, true);
        BifrostLooks.State s = new BifrostLooks.State();
        s.inGame = true;
        feed.setScreenBrightness(0.25f);
        feed.poll(1000L, s);
        List<BifrostLooks.Command> c0 = feed.poll(1125L, s);
        check(c0.size() == 1, "feed displays scaled command");
        int baseInt = BifrostLooks.AOI_BASE.intensity;
        int expectLow = BifrostLooks.scaleIntensity(baseInt, 0.25f);
        check(Integer.valueOf(255).equals(c0.get(0).extras.get("intensityScale")), "intensityScale stays 255");
        check(Integer.valueOf(expectLow).equals(c0.get(0).extras.get("intensity")), "intensity scaled at 25%");

        feed.setScreenBrightness(0.27f); // < 4% delta (< 500ms heartbeat): hysteresis suppresses extra send
        check(feed.poll(1475L, s).isEmpty(), "small brightness delta suppressed by hysteresis");

        feed.setScreenBrightness(0.65f); // >= 4% delta (>= 300ms gap, < 500ms heartbeat): marks dirty immediately
        List<BifrostLooks.Command> c1 = feed.poll(1475L, s);
        check(c1.size() == 1 && Integer.valueOf(BifrostLooks.scaleIntensity(baseInt, 0.65f)).equals(c1.get(0).extras.get("intensity")),
                "significant brightness change triggers immediate update");

        feed.setScreenBrightness(-1f);
        List<BifrostLooks.Command> c2 = feed.poll(1825L, s);
        check(c2.size() == 1 && Integer.valueOf(baseInt).equals(c2.get(0).extras.get("intensity")),
                "disabling brightness sync restores full base intensity");
    }

    public static void main(String[] args) {
        testCompanionStateBounds();
        testLookPriorityAndHysteresis();
        testFeedBurstAndRateLimit();
        testAoiPaletteAndTouch();
        testScreenBrightnessScaling();
        System.out.println("BifrostJvmTest: PASS");
    }
}
