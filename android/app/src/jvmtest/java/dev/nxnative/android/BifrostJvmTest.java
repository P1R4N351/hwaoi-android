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

    private static void testFeedBurstRateFallbackMuteAndStop() {
        BifrostLooks.Feed feed = new BifrostLooks.Feed(false);
        BifrostLooks.State s = new BifrostLooks.State();
        CompanionState cs = makeWaist(CompanionState.READY, 1, 0, 24, 36, -1, 10);
        check(BifrostLooks.fromState(cs, s), "fromState succeeds on READY");

        // 2-poll debounce before sending display.
        List<BifrostLooks.Command> c0 = feed.poll(1000, s);
        check(c0.isEmpty(), "poll 1 debounces");
        List<BifrostLooks.Command> c1 = feed.poll(1125, s);
        check(c1.size() == 1 && BifrostLooks.ACTION_DISPLAY.equals(c1.get(0).action), "poll 2 displays BASE");
        check("PIPBOY".equals(c1.get(0).extras.get("effect")), "default uses PIPBOY");
        check(Integer.valueOf(BifrostLooks.BASE.color).equals(c1.get(0).extras.get("color")), "BASE color matches");

        // Life drop triggers burstWallMs.
        s.life = 20;
        List<BifrostLooks.Command> hit = feed.poll(1450, s);
        check(hit.size() == 1 && Long.valueOf(1450L).equals(hit.get(0).extras.get("burstWallMs")), "damage attaches burstWallMs");

        // Rate limiting <= 3 DISPLAYS_PER_SECOND within a 1000 ms window (1125, 1450, 1775 -> 2085 blocked, 2130 allowed).
        s.life = 16;
        List<BifrostLooks.Command> h2 = feed.poll(1775, s);
        check(h2.size() == 1, "display 3 within 1s allowed");
        s.life = 12;
        List<BifrostLooks.Command> h3 = feed.poll(2085, s);
        check(h3.isEmpty(), "display 4 within 1s window blocked by 3/s limit");
        List<BifrostLooks.Command> h4 = feed.poll(2130, s);
        check(h4.size() == 1, "display allowed once oldest leaves 1s window");

        // -3 validation error falls back permanently to classic public effects.
        feed.result(2200, BifrostLooks.RESULT_VALIDATION);
        check(feed.classic(), "-3 switches to classic");
        List<BifrostLooks.Command> fb = feed.poll(2600, s);
        check(fb.size() == 1 && "BREATH".equals(fb.get(0).extras.get("effect")), "classic effect sent after -3");

        // -1 or 7001 mutes for 30 s.
        feed.result(3000, BifrostLooks.RESULT_DISABLED);
        check(!feed.muteReason(3500).isEmpty(), "disabled sets mute reason");
        check(feed.poll(10_000, s).isEmpty(), "muted suppresses commands within 30s");

        // Fresh feed stop() sends immediate ACTION_CLEAR when active.
        BifrostLooks.Feed activeFeed = new BifrostLooks.Feed(false);
        activeFeed.poll(1000, s);
        check(activeFeed.poll(1125, s).size() == 1, "active feed displays");
        List<BifrostLooks.Command> clr = activeFeed.stop(2500);
        check(clr.size() == 1 && BifrostLooks.ACTION_CLEAR.equals(clr.get(0).action), "stop emits immediate ACTION_CLEAR");
    }

    public static void main(String[] args) {
        testCompanionStateBounds();
        testLookPriorityAndHysteresis();
        testFeedBurstRateFallbackMuteAndStop();
        System.out.println("BifrostJvmTest: PASS");
    }
}
