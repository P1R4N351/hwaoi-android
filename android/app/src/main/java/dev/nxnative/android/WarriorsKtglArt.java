// SPDX-License-Identifier: MIT
package dev.nxnative.android;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.zip.CRC32;
import java.util.zip.DataFormatException;
import java.util.zip.Deflater;
import java.util.zip.Inflater;

/**
 * Pure-Java runtime KTGL asset extractor for Age of Imprisonment Purah Pad chrome.
 * Streams only the first 29.3 MB prefix of {@code romfs/b6e28c40/0x6df2ba2a.fdata}
 * from a user-supplied .nup or unpacked dump directory and inflates only matched G1T slices.
 */
final class WarriorsKtglArt {
    static final long MAX_PREFIX_BYTES = 29_300_000L;
    static final String FDATA_REL = "romfs/b6e28c40/0x6df2ba2a.fdata";
    static final String FILE_BACKDROP = "backdrop.png";
    static final String FILE_FILTER = "filter.png";
    static final String FILE_GLYPH = "glyph.png";
    static final String FILE_FRIEZE = "border_frieze.png";
    static final String FILE_EDGE = "border_edge.png";
    static final String FILE_SIDE = "border_side.png";

    private static final int MAX_IDRK_RECS = 4096, MAX_G1T_BUF = 28_000_000, MAX_KSS_BUF = 2_000_000;
    private static final int CHUNK_OUT = 16384, THASH_G1T = 0xafbec60c, THASH_KSS = 0xf20de437;
    private static final int TINT_GLOW = 0xB85CE6C4, TINT_EDGE = 0xE688F4DC;
    private static final List<String> TARGET_NAMES = List.of(
        "cmn_parts_zonau_repeat",
        "cmn_parts_fade_first_option",
        "cmn_parts_loading_glyph",
        "main_parts_wmap_zonau_font_line",
        "main_parts_wmapframe_edgeglow",
        "main_parts_wmapframe_edge",
        "main_parts_wmapframe_side_edge_00_l"
    );

    private WarriorsKtglArt() {}

    static final class ArtImage {
        final int w, h;
        final int[] argb;
        ArtImage(int w, int h, int[] argb) { this.w = w; this.h = h; this.argb = argb; }
    }

    static final class ArtBundle {
        final ArtImage backdrop, filter, glyph, borderFrieze, borderEdge, borderSide;
        ArtBundle(ArtImage backdrop, ArtImage filter, ArtImage glyph,
                  ArtImage borderFrieze, ArtImage borderEdge, ArtImage borderSide) {
            this.backdrop = backdrop; this.filter = filter; this.glyph = glyph;
            this.borderFrieze = borderFrieze; this.borderEdge = borderEdge; this.borderSide = borderSide;
        }
        boolean complete() {
            return backdrop != null && filter != null && glyph != null
                    && borderFrieze != null && borderEdge != null && borderSide != null;
        }
    }

    private static final class KssHit {
        final int targetIdx, texIdx;
        final float uMin, vMin, uMax, vMax;
        KssHit(int targetIdx, int texIdx, float uMin, float vMin, float uMax, float vMax) {
            this.targetIdx = targetIdx; this.texIdx = texIdx;
            this.uMin = uMin; this.vMin = vMin; this.uMax = uMax; this.vMax = vMax;
        }
    }

    static ArtBundle extract(File[] roots) {
        if (roots == null) return null;
        int lim = Math.min(roots.length, 64);
        for (int i = 0; i < lim; i++) {
            ArtBundle b = tryCandidate(roots[i]);
            if (b != null && b.complete()) return b;
        }
        return null;
    }

    static boolean saveBundlePngs(File outDir, ArtBundle b) {
        if (outDir == null || b == null || !b.complete()) return false;
        if (!outDir.exists() && !outDir.mkdirs()) return false;
        return writePng(new File(outDir, FILE_BACKDROP), b.backdrop)
                && writePng(new File(outDir, FILE_FILTER), b.filter)
                && writePng(new File(outDir, FILE_GLYPH), b.glyph)
                && writePng(new File(outDir, FILE_FRIEZE), b.borderFrieze)
                && writePng(new File(outDir, FILE_EDGE), b.borderEdge)
                && writePng(new File(outDir, FILE_SIDE), b.borderSide);
    }

