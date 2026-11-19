// SPDX-License-Identifier: MIT
package dev.nxnative.android;

/**
 * The second screen's common game state matching dm/host/android/companion_state.h.
 * Pure Java (no Android imports) so host JVM tests run it directly.
 */
public final class CompanionState {
    /** Game status values exported at word {@link #STATUS}. */
    public static final int NONE = 0, STARTING = 1, UNSUPPORTED = 2, TITLE = 3, LOADING = 4, READY = 5;
    /** Well-known second-screen page index when Ocarina playback is active. */
    public static final int PAGE_OCARINA = 4;
    /** Word order of {@code Native.companionState(long[])}. */
    public static final int STATUS = 0, IN_GAME = 1, MENU_OPEN = 2, TABLET_OWNED = 3, LIFE = 4, MAX_LIFE = 5,
            PAGE = 6, AGE_MS = 7, FIELDS = 8;
    /** Samples older than this are treated as stale. */
    public static final long MAX_AGE_MS = 2000;

    public int status = NONE;
    public boolean inGame;
    public boolean menuOpen;
    public boolean tabletOwned = true;
    public int life = -1, maxLife = -1;
    public int page = -1;

    public void clear() {
        status = NONE;
        inGame = false;
        menuOpen = false;
        tabletOwned = true;
        life = -1;
        maxLife = -1;
        page = -1;
    }

    /** Loads the 8 native waist words. Returns false (and clears state) if invalid or stale. */
    public boolean set(long[] v) {
        clear();
        if (v == null || v.length < FIELDS) return false;
        long age = v[AGE_MS], s = v[STATUS];
        if (age < 0 || age > MAX_AGE_MS || s <= NONE || s > READY) return false;
        status = (int) s;
        inGame = status == READY && v[IN_GAME] == 1;
        menuOpen = inGame && v[MENU_OPEN] == 1;
        tabletOwned = !inGame || v[TABLET_OWNED] != 0;
        if (inGame) {
            life = (int) v[LIFE];
            maxLife = (int) v[MAX_LIFE];
        }
        page = (int) v[PAGE];
        return true;
    }

    /** Change key covering every field except sample age. */
    public long key() {
        return status
                | (inGame ? 8L : 0L)
                | (menuOpen ? 16L : 0L)
                | (tabletOwned ? 32L : 0L)
                | ((long) (life & 0xffff) << 8)
                | ((long) (maxLife & 0xffff) << 24)
                | ((long) (page & 0xff) << 40);
    }
}
