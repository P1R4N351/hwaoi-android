// SPDX-License-Identifier: MIT
package dev.nxnative.android;

/** Host JVM verification harness for {@link WarriorsCompanionModel} and {@link CompanionDisplay}. */
public final class WarriorsCompanionJvmTest {
    private static void check(boolean cond, String msg) {
        if (!cond) throw new AssertionError(msg);
    }

    private static void testModelUnpackProjectionAndDim() {
        WarriorsCompanionModel m = new WarriorsCompanionModel();
        int[] ints = new int[WarriorsCompanionModel.DETAIL_INTS];
        float[] flts = new float[WarriorsCompanionModel.DETAIL_FLOATS];
        ints[0] = CompanionState.READY;
        ints[1] = 1;
        ints[2] = 1; // writeSafe
        ints[3] = 0; // moviePlaying
        ints[6] = WarriorsCompanionModel.GAME_AOI;
        ints[7] = 2; // activeParty
        ints[12] = 240;
        ints[13] = 3; // saveCommits
        ints[14] = 12; // lastSaveAgeS
        ints[15] = 2; // trailCount
        ints[16] = 1; // pin 0 active
        ints[18] = 1; // headingUp
        ints[19] = 0; // initialPage

        flts[0] = 0f; flts[1] = 0f; flts[2] = 0f; flts[3] = 90f;
        flts[4] = 30.2f; flts[5] = 33.1f;
        flts[6] = -10f; flts[7] = 0f; flts[8] = 0f;
        flts[9] = 0f; flts[10] = 0f; flts[11] = 0f;
        flts[390] = 40f; flts[391] = 5f; flts[392] = 0f;

        check(m.update(ints, flts, "Boss Arena\n", 1000L), "valid update returns true");
        check(m.activeParty == 2 && m.saveCommits == 3 && m.lastSaveAgeS == 12, "telemetry fields unpacked");
        check("Boss Arena".equals(m.pinLabels[0]) && "40m \u25b2".equals(m.formatPinDelta(0)), "pin delta formats 3D band");
        check(m.fpsHistCount == 1 && !m.dimmed() && !m.touchBlocked(), "live frame not dimmed");

        float[] proj = new float[3];
        check(m.projectXZ(40f, 0f, proj) && Math.abs(proj[0]) < 1e-4f && Math.abs(proj[1] + 0.5f) < 1e-4f,
                "heading-up rotates East to top");
        m.toggleHeadingUp();
        check(m.projectXZ(40f, 0f, proj) && Math.abs(proj[0] - 0.5f) < 1e-4f && Math.abs(proj[1]) < 1e-4f,
                "north-up maps East right");
        m.cycleZoom();
        check(m.radiusMeters() == 160f, "zoom cycles 80 -> 160");

        ints[2] = 0; ints[3] = 1; // moviePlaying
        m.update(ints, flts, "Boss Arena\n", 2000L);
        check(m.dimmed() && m.touchBlocked(), "movie auto-dims and blocks combat touch");
        m.toggleAutoDim();
        check(!m.dimmed() && !m.touchBlocked(), "auto-dim opt-out unblocks touch");


        for (int step = 0; step < 8; step++) {
            m.updateOrbit(step * 60_000L);
            int x0 = m.orbitDx(), y0 = m.orbitDy();
            m.updateOrbit((step + 1) * 60_000L);
            check(Math.abs(m.orbitDx() - x0) + Math.abs(m.orbitDy() - y0) <= 1, "orbit <= 1px");
        }
        check(!m.update(null, flts, "", 0L) && m.status == CompanionState.NONE, "null clears model");
    }

    private static void testDisplayClassification() {
        CompanionDisplay.Screen[] thor = {
            new CompanionDisplay.Screen(4, true, CompanionDisplay.SINK_UNKNOWN, "QCM")
        };
        check(CompanionDisplay.select(thor, "QCM", false) == 4, "AYN Thor PnP match selects Display 4");
        CompanionDisplay.Screen[] externalOnly = {
            new CompanionDisplay.Screen(5, true, CompanionDisplay.NO_INFO, null),
            new CompanionDisplay.Screen(6, true, 2, "DEL")
        };
        check(CompanionDisplay.select(externalOnly, "QCM", false) == -1, "external display rejected by default");
        check(CompanionDisplay.select(externalOnly, "QCM", true) == 5, "external opt-in selects first usable");
    }

    private static void testRtAndStatusTelemetry() {
        WarriorsCompanionModel m = new WarriorsCompanionModel();
        check(m.devOnStatus && m.stickLights && m.stickSync, "status overlay migration, stick lights, and stick sync default ON");
        m.stickLights = false;
        check(!m.stickLights, "stick lights master toggle switches OFF");
        check("SEARCHING RT".equals(m.minimapLabel()), "initial minimap label is SEARCHING RT");
        check("WAIT".equals(m.rtCandLabel()), "initial RT candidate label is WAIT");
        m.minimapW = 256; m.minimapH = 256; m.minimapCandCount = 3; m.minimapCandIdx = 1;
        check("RT 256x256".equals(m.minimapLabel()), "minimap label formats RT extent");
        check("2 / 3".equals(m.rtCandLabel()), "RT candidate label formats 1-based slot");

        check(m.bgColor() == 0xFF000000, "companion background defaults to pure black");
        check(m.borderColor() == 0x00000000, "companion panels and buttons are borderless");
        check(m.primaryColor() == 0xFF5CE6C4 && m.secondaryColor() == 0xFFDCC08A, "Purah Pad palette active");
        check("SEARCHING OBJ".equals(m.objectiveLabel()), "initial objective label");
        m.hasLiveObjective = true; m.objectiveW = 520; m.objectiveH = 96;
        check("OBJ 520x96".equals(m.objectiveLabel()), "live objective label formats extent");

        m.screenBrightnessPct = 42;
        check("ON (42%)".equals(m.stickSyncLabel()), "stick sync label reports brightness percentage");
        m.stickSync = false;
        check("OFF".equals(m.stickSyncLabel()), "stick sync OFF label");

        int[] ints = new int[WarriorsCompanionModel.DETAIL_INTS];
        float[] flts = new float[WarriorsCompanionModel.DETAIL_FLOATS];
        ints[0] = CompanionState.READY; ints[1] = 1; ints[2] = 1;
        float[] samples = {30.0f, 22.0f, 31.0f, 25.0f, 30.0f};
        for (int i = 0; i < samples.length; i++) {
            flts[4] = samples[i];
            m.update(ints, flts, "", 1000L + i * 50L);
        }
        check(Math.abs(m.fpsOnePercentLow() - 25.0f) < 1e-4f, "fpsOnePercentLow tracks second-lowest sample");
    }

    public static void main(String[] args) {
        testModelUnpackProjectionAndDim();
        testDisplayClassification();
        testRtAndStatusTelemetry();
        System.out.println("WarriorsCompanionJvmTest: PASS");
    }
}