    private static ArtBundle tryCandidate(File root) {
        if (root == null) return null;
        String raw = root.getPath();
        try {
            Nup.Ref ref = Nup.parse(raw);
            if (ref != null) {
                ArtBundle b = extractNup(new File(ref.path), ref.selector);
                if (b != null) return b;
            }
        } catch (IllegalArgumentException ignored) {}
        if (Nup.extOf(root.getName()) != null && (root.isFile() || Nup.picked(root))) {
            return extractNup(root, null);
        }
        if (root.isFile() && "0x6df2ba2a.fdata".equalsIgnoreCase(root.getName())) {
            return extractFdataFile(root);
        }
        return root.isDirectory() ? tryDir(root) : null;
    }

    private static ArtBundle tryDir(File dir) {
        List<File> direct = List.of(
            new File(dir, FDATA_REL),
            new File(dir, "b6e28c40/0x6df2ba2a.fdata"),
            new File(dir, "0x6df2ba2a.fdata"),
            new File(dir, "base/" + FDATA_REL)
        );
        for (int i = 0; i < direct.size(); i++) {
            File f = direct.get(i);
            if (f.isFile()) {
                ArtBundle b = extractFdataFile(f);
                if (b != null && b.complete()) return b;
            }
        }
        File[] list = dir.listFiles();
        if (list == null) return null;
        int lim = Math.min(list.length, 64);
        for (int i = 0; i < lim; i++) {
            File f = list[i];
            if (f != null && f.isFile() && Nup.extOf(f.getName()) != null) {
                ArtBundle b = extractNup(f, null);
                if (b != null && b.complete()) return b;
            }
        }
        return null;
    }

    private static ArtBundle extractNup(File nupFile, String selector) {
        if (nupFile == null || (!nupFile.isFile() && !Nup.picked(nupFile))) return null;
        try {
            Nup nup = Nup.open(nupFile);
            Nup.Node node = findFdataNode(nup, selector);
            if (node == null || node.dir) return null;
            IdrkCollector col = new IdrkCollector();
            nup.readPrefixTo(node, MAX_PREFIX_BYTES, col);
            col.finish();
            return col.buildBundle();
        } catch (IOException | RuntimeException e) {
            return null;
        }
    }

    private static Nup.Node findFdataNode(Nup nup, String selector) throws IOException {
        Nup.Pkg base = nup.base();
        if (base != null) {
            Nup.Node n = nup.lookup(base, FDATA_REL);
            if (n != null && !n.dir) return n;
        }
        Nup.Pkg newest = nup.newest();
        if (newest != null && newest != base) {
            Nup.Node n = nup.lookup(newest, FDATA_REL);
            if (n != null && !n.dir) return n;
        }
        if (selector != null) {
            Nup.Pkg sel = nup.find(selector);
            if (sel != null && sel != base && sel != newest) return nup.lookup(sel, FDATA_REL);
        }
        return null;
    }

    private static ArtBundle extractFdataFile(File f) {
        IdrkCollector col = new IdrkCollector();
        byte[] buf = new byte[65536];
        long left = Math.min(f.length(), MAX_PREFIX_BYTES);
        try (FileInputStream in = new FileInputStream(f)) {
            for (int it = 0; it < 4096 && left > 0 && !col.isDone(); it++) {
                int want = (int) Math.min(buf.length, left);
                int got = in.read(buf, 0, want);
                if (got <= 0) break;
                col.accept(buf, 0, got);
                left -= got;
            }
            col.finish();
            return col.buildBundle();
        } catch (IOException | RuntimeException e) {
            return null;
        }
    }

    static final class IdrkCollector implements Zstd.Sink {
        private static final int ST_SEEK = 0, ST_HDR = 1, ST_PRE = 2, ST_BODY = 3, ST_DONE = 4;
        private final byte[] hdr = new byte[48];
        private final ArtImage[] rawImgs = new ArtImage[TARGET_NAMES.size()];
        private int state = ST_SEEK, hdrFill, recCount, padSkipped, thash, recFill;
        private long streamPos, nextOff = 16L, cstartOff, recEndOff, dsz, lastG1tDsz;
        private byte[] recBuf, lastG1t;

        boolean isDone() { return state == ST_DONE; }

        @Override public void accept(byte[] b, int off, int len) {
            int pos = off, end = off + len;
            for (int g = 0; g < 8192 && pos < end && state != ST_DONE; g++) {
                if (state == ST_SEEK) pos = stepSeek(b, pos, end);
                else if (state == ST_HDR) pos = stepHdr(b, pos, end);
                else if (state == ST_PRE) pos = stepPre(pos, end);
                else if (state == ST_BODY) pos = stepBody(b, pos, end);
            }
        }

