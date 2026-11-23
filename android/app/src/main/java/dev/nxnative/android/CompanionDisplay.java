// SPDX-License-Identifier: MIT
package dev.nxnative.android;

/**
 * Pure-JVM built-in secondary display classifier matching DisplayClass semantics.
 * Selects Display 4 on AYN Thor (SINK_UNKNOWN + matching PnP manufacturer ID) or SINK_BUILT_IN.
 */
final class CompanionDisplay {
    private CompanionDisplay() {}

    static final int NONE = 0, BUILT_IN = 1, EXTERNAL = 2;
    static final int SINK_UNKNOWN = 0, SINK_BUILT_IN = 1, NO_INFO = -1;

    static final class Screen {
        final int id, sink;
        final boolean usable;
        final String pnp;

        Screen(int id, boolean usable, int sink, String pnp) {
            this.id = id;
            this.usable = usable;
            this.sink = sink;
            this.pnp = pnp;
        }
    }

    static boolean isBuiltIn(Screen s, String mainPnp) {
        if (s == null || !s.usable) return false;
        if (s.sink == SINK_BUILT_IN) return true;
        return s.sink == SINK_UNKNOWN && mainPnp != null && !mainPnp.isEmpty() && mainPnp.equals(s.pnp);
    }

    static int select(Screen[] screens, String mainPnp, boolean allowExternal) {
        if (screens == null) return -1;
        for (Screen s : screens) {
            if (isBuiltIn(s, mainPnp)) return s.id;
        }
        if (allowExternal) {
            for (Screen s : screens) {
                if (s != null && s.usable) return s.id;
            }
        }
        return -1;
    }

    static int classify(Screen[] screens, String mainPnp) {
        if (screens == null) return NONE;
        int found = NONE;
        for (Screen s : screens) {
            if (s == null || !s.usable) continue;
            if (isBuiltIn(s, mainPnp)) return BUILT_IN;
            found = EXTERNAL;
        }
        return found;
    }
}
