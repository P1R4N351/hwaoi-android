// SPDX-License-Identifier: MIT
package dev.nxnative.android;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.List;

/**
 * BYOG runtime asset harvester for the Warriors second-screen companion.
 * Extracts authentic Purah Pad textures from the user's own .nup or unpacked RomFS via
 * {@link WarriorsKtglArt} and caches them under {@code filesDir/companion_art/}.
 */
final class WarriorsAssetHarvester {
    static final String SRC_VER = "KTGL_PAD_V2";
    private static final String TAG = "WarriorsArt";
    private static final String DIR_NAME = "companion_art";
    private static final String SRC_FILE = "source.txt";
    private static final List<String> ALL_FILES = List.of(
        WarriorsKtglArt.FILE_BACKDROP,
        WarriorsKtglArt.FILE_FILTER,
        WarriorsKtglArt.FILE_GLYPH,
        WarriorsKtglArt.FILE_FRIEZE,
        WarriorsKtglArt.FILE_EDGE,
        WarriorsKtglArt.FILE_SIDE,
        "emblem.png",
        "purah_tex.png",
        SRC_FILE
    );

    private volatile Bitmap backdrop, filter, glyph, borderFrieze, borderEdge, borderSide;
    private volatile Bitmap minimap, objective, leftHud;
    private volatile String source = "NONE";
    private volatile boolean scanning, suppressed;

    Bitmap minimap() { return minimap; }
    void setMinimap(Bitmap bmp) { minimap = bmp; }
    Bitmap objective() { return objective; }
    void setObjective(Bitmap bmp) { objective = bmp; }
    Bitmap leftHud() { return leftHud; }
    void setLeftHud(Bitmap bmp) { leftHud = bmp; }
    Bitmap backdrop() { return backdrop; }
    Bitmap filter() { return filter; }
    Bitmap glyph() { return glyph; }
    Bitmap borderFrieze() { return borderFrieze; }
    Bitmap borderEdge() { return borderEdge; }
    Bitmap borderSide() { return borderSide; }
    String source() { return source; }
    boolean isSuppressed() { return suppressed; }
    void setSuppressed(boolean s) { suppressed = s; }

    boolean hasAllPadArt() {
        return backdrop != null && filter != null && glyph != null
                && borderFrieze != null && borderEdge != null && borderSide != null;
    }

    void loadCached(Context ctx) {
        File dir = artDir(ctx);
        File sf = new File(dir, SRC_FILE);
        String ver = sf.isFile() ? readSmallText(sf) : "";
        if (!SRC_VER.equals(ver)) {
            deleteArtFiles(dir);
            clearBitmaps();
            source = "NONE";
            return;
        }
        backdrop = decodeFileSimple(new File(dir, WarriorsKtglArt.FILE_BACKDROP));
        filter = decodeFileSimple(new File(dir, WarriorsKtglArt.FILE_FILTER));
        glyph = decodeFileSimple(new File(dir, WarriorsKtglArt.FILE_GLYPH));
        borderFrieze = decodeFileSimple(new File(dir, WarriorsKtglArt.FILE_FRIEZE));
        borderEdge = decodeFileSimple(new File(dir, WarriorsKtglArt.FILE_EDGE));
        borderSide = decodeFileSimple(new File(dir, WarriorsKtglArt.FILE_SIDE));
        if (hasAllPadArt()) {
            source = SRC_VER;
        } else {
            deleteArtFiles(dir);
            clearBitmaps();
            source = "NONE";
        }
    }

    void harvestFromGameDirsAsync(Context ctx, File[] roots, int gameKind, Runnable onUpdated) {
        suppressed = false;
        if (scanning || roots == null || roots.length == 0) return;
        scanning = true;
        final Context app = ctx.getApplicationContext();
        final Handler main = new Handler(Looper.getMainLooper());
        Thread t = new Thread(() -> runHarvestThread(app, roots, onUpdated, main), "warriors-art-harvest");
        t.setDaemon(true);
        t.start();
    }

    private void runHarvestThread(Context app, File[] roots, Runnable onUpdated, Handler main) {
        try {
            boolean changed = scanRoots(app, roots);
            if (changed && onUpdated != null) main.post(onUpdated);
        } catch (RuntimeException e) {
            Log.w(TAG, "harvest scan failed", e);
        } finally {
            scanning = false;
        }
    }

    void clearCache(Context ctx) {
        suppressed = true;
        deleteArtFiles(artDir(ctx));
        clearBitmaps();
        source = "NONE";
    }

    private boolean scanRoots(Context ctx, File[] roots) {
        WarriorsKtglArt.ArtBundle b = WarriorsKtglArt.extract(roots);
        if (b == null || !b.complete() || suppressed) return false;
        File dir = artDir(ctx);
        if (!WarriorsKtglArt.saveBundlePngs(dir, b)) return false;
        writeSmallText(new File(dir, SRC_FILE), SRC_VER);
        backdrop = toBitmap(b.backdrop);
        filter = toBitmap(b.filter);
        glyph = toBitmap(b.glyph);
        borderFrieze = toBitmap(b.borderFrieze);
        borderEdge = toBitmap(b.borderEdge);
        borderSide = toBitmap(b.borderSide);
        source = SRC_VER;
        return true;
    }

    private void clearBitmaps() {
        backdrop = null;
        filter = null;
        glyph = null;
        borderFrieze = null;
        borderEdge = null;
        borderSide = null;
    }

    private static Bitmap toBitmap(WarriorsKtglArt.ArtImage img) {
        return Bitmap.createBitmap(img.argb, img.w, img.h, Bitmap.Config.ARGB_8888);
    }

    private static void deleteArtFiles(File dir) {
        for (int i = 0; i < ALL_FILES.size(); i++) deleteQuiet(new File(dir, ALL_FILES.get(i)));
    }

    private static Bitmap decodeFileSimple(File f) {
        return (f.isFile() && f.length() > 0) ? BitmapFactory.decodeFile(f.getAbsolutePath()) : null;
    }

    private static File artDir(Context ctx) {
        File d = new File(ctx.getFilesDir(), DIR_NAME);
        if (!d.exists()) d.mkdirs();
        return d;
    }

    private static void writeSmallText(File dst, String val) {
        try (FileOutputStream out = new FileOutputStream(dst)) {
            out.write(val.getBytes(StandardCharsets.UTF_8));
        } catch (IOException ignored) {}
    }

    private static String readSmallText(File f) {
        byte[] buf = new byte[32];
        try (FileInputStream in = new FileInputStream(f)) {
            int n = in.read(buf);
            return n > 0 ? new String(buf, 0, n, StandardCharsets.UTF_8).trim() : "";
        } catch (IOException e) {
            return "";
        }
    }

    private static void deleteQuiet(File f) { if (f.exists()) f.delete(); }
}