        void finish() {
            if (state == ST_BODY && recFill > 0 && streamPos >= recEndOff) finishRecord();
            lastG1t = null;
            recBuf = null;
        }

        private int stepSeek(byte[] b, int pos, int end) {
            if (streamPos < nextOff) {
                int sk = (int) Math.min(end - pos, nextOff - streamPos);
                streamPos += sk;
                return pos + sk;
            }
            int cp = Math.min(end - pos, 4 - hdrFill);
            System.arraycopy(b, pos, hdr, hdrFill, cp);
            hdrFill += cp;
            streamPos += cp;
            if (hdrFill < 4) return pos + cp;
            if (hdr[0] == 0 && hdr[1] == 0 && hdr[2] == 0 && hdr[3] == 0 && padSkipped < 16) {
                hdrFill = 0;
                padSkipped += 4;
            } else if (hdr[0] == 'I' && hdr[1] == 'D' && hdr[2] == 'R' && hdr[3] == 'K') {
                state = ST_HDR;
            } else {
                state = ST_DONE;
            }
            return pos + cp;
        }

        private int stepHdr(byte[] b, int pos, int end) {
            int cp = Math.min(end - pos, 48 - hdrFill);
            System.arraycopy(b, pos, hdr, hdrFill, cp);
            hdrFill += cp;
            streamPos += cp;
            if (hdrFill == 48) beginRecord();
            return pos + cp;
        }

        private void beginRecord() {
            if (hdr[4] != '0' || hdr[5] != '0' || hdr[6] != '0' || hdr[7] != '0') { state = ST_DONE; return; }
            long recOff = streamPos - 48L;
            long esz = le64(hdr, 8), csz = le64(hdr, 16);
            dsz = le64(hdr, 24);
            thash = le32(hdr, 40);
            if (esz < 48L || csz > esz - 48L || recCount >= MAX_IDRK_RECS) { state = ST_DONE; return; }
            cstartOff = recOff + (esz - csz);
            recEndOff = recOff + esz;
            recFill = 0;
            boolean wantG1t = thash == THASH_G1T && csz > 0 && csz <= MAX_G1T_BUF;
            boolean wantKss = thash == THASH_KSS && lastG1t != null && csz > 0 && csz <= MAX_KSS_BUF;
            recBuf = (wantG1t || wantKss) ? new byte[(int) csz] : null;
            state = (streamPos < cstartOff) ? ST_PRE : (streamPos < recEndOff ? ST_BODY : finishNext());
        }

        private int stepPre(int pos, int end) {
            int sk = (int) Math.min(end - pos, cstartOff - streamPos);
            streamPos += sk;
            if (streamPos >= cstartOff) state = (streamPos < recEndOff) ? ST_BODY : finishNext();
            return pos + sk;
        }

        private int stepBody(byte[] b, int pos, int end) {
            int cp = (int) Math.min(end - pos, recEndOff - streamPos);
            if (recBuf != null && recFill + cp <= recBuf.length) System.arraycopy(b, pos, recBuf, recFill, cp);
            recFill += cp;
            streamPos += cp;
            if (streamPos >= recEndOff) state = finishNext();
            return pos + cp;
        }

        private int finishNext() { finishRecord(); return state; }

        private void finishRecord() {
            if (thash == THASH_G1T && recBuf != null) {
                lastG1t = recBuf;
                lastG1tDsz = dsz;
            } else if (thash == THASH_KSS && recBuf != null && lastG1t != null) {
                processPairedKss(recBuf, (int) Math.min(dsz, MAX_KSS_BUF), lastG1t, lastG1tDsz, rawImgs);
                lastG1t = null;
            }
            recBuf = null;
            recCount++;
            padSkipped = 0;
            hdrFill = 0;
            if (allRawPresent() || recCount >= MAX_IDRK_RECS) state = ST_DONE;
            else { nextOff = (recEndOff + 3L) & ~3L; state = ST_SEEK; }
        }

        private boolean allRawPresent() {
            for (int i = 0; i < rawImgs.length; i++) if (rawImgs[i] == null) return false;
            return true;
        }

