// SPDX-License-Identifier: MIT
package dev.nxnative.android;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Arrays;
import java.util.List;
import java.util.zip.Deflater;

/** Host JVM verification harness for {@link WarriorsCompanionModel}, {@link CompanionDisplay}, and {@link WarriorsKtglArt}. */
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
        ints[2] = 1;
        ints[6] = WarriorsCompanionModel.GAME_AOI;
        ints[7] = 2;
        ints[12] = 240;
        ints[13] = 3;
        ints[14] = 12;
        ints[15] = 2;
        ints[16] = 1;
        ints[18] = 1;
        ints[19] = 0;

        flts[0] = 0f; flts[1] = 0f; flts[2] = 0f; flts[3] = 90f;
        flts[4] = 30.2f; flts[5] = 33.1f;
        flts[6] = -10f; flts[390] = 40f; flts[391] = 5f;

        check(m.update(ints, flts, "Boss Arena\n", 1000L), "valid update returns true");
        check(m.activeParty == 2 && m.saveCommits == 3 && m.lastSaveAgeS == 12, "telemetry unpacked");
        check("Boss Arena".equals(m.pinLabels[0]) && "40m \u25b2".equals(m.formatPinDelta(0)), "pin delta formats");
        check(m.fpsHistCount == 1 && !m.dimmed() && !m.touchBlocked(), "live frame not dimmed");

        checkProjectionAndDim(m, ints, flts);
        check(!m.update(null, flts, "", 0L) && m.status == CompanionState.NONE, "null clears model");
    }

    private static void checkProjectionAndDim(WarriorsCompanionModel m, int[] ints, float[] flts) {
        float[] proj = new float[3];
        check(m.projectXZ(40f, 0f, proj) && Math.abs(proj[0]) < 1e-4f && Math.abs(proj[1] + 0.5f) < 1e-4f,
                "heading-up rotates East to top");
        m.toggleHeadingUp();
        check(m.projectXZ(40f, 0f, proj) && Math.abs(proj[0] - 0.5f) < 1e-4f && Math.abs(proj[1]) < 1e-4f,
                "north-up maps East right");
        ints[2] = 0; ints[3] = 1;
        m.update(ints, flts, "Boss Arena\n", 2000L);
        check(m.dimmed() && m.touchBlocked(), "movie auto-dims and blocks touch");
        m.toggleAutoDim();
        check(!m.dimmed() && !m.touchBlocked(), "auto-dim opt-out unblocks touch");
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

    private static void testTwoTabsAndStatusTelemetry() {
        WarriorsCompanionModel m = new WarriorsCompanionModel();
        check(WarriorsCompanionModel.TAB_COUNT == 2, "companion has 2 tabs");
        check("PAD".equals(WarriorsCompanionModel.tabName(0)), "tab 0 is PAD");
        check("STATUS".equals(WarriorsCompanionModel.tabName(1)), "tab 1 is STATUS");
        m.selectTab(1);
        check(m.activeTab() == 1, "selectTab switches to 1");
        m.selectTab(2);
        check(m.activeTab() == 1, "selectTab clamps out-of-range");
        check(!m.hasFilter && !m.hasGlyph && !m.hasBorders && !m.hasLiveLeftHud, "art flags default false");
        check(m.devOnStatus && m.stickLights && m.stickSync, "hardware toggles default ON");
        check("WAIT".equals(m.rtCandLabel()), "initial RT candidate label is WAIT");
        m.minimapCandCount = 3; m.minimapCandIdx = 1;
        check("2 / 3".equals(m.rtCandLabel()), "RT candidate label formats 1-based slot");
        check(m.bgColor() == 0xFF000000 && m.borderColor() == 0x00000000, "OLED black borderless palette");
        m.screenBrightnessPct = 42;
        check("ON (42%)".equals(m.stickSyncLabel()), "stick sync label reports brightness");
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

    private static void testKtglDecodersAndIdrk() throws Exception {
        byte[] bc3 = new byte[16];
        bc3[0] = (byte) 0x80; bc3[1] = (byte) 0x80;
        bc3[8] = (byte) 0x00; bc3[9] = (byte) 0xF8;
        int[] p3 = WarriorsKtglArt.decodeBc3(bc3, 0, 4, 4);
        check(p3.length == 16 && p3[0] == 0x80FF0000, "BC3 decodes red+alpha: " + Integer.toHexString(p3[0]));

        byte[] bc1 = new byte[8];
        bc1[0] = (byte) 0xE0; bc1[1] = 0x07;
        int[] p1 = WarriorsKtglArt.decodeBc1(bc1, 0, 4, 4);
        check(p1.length == 16 && p1[0] == 0xFF00FF00, "BC1 decodes green: " + Integer.toHexString(p1[0]));

        byte[] fdata = buildSyntheticFdata();
        WarriorsKtglArt.IdrkCollector col = new WarriorsKtglArt.IdrkCollector();
        for (int off = 0; off < fdata.length; off += 4096) {
            col.accept(fdata, off, Math.min(4096, fdata.length - off));
        }
        col.finish();
        WarriorsKtglArt.ArtBundle b = col.buildBundle();
        check(b != null && b.complete(), "synthetic IDRK/KSS/G1T bundle extracted");
        check(b.backdrop.w == 8 && b.backdrop.h == 8, "backdrop decoded");
        check(b.filter.w == 4 && b.filter.h == 4, "filter UV-cropped");
        check(b.borderEdge.w == 1240 && b.borderEdge.h == 98, "border_edge scaled cleanly");
        check(b.borderSide.w == 128 && b.borderSide.h == 1080, "border_side scaled cleanly");

        File tmp = Files.createTempDirectory("ktgl_png_").toFile();
        try {
            check(WarriorsKtglArt.saveBundlePngs(tmp, b), "saveBundlePngs succeeds");
            byte[] png = Files.readAllBytes(new File(tmp, WarriorsKtglArt.FILE_BACKDROP).toPath());
            check(png.length > 32 && (png[0] & 0xff) == 0x89 && png[1] == 'P' && png[2] == 'N' && png[3] == 'G',
                    "saved valid PNG header");
        } finally {
            File[] fs = tmp.listFiles();
            if (fs != null) for (File f : fs) f.delete();
            tmp.delete();
        }
    }

    private static byte[] buildSyntheticFdata() throws IOException {
        byte[] g1tRaw = buildSyntheticG1t();
        byte[] g1tChunks = chunkCompress(g1tRaw);
        List<String> names = List.of(
            "cmn_parts_zonau_repeat",
            "cmn_parts_fade_first_option",
            "cmn_parts_loading_glyph",
            "main_parts_wmap_zonau_font_line",
            "main_parts_wmapframe_edgeglow",
            "main_parts_wmapframe_edge",
            "main_parts_wmapframe_side_edge_00_l"
        );
        byte[] kssRaw = buildSyntheticKss(names);
        byte[] kssChunks = chunkCompress(kssRaw);
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        out.write(new byte[16]);
        writeIdrkRecord(out, 0xafbec60c, g1tRaw.length, g1tChunks);
        while ((out.size() & 15) != 0) out.write(0);
        writeIdrkRecord(out, 0xf20de437, kssRaw.length, kssChunks);
        return out.toByteArray();
    }

    private static byte[] buildSyntheticG1t() throws IOException {
        int numTex = 7, tblOff = 32;
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] hdr = new byte[tblOff + numTex * 4];
        hdr[0] = 'G'; hdr[1] = 'T'; hdr[2] = '1'; hdr[3] = 'G';
        le32(hdr, 12, tblOff); le32(hdr, 16, numTex);
        int rel = numTex * 4;
        for (int i = 0; i < numTex; i++) {
            le32(hdr, tblOff + i * 4, rel);
            rel += 20 + 64;
        }
        out.write(hdr);
        for (int i = 0; i < numTex; i++) {
            byte[] sub = new byte[20 + 64];
            sub[0] = 0x10; sub[1] = 0x5b; sub[2] = 0x33; sub[7] = 0x10; sub[8] = 12;
            Arrays.fill(sub, 20, 84, (byte) 0xff);
            out.write(sub);
        }
        return out.toByteArray();
    }

    private static byte[] buildSyntheticKss(List<String> names) {
        byte[] b = new byte[4 + names.size() * 104];
        le32(b, 0, names.size());
        for (int i = 0; i < names.size(); i++) {
            int base = 4 + i * 104;
            byte[] nm = names.get(i).getBytes(StandardCharsets.US_ASCII);
            System.arraycopy(nm, 0, b, base, nm.length);
            le32(b, base + 64, i);
            float uMax = (i == 1) ? 0.5f : 1.0f;
            float vMax = (i == 1) ? 0.5f : 1.0f;
            le32(b, base + 68, Float.floatToRawIntBits(0f));
            le32(b, base + 72, Float.floatToRawIntBits(0f));
            le32(b, base + 76, Float.floatToRawIntBits(uMax));
            le32(b, base + 96, Float.floatToRawIntBits(vMax));
        }
        return b;
    }

    private static byte[] chunkCompress(byte[] raw) throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        for (int off = 0; off < raw.length; off += 16384) {
            int n = Math.min(16384, raw.length - off);
            Deflater def = new Deflater(Deflater.DEFAULT_COMPRESSION);
            byte[] tmp = new byte[n + 64];
            def.setInput(raw, off, n);
            def.finish();
            int cl = def.deflate(tmp);
            def.end();
            byte[] ch = new byte[10];
            ch[0] = (byte) cl; ch[1] = (byte) (cl >>> 8);
            out.write(ch);
            out.write(tmp, 0, cl);
        }
        return out.toByteArray();
    }

    private static void writeIdrkRecord(ByteArrayOutputStream out, int thash, int dsz, byte[] chunks) throws IOException {
        byte[] hdr = new byte[48];
        byte[] mg = "IDRK0000".getBytes(StandardCharsets.US_ASCII);
        System.arraycopy(mg, 0, hdr, 0, 8);
        le64(hdr, 8, 48 + chunks.length);
        le64(hdr, 16, chunks.length);
        le64(hdr, 24, dsz);
        le32(hdr, 40, thash);
        out.write(hdr);
        out.write(chunks);
    }

    private static void le32(byte[] b, int o, int v) {
        b[o] = (byte) v; b[o + 1] = (byte) (v >>> 8); b[o + 2] = (byte) (v >>> 16); b[o + 3] = (byte) (v >>> 24);
    }

    private static void le64(byte[] b, int o, long v) { le32(b, o, (int) v); le32(b, o + 4, (int) (v >>> 32)); }

    public static void main(String[] args) throws Exception {
        testModelUnpackProjectionAndDim();
        testDisplayClassification();
        testTwoTabsAndStatusTelemetry();
        testKtglDecodersAndIdrk();
        System.out.println("WarriorsCompanionJvmTest: PASS");
    }
}