        ArtBundle buildBundle() {
            if (!allRawPresent()) return null;
            ArtImage bg = rawImgs[0];
            ArtImage filt = rawImgs[1];
            ArtImage gly = rawImgs[2];
            ArtImage frz = rawImgs[3];
            ArtImage glow = scaleBilinear(tintSrcIn(rawImgs[4], TINT_GLOW), rawImgs[5].w, rawImgs[5].h);
            ArtImage edge = scaleBilinear(alphaOver(glow, tintSrcIn(rawImgs[5], TINT_EDGE)), 1240, 98);
            ArtImage side = scaleBilinear(rawImgs[6], 128, 1080);
            return new ArtBundle(bg, filt, gly, frz, edge, side);
        }
    }

    static void processPairedKss(
        byte[] kssChunks, int kssDsz, byte[] g1tChunks, long g1tDsz, ArtImage[] outImgs) {
        if (kssDsz < 4 || kssDsz > MAX_KSS_BUF) return;
        byte[] kss = inflateIdrkRecord(kssChunks, kssDsz);
        if (kss == null || kss.length < 4) return;
        List<KssHit> hits = matchKssEntries(kss, outImgs);
        if (hits.isEmpty()) return;
        extractG1tHits(g1tChunks, g1tDsz, hits, outImgs);
    }

    private static List<KssHit> matchKssEntries(byte[] kss, ArtImage[] outImgs) {
        List<KssHit> hits = new ArrayList<>();
        int count = Math.min(le32(kss, 0), (kss.length - 4) / 104);
        for (int i = 0; i < count; i++) {
            int base = 4 + i * 104;
            int nlen = 0;
            for (int j = 0; j < 64 && kss[base + j] != 0; j++) nlen++;
            String nm = new String(kss, base, nlen, StandardCharsets.US_ASCII);
            int idx = TARGET_NAMES.indexOf(nm);
            if (idx >= 0 && outImgs[idx] == null) {
                int texIdx = le32(kss, base + 64);
                float u0 = leF32(kss, base + 68), v0 = leF32(kss, base + 72);
                float u1 = leF32(kss, base + 76), v3 = leF32(kss, base + 96);
                hits.add(new KssHit(idx, texIdx, Math.min(u0, u1), Math.min(v0, v3), Math.max(u0, u1), Math.max(v0, v3)));
            }
        }
        return hits;
    }

    private static void extractG1tHits(byte[] g1tChunks, long g1tDsz, List<KssHit> hits, ArtImage[] outImgs) {
        byte[] hdr = inflateIdrkRecord(g1tChunks, 65536);
        if (hdr == null || hdr.length < 20 || hdr[0] != 'G' || hdr[1] != 'T' || hdr[2] != '1' || hdr[3] != 'G') return;
        int tblOff = le32(hdr, 12), numTex = le32(hdr, 16);
        if (tblOff < 20 || numTex <= 0 || numTex > 65536) return;
        int needTbl = tblOff + numTex * 4;
        if (needTbl > hdr.length) hdr = inflateIdrkRecord(g1tChunks, Math.min(needTbl + CHUNK_OUT, 1_048_576));
        if (hdr == null || hdr.length < needTbl) return;
        for (int i = 0; i < hits.size(); i++) {
            KssHit h = hits.get(i);
            if (h.texIdx < 0 || h.texIdx >= numTex) continue;
            long subOff = tblOff + (le32(hdr, tblOff + h.texIdx * 4) & 0xffffffffL);
            long nextRel = (h.texIdx + 1 < numTex) ? (le32(hdr, tblOff + (h.texIdx + 1) * 4) & 0xffffffffL) : (g1tDsz - tblOff);
            long subEnd = tblOff + nextRel;
            if (subEnd <= subOff || subEnd - subOff > 16_777_216L) continue;
            byte[] slice = readChunkWindow(g1tChunks, subOff, (int) (subEnd - subOff));
            if (slice != null) outImgs[h.targetIdx] = decodeSubTexture(slice, h);
        }
    }

    static byte[] inflateIdrkRecord(byte[] chunks, int outCap) {
        byte[] out = new byte[outCap];
        int p = 0, outPos = 0;
        for (int c = 0; c < 65536 && p + 10 <= chunks.length && outPos < outCap; c++) {
            int clen = le16(chunks, p);
            p += 10;
            if (clen <= 0 || p + clen > chunks.length) break;
            int got = inflateChunk(chunks, p, clen, out, outPos, outCap - outPos);
            if (got <= 0) break;
            outPos += got;
            p += clen;
        }
        return outPos == out.length ? out : Arrays.copyOf(out, outPos);
    }

    static byte[] readChunkWindow(byte[] chunks, long start, int len) {
        if (start < 0 || len <= 0) return null;
        byte[] out = new byte[len], tmp = new byte[CHUNK_OUT];
        int p = 0;
        long outOff = 0L;
        for (int c = 0; c < 65536 && p + 10 <= chunks.length && outOff < start + len; c++) {
            int clen = le16(chunks, p);
            p += 10;
            if (clen <= 0 || p + clen > chunks.length) break;
            boolean last = (p + clen == chunks.length);
            boolean isZlib = isZlibChunk(chunks, p, clen);
            int chunkSpan = (isZlib && !last) ? CHUNK_OUT : (isZlib ? CHUNK_OUT : clen);
            if (outOff + chunkSpan > start) {
                int got = inflateChunk(chunks, p, clen, tmp, 0, CHUNK_OUT);
                if (got <= 0) break;
                copyWindowOverlap(tmp, outOff, got, out, start, len);
                outOff += got;
            } else {
                outOff += chunkSpan;
            }
            p += clen;
        }
        return out;
    }

    private static void copyWindowOverlap(byte[] src, long srcBase, int srcLen, byte[] dst, long winStart, int winLen) {
        long s = Math.max(srcBase, winStart);
        long e = Math.min(srcBase + srcLen, winStart + winLen);
        if (e > s) System.arraycopy(src, (int) (s - srcBase), dst, (int) (s - winStart), (int) (e - s));
    }

    private static boolean isZlibChunk(byte[] src, int off, int clen) {
        if (clen < 2 || clen >= CHUNK_OUT || off + 2 > src.length) return false;
        int b0 = src[off] & 0xff, b1 = src[off + 1] & 0xff;
        return (b0 & 0x0f) == 8 && (((b0 << 8) | b1) % 31) == 0;
    }

    private static int inflateChunk(byte[] src, int off, int clen, byte[] dst, int dstOff, int maxOut) {
        if (isZlibChunk(src, off, clen)) {
            Inflater inf = new Inflater(false);
            try {
                inf.setInput(src, off, clen);
                int total = 0;
                for (int i = 0; i < 8 && !inf.finished() && total < maxOut; i++) {
                    int n = inf.inflate(dst, dstOff + total, maxOut - total);
                    if (n <= 0) break;
                    total += n;
                }
                return total;
            } catch (DataFormatException e) {
                return -1;
            } finally {
                inf.end();
            }
        }
        int n = Math.min(clen, maxOut);
        System.arraycopy(src, off, dst, dstOff, n);
        return n;
    }

    static ArtImage decodeSubTexture(byte[] sub, KssHit hit) {
        if (sub.length < 20) return null;
        int fmt = sub[1] & 0xff;
        int w = 1 << (sub[2] & 0x0f), h = 1 << ((sub[2] >> 4) & 0x0f);
        int hdrSz = (sub[7] & 0xff) != 0 ? (8 + le32(sub, 8)) : 8;
        if (hdrSz < 8 || hdrSz >= sub.length) return null;
        boolean bc1 = (fmt == 0x59 || fmt == 0x5a || fmt == 0x60);
        boolean bc3 = (fmt == 0x5b || fmt == 0x5c || fmt == 0x62);
        if (!bc1 && !bc3) return null;
        boolean swizzled = (fmt == 0x60 || fmt == 0x62);
        int bpb = bc1 ? 8 : 16, bw = Math.max(1, (w + 3) >> 2), bh = Math.max(1, (h + 3) >> 2);
        byte[] lin = swizzled ? deswizzleGob(sub, hdrSz, bw, bh, bpb) : sub;
        int linOff = swizzled ? 0 : hdrSz;
        if (linOff + bw * bh * bpb > lin.length) return null;
        int[] argb = bc3 ? decodeBc3(lin, linOff, w, h) : decodeBc1(lin, linOff, w, h);
        return cropByUv(new ArtImage(w, h, argb), hit);
    }

    private static ArtImage cropByUv(ArtImage img, KssHit hit) {
        int x0 = clamp(Math.round(hit.uMin * img.w), 0, img.w);
        int y0 = clamp(Math.round(hit.vMin * img.h), 0, img.h);
        int x1 = clamp(Math.round(hit.uMax * img.w), x0, img.w);
        int y1 = clamp(Math.round(hit.vMax * img.h), y0, img.h);
        int cw = x1 - x0, ch = y1 - y0;
        if (cw <= 0 || ch <= 0) return null;
        if (x0 == 0 && y0 == 0 && cw == img.w && ch == img.h) return img;
        int[] out = new int[cw * ch];
        for (int y = 0; y < ch; y++) System.arraycopy(img.argb, (y0 + y) * img.w + x0, out, y * cw, cw);
        return new ArtImage(cw, ch, out);
    }

    static byte[] deswizzleGob(byte[] src, int off, int bw, int bh, int bpb) {
        int gxBlocks = 64 / bpb, gobsX = (bw + gxBlocks - 1) / gxBlocks;
        int bhLog = gobHeightLog2(bh), gobH = 1 << bhLog;
        int gobsY = (bh + (gobH * 8) - 1) / (gobH * 8);
        byte[] dst = new byte[bw * bh * bpb];
        for (int by = 0; by < bh; by++) {
            int gy = by >> 3, yb = by & 7;
            int gobRow = gy >> bhLog, gobInBlock = gy & (gobH - 1);
            for (int bx = 0; bx < bw; bx++) {
                int gx = bx / gxBlocks, xByte = (bx % gxBlocks) * bpb;
                int gobIdx = (gobRow * gobsX + gx) * gobH + gobInBlock;
                int inGob = ((xByte & 0x20) << 3) | ((yb & 0x06) << 5) | ((xByte & 0x10) << 1) | ((yb & 0x01) << 4) | (xByte & 0x0f);
                int s = off + gobIdx * 512 + inGob;
                if (s + bpb <= src.length) System.arraycopy(src, s, dst, (by * bw + bx) * bpb, bpb);
            }
        }
        return dst;
    }

    private static int gobHeightLog2(int bh) {
        int l = 0;
        for (int h = 8; l < 4 && h < bh; l++) h <<= 1;
        return l;
    }

    static int[] decodeBc3(byte[] src, int off, int w, int h) {
        int[] out = new int[w * h];
        int bw = Math.max(1, (w + 3) >> 2), bh = Math.max(1, (h + 3) >> 2);
        int[] alphas = new int[8], cols = new int[4];
        for (int by = 0; by < bh; by++) {
            for (int bx = 0; bx < bw; bx++) {
                int p = off + (by * bw + bx) * 16;
                buildAlphaLut(src[p] & 0xff, src[p + 1] & 0xff, alphas);
                buildRgb565Lut(le16(src, p + 8), le16(src, p + 10), false, cols);
                long abits = (le32(src, p + 2) & 0xffffffffL) | ((le16(src, p + 6) & 0xffffL) << 32);
                int cbits = le32(src, p + 12);
                writeBlock(out, w, h, bx << 2, by << 2, alphas, abits, cols, cbits, true);
            }
        }
        return out;
    }

    static int[] decodeBc1(byte[] src, int off, int w, int h) {
        int[] out = new int[w * h];
        int bw = Math.max(1, (w + 3) >> 2), bh = Math.max(1, (h + 3) >> 2);
        int[] cols = new int[4];
        for (int by = 0; by < bh; by++) {
            for (int bx = 0; bx < bw; bx++) {
                int p = off + (by * bw + bx) * 8;
                buildRgb565Lut(le16(src, p), le16(src, p + 2), true, cols);
                writeBlock(out, w, h, bx << 2, by << 2, null, 0L, cols, le32(src, p + 4), false);
            }
        }
        return out;
    }

    private static void buildAlphaLut(int a0, int a1, int[] lut) {
        lut[0] = a0; lut[1] = a1;
        if (a0 > a1) {
            for (int i = 0; i < 6; i++) lut[2 + i] = ((6 - i) * a0 + (1 + i) * a1 + 3) / 7;
        } else {
            for (int i = 0; i < 4; i++) lut[2 + i] = ((4 - i) * a0 + (1 + i) * a1 + 2) / 5;
            lut[6] = 0; lut[7] = 255;
        }
    }

    private static void buildRgb565Lut(int c0, int c1, boolean alphaPunch, int[] lut) {
        int r0 = ((c0 >> 11) & 31) * 255 / 31, g0 = ((c0 >> 5) & 63) * 255 / 63, b0 = (c0 & 31) * 255 / 31;
        int r1 = ((c1 >> 11) & 31) * 255 / 31, g1 = ((c1 >> 5) & 63) * 255 / 63, b1 = (c1 & 31) * 255 / 31;
        lut[0] = 0xFF000000 | (r0 << 16) | (g0 << 8) | b0;
        lut[1] = 0xFF000000 | (r1 << 16) | (g1 << 8) | b1;
        if (!alphaPunch || c0 > c1) {
            lut[2] = 0xFF000000 | (((2 * r0 + r1) / 3) << 16) | (((2 * g0 + g1) / 3) << 8) | ((2 * b0 + b1) / 3);
            lut[3] = 0xFF000000 | (((r0 + 2 * r1) / 3) << 16) | (((g0 + 2 * g1) / 3) << 8) | ((b0 + 2 * b1) / 3);
        } else {
            lut[2] = 0xFF000000 | (((r0 + r1) >> 1) << 16) | (((g0 + g1) >> 1) << 8) | ((b0 + b1) >> 1);
            lut[3] = 0;
        }
    }

    private static void writeBlock(
        int[] out, int w, int h, int x0, int y0, int[] alphas, long abits, int[] cols, int cbits, boolean hasAlpha) {
        for (int py = 0; py < 4 && y0 + py < h; py++) {
            int row = (y0 + py) * w + x0;
            for (int px = 0; px < 4 && x0 + px < w; px++) {
                int idx = (py << 2) + px;
                int rgb = cols[(cbits >>> (idx << 1)) & 3];
                if (hasAlpha) rgb = (rgb & 0x00FFFFFF) | (alphas[(int) ((abits >>> (idx * 3)) & 7L)] << 24);
                out[row + px] = rgb;
            }
        }
    }

    static ArtImage tintSrcIn(ArtImage src, int argbTint) {
        int ta = (argbTint >>> 24) & 0xff, rgb = argbTint & 0x00FFFFFF;
        int[] out = new int[src.w * src.h];
        for (int i = 0; i < out.length; i++) {
            int sa = (src.argb[i] >>> 24) & 0xff;
            int na = (sa * ta + 127) / 255;
            out[i] = (na << 24) | rgb;
        }
        return new ArtImage(src.w, src.h, out);
    }

    static ArtImage alphaOver(ArtImage bg, ArtImage fg) {
        int[] out = new int[bg.w * bg.h];
        for (int i = 0; i < out.length; i++) {
            int b = bg.argb[i], f = fg.argb[i];
            int fa = (f >>> 24) & 0xff, ba = (b >>> 24) & 0xff, inv = 255 - fa;
            int oa = fa + (ba * inv + 127) / 255;
            if (oa <= 0) { out[i] = 0; continue; }
            int bScaled = ba * inv;
            int r = (((f >>> 16) & 0xff) * fa * 255 + ((b >>> 16) & 0xff) * bScaled) / (oa * 255);
            int g = (((f >>> 8) & 0xff) * fa * 255 + ((b >>> 8) & 0xff) * bScaled) / (oa * 255);
            int bl = ((f & 0xff) * fa * 255 + (b & 0xff) * bScaled) / (oa * 255);
            out[i] = (oa << 24) | (clamp(r, 0, 255) << 16) | (clamp(g, 0, 255) << 8) | clamp(bl, 0, 255);
        }
        return new ArtImage(bg.w, bg.h, out);
    }

    static ArtImage scaleBilinear(ArtImage src, int dw, int dh) {
        if (src.w == dw && src.h == dh) return src;
        int[] out = new int[dw * dh];
        for (int y = 0; y < dh; y++) {
            float fy = (y + 0.5f) * src.h / dh - 0.5f;
            int y0 = clamp((int) Math.floor(fy), 0, src.h - 1), y1 = Math.min(y0 + 1, src.h - 1);
            float wy = Math.max(0f, fy - y0);
            for (int x = 0; x < dw; x++) {
                float fx = (x + 0.5f) * src.w / dw - 0.5f;
                int x0 = clamp((int) Math.floor(fx), 0, src.w - 1), x1 = Math.min(x0 + 1, src.w - 1);
                out[y * dw + x] = bilerpPremul(src.argb[y0 * src.w + x0], src.argb[y0 * src.w + x1],
                        src.argb[y1 * src.w + x0], src.argb[y1 * src.w + x1], Math.max(0f, fx - x0), wy);
            }
        }
        return new ArtImage(dw, dh, out);
    }

    private static int bilerpPremul(int c00, int c10, int c01, int c11, float wx, float wy) {
        float w00 = (1f - wx) * (1f - wy), w10 = wx * (1f - wy), w01 = (1f - wx) * wy, w11 = wx * wy;
        float a = ((c00 >>> 24) & 0xff) * w00 + ((c10 >>> 24) & 0xff) * w10
                + ((c01 >>> 24) & 0xff) * w01 + ((c11 >>> 24) & 0xff) * w11;
        if (a < 0.5f) return 0;
        float r = premulCh(c00, 16) * w00 + premulCh(c10, 16) * w10 + premulCh(c01, 16) * w01 + premulCh(c11, 16) * w11;
        float g = premulCh(c00, 8) * w00 + premulCh(c10, 8) * w10 + premulCh(c01, 8) * w01 + premulCh(c11, 8) * w11;
        float b = premulCh(c00, 0) * w00 + premulCh(c10, 0) * w10 + premulCh(c01, 0) * w01 + premulCh(c11, 0) * w11;
        int ia = clamp(Math.round(a), 0, 255);
        return (ia << 24) | (clamp(Math.round(r / a), 0, 255) << 16)
                | (clamp(Math.round(g / a), 0, 255) << 8) | clamp(Math.round(b / a), 0, 255);
    }

    private static float premulCh(int c, int sh) { return ((c >>> sh) & 0xff) * (((c >>> 24) & 0xff)); }

    static boolean writePng(File dst, ArtImage img) {
        byte[] raw = packPngScanlines(img);
        byte[] idat = deflateBytes(raw);
        File tmp = new File(dst.getParentFile(), dst.getName() + ".tmp");
        try (FileOutputStream out = new FileOutputStream(tmp)) {
            out.write(new byte[] {(byte) 137, 80, 78, 71, 13, 10, 26, 10});
            byte[] ihdr = new byte[13];
            be32(ihdr, 0, img.w); be32(ihdr, 4, img.h);
            ihdr[8] = 8; ihdr[9] = 6;
            writePngChunk(out, "IHDR", ihdr);
            writePngChunk(out, "IDAT", idat);
            writePngChunk(out, "IEND", new byte[0]);
            out.flush();
            return tmp.renameTo(dst);
        } catch (IOException e) {
            tmp.delete();
            return false;
        }
    }

    private static byte[] packPngScanlines(ArtImage img) {
        int stride = 1 + img.w * 4;
        byte[] raw = new byte[stride * img.h];
        for (int y = 0; y < img.h; y++) {
            int dstRow = y * stride, srcRow = y * img.w;
            for (int x = 0; x < img.w; x++) {
                int c = img.argb[srcRow + x], d = dstRow + 1 + (x << 2);
                raw[d] = (byte) (c >>> 16); raw[d + 1] = (byte) (c >>> 8);
                raw[d + 2] = (byte) c; raw[d + 3] = (byte) (c >>> 24);
            }
        }
        return raw;
    }

    private static byte[] deflateBytes(byte[] raw) {
        Deflater def = new Deflater(Deflater.DEFAULT_COMPRESSION);
        byte[] buf = new byte[raw.length + 64 + (raw.length >> 8)];
        try {
            def.setInput(raw);
            def.finish();
            int len = 0;
            for (int i = 0; i < 16 && !def.finished(); i++) {
                int n = def.deflate(buf, len, buf.length - len);
                if (n <= 0) break;
                len += n;
            }
            return Arrays.copyOf(buf, len);
        } finally {
            def.end();
        }
    }

    private static void writePngChunk(FileOutputStream out, String tag, byte[] data) throws IOException {
        byte[] hdr = new byte[8];
        be32(hdr, 0, data.length);
        byte[] t = tag.getBytes(StandardCharsets.US_ASCII);
        System.arraycopy(t, 0, hdr, 4, 4);
        CRC32 crc = new CRC32();
        crc.update(t);
        crc.update(data);
        byte[] tail = new byte[4];
        be32(tail, 0, (int) crc.getValue());
        out.write(hdr);
        if (data.length > 0) out.write(data);
        out.write(tail);
    }

    private static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
    private static int le16(byte[] b, int o) { return (b[o] & 0xff) | ((b[o + 1] & 0xff) << 8); }
    private static int le32(byte[] b, int o) { return (b[o] & 0xff) | ((b[o + 1] & 0xff) << 8) | ((b[o + 2] & 0xff) << 16) | ((b[o + 3] & 0xff) << 24); }
    private static long le64(byte[] b, int o) { return (le32(b, o) & 0xffffffffL) | ((le32(b, o + 4) & 0xffffffffL) << 32); }
    private static float leF32(byte[] b, int o) { return Float.intBitsToFloat(le32(b, o)); }
    private static void be32(byte[] b, int o, int v) {
        b[o] = (byte) (v >>> 24); b[o + 1] = (byte) (v >>> 16); b[o + 2] = (byte) (v >>> 8); b[o + 3] = (byte) v;
    }
}
