// SPDX-License-Identifier: MIT
package dev.nxnative.android;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.Shader;
import android.media.MediaMetadataRetriever;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.Looper;
import android.os.Process;
import android.util.Log;
import android.view.PixelCopy;
import android.view.SurfaceView;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/**
 * BYOG runtime asset harvester for the Warriors second-screen companion.
 * Zero game assets ship in the repository or APK: backgrounds and crests are
 * harvested at runtime from the user's own installed game files (pack icons,
 * RomFS WebM/EBML attract movies) or captured live from Display 0 via PixelCopy,
 * then cached under app-private filesDir/companion_art/.
 */
final class WarriorsAssetHarvester {
    private static final String TAG = "WarriorsArt";
    private static final String DIR_NAME = "companion_art";
    private static final String BG_FILE = "backdrop.png";
    private static final String EMBLEM_FILE = "emblem.png";
    private static final String SRC_FILE = "source.txt";
    private static final int BG_W = 620, BG_H = 540, EMBLEM_SZ = 192;
    private static final int MAX_FILES_SCAN = 96;

    private volatile Bitmap backdrop;
    private volatile Bitmap emblem;
    private volatile String source = "NONE";
    private volatile boolean scanning;
    private volatile Bitmap minimap;
    private volatile boolean minimapInFlight;
    private HandlerThread copyThread;
    private Handler copyHandler;

    Bitmap minimap() { return minimap; }
    void clearMinimap() { minimap = null; }

    void shutdown() {
        if (copyThread != null) {
            copyThread.quitSafely();
            copyThread = null;
            copyHandler = null;
        }
    }

    /** Captures the target game's own top-right circular minimap from Display 0 without blocking UI. */
    void captureLiveMinimap(SurfaceView surface, int cropMode, Runnable onUpdated) {
        if (minimapInFlight || surface == null || !surface.getHolder().getSurface().isValid()) return;
        int sw = surface.getWidth(), sh = surface.getHeight();
        if (sw < 320 || sh < 240) return;
        if (copyHandler == null) {
            copyThread = new HandlerThread("warriors-minimap-copy", Process.THREAD_PRIORITY_BACKGROUND);
            copyThread.start();
            copyHandler = new Handler(copyThread.getLooper());
        }
        Rect src = minimapSrcRect(sw, sh, cropMode);
        if (src.width() < 32 || src.height() < 32) return;
        minimapInFlight = true;
        final Bitmap dst = Bitmap.createBitmap(384, 384, Bitmap.Config.ARGB_8888);
        final Handler main = new Handler(Looper.getMainLooper());
        try {
            PixelCopy.request(surface, src, dst, result -> {
                minimapInFlight = false;
                if (result == PixelCopy.SUCCESS) {
                    minimap = dst;
                    if (onUpdated != null) main.post(onUpdated);
                } else {
                    dst.recycle();
                }
            }, copyHandler);
        } catch (RuntimeException e) {
            minimapInFlight = false;
            dst.recycle();
        }
    }

    static Rect minimapSrcRect(int sw, int sh, int cropMode) {
        float fx, fy, fw, fh;
        if (cropMode == 0) { fx = 0.79f; fy = 0.045f; fw = 0.185f; fh = 0.33f; }       // TIGHT
        else if (cropMode == 2) { fx = 0.73f; fy = 0.015f; fw = 0.255f; fh = 0.45f; }  // WIDE
        else { fx = 0.77f; fy = 0.03f; fw = 0.21f; fh = 0.37f; }                       // NORMAL
        int l = Math.max(0, Math.round(sw * fx));
        int t = Math.max(0, Math.round(sh * fy));
        int r = Math.min(sw, l + Math.round(sw * fw));
        int b = Math.min(sh, t + Math.round(sh * fh));
        return new Rect(l, t, r, b);
    }

    Bitmap backdrop() { return backdrop; }
    Bitmap emblem() { return emblem; }
    String source() { return source; }

    void loadCached(Context ctx) {
        File dir = artDir(ctx);
        File bg = new File(dir, BG_FILE);
        if (bg.isFile() && bg.length() > 0) {
            Bitmap b = BitmapFactory.decodeFile(bg.getAbsolutePath());
            if (b != null) backdrop = b;
        }
        File em = new File(dir, EMBLEM_FILE);
        if (em.isFile() && em.length() > 0) {
            Bitmap b = BitmapFactory.decodeFile(em.getAbsolutePath());
            if (b != null) emblem = b;
        }
        File sf = new File(dir, SRC_FILE);
        if (sf.isFile()) {
            source = readSmallText(sf);
        } else if (backdrop != null) {
            source = "CACHE";
        }
    }

    void harvestFromGameDirsAsync(Context ctx, File[] roots, int gameKind, Runnable onUpdated) {
        if (scanning || roots == null || roots.length == 0) return;
        scanning = true;
        final Context app = ctx.getApplicationContext();
        final Handler main = new Handler(Looper.getMainLooper());
        Thread t = new Thread(() -> {
            try {
                boolean changed = scanRoots(app, roots, gameKind);
                if (changed && onUpdated != null) main.post(onUpdated);
            } catch (RuntimeException e) {
                Log.w(TAG, "harvest scan failed", e);
            } finally {
                scanning = false;
            }
        }, "warriors-art-harvest");
        t.setDaemon(true);
        t.start();
    }

    void captureLiveBackdrop(Context ctx, SurfaceView surface, int gameKind, Runnable onUpdated) {
        if (surface == null || !surface.getHolder().getSurface().isValid()) return;
        int sw = surface.getWidth(), sh = surface.getHeight();
        if (sw < 128 || sh < 128) return;
        final Bitmap raw = Bitmap.createBitmap(BG_W, BG_H, Bitmap.Config.ARGB_8888);
        final Context app = ctx.getApplicationContext();
        final Handler main = new Handler(Looper.getMainLooper());
        try {
            PixelCopy.request(surface, raw, result -> {
                if (result != PixelCopy.SUCCESS) {
                    raw.recycle();
                    return;
                }
                Bitmap styled = styleBackdrop(raw, gameKind);
                raw.recycle();
                if (styled != null) {
                    saveBitmap(new File(artDir(app), BG_FILE), styled);
                    writeSmallText(new File(artDir(app), SRC_FILE), "LIVE");
                    backdrop = styled;
                    source = "LIVE";
                    if (onUpdated != null) onUpdated.run();
                }
            }, main);
        } catch (RuntimeException e) {
            raw.recycle();
            Log.w(TAG, "PixelCopy capture failed", e);
        }
    }

    void clearCache(Context ctx) {
        File dir = artDir(ctx);
        deleteQuiet(new File(dir, BG_FILE));
        deleteQuiet(new File(dir, EMBLEM_FILE));
        deleteQuiet(new File(dir, SRC_FILE));
        backdrop = null;
        emblem = null;
        source = "NONE";
    }

    private boolean scanRoots(Context ctx, File[] roots, int gameKind) {
        List<File> images = new ArrayList<>();
        List<File> movies = new ArrayList<>();
        int[] budget = {MAX_FILES_SCAN};
        for (File root : roots) {
            if (root != null && root.exists()) walkBounded(root, 0, budget, images, movies);
        }
        boolean changed = false;
        if (emblem == null) {
            for (File f : images) {
                Bitmap b = decodeBounded(f, 512, 512);
                if (b != null && b.getWidth() >= 64 && b.getHeight() >= 64) {
                    Bitmap sq = centerCropSquare(b, EMBLEM_SZ);
                    b.recycle();
                    saveBitmap(new File(artDir(ctx), EMBLEM_FILE), sq);
                    emblem = sq;
                    changed = true;
                    break;
                }
            }
        }
        if (backdrop == null) {
            for (File f : images) {
                Bitmap b = decodeBounded(f, 1280, 1080);
                if (b != null && b.getWidth() >= 256 && b.getHeight() >= 144) {
                    Bitmap styled = styleBackdrop(b, gameKind);
                    b.recycle();
                    saveBitmap(new File(artDir(ctx), BG_FILE), styled);
                    writeSmallText(new File(artDir(ctx), SRC_FILE), "PACK");
                    backdrop = styled;
                    source = "PACK";
                    return true;
                }
            }
            for (File m : movies) {
                Bitmap frame = grabMovieFrame(m);
                if (frame != null) {
                    Bitmap styled = styleBackdrop(frame, gameKind);
                    frame.recycle();
                    saveBitmap(new File(artDir(ctx), BG_FILE), styled);
                    writeSmallText(new File(artDir(ctx), SRC_FILE), "MOVIE");
                    backdrop = styled;
                    source = "MOVIE";
                    return true;
                }
            }
        }
        return changed;
    }

    private static void walkBounded(File dir, int depth, int[] budget, List<File> imgs, List<File> movs) {
        if (depth > 4 || budget[0] <= 0) return;
        File[] list = dir.listFiles();
        if (list == null) return;
        for (File f : list) {
            if (budget[0] <= 0) return;
            if (f.isDirectory()) {
                walkBounded(f, depth + 1, budget, imgs, movs);
                continue;
            }
            budget[0]--;
            String n = f.getName().toLowerCase(java.util.Locale.ROOT);
            if ((n.endsWith(".png") || n.endsWith(".jpg") || n.endsWith(".jpeg") || n.endsWith(".webp"))
                    && f.length() > 2048 && f.length() < 8_388_608L) {
                imgs.add(f);
            } else if ((n.endsWith(".webm") || (n.endsWith(".bin") && isEbml(f)))
                    && f.length() > 65_536L && movs.size() < 8) {
                movs.add(f);
            }
        }
    }

    private static boolean isEbml(File f) {
        byte[] h = new byte[4];
        try (FileInputStream in = new FileInputStream(f)) {
            return in.read(h) == 4 && h[0] == 0x1A && h[1] == 0x45 && h[2] == (byte) 0xDF && h[3] == (byte) 0xA3;
        } catch (IOException e) {
            return false;
        }
    }

    private static Bitmap grabMovieFrame(File f) {
        MediaMetadataRetriever mmr = new MediaMetadataRetriever();
        try {
            mmr.setDataSource(f.getAbsolutePath());
            Bitmap b = mmr.getFrameAtTime(5_000_000L, MediaMetadataRetriever.OPTION_CLOSEST_SYNC);
            if (b == null) b = mmr.getFrameAtTime(1_000_000L, MediaMetadataRetriever.OPTION_CLOSEST_SYNC);
            return b;
        } catch (RuntimeException e) {
            return null;
        } finally {
            try { mmr.release(); } catch (IOException | RuntimeException ignored) {}
        }
    }

    private static Bitmap decodeBounded(File f, int maxW, int maxH) {
        BitmapFactory.Options o = new BitmapFactory.Options();
        o.inJustDecodeBounds = true;
        BitmapFactory.decodeFile(f.getAbsolutePath(), o);
        if (o.outWidth <= 0 || o.outHeight <= 0) return null;
        int sample = 1;
        while ((o.outWidth / sample) > maxW * 2 || (o.outHeight / sample) > maxH * 2) sample <<= 1;
        o.inJustDecodeBounds = false;
        o.inSampleSize = sample;
        return BitmapFactory.decodeFile(f.getAbsolutePath(), o);
    }

    private static Bitmap styleBackdrop(Bitmap src, int gameKind) {
        Bitmap out = Bitmap.createBitmap(BG_W, BG_H, Bitmap.Config.ARGB_8888);
        Canvas c = new Canvas(out);
        float scale = Math.max((float) BG_W / src.getWidth(), (float) BG_H / src.getHeight());
        int dw = Math.round(src.getWidth() * scale);
        int dh = Math.round(src.getHeight() * scale);
        int dx = (BG_W - dw) / 2, dy = (BG_H - dh) / 2;
        Paint p = new Paint(Paint.FILTER_BITMAP_FLAG);
        c.drawBitmap(src, null, new Rect(dx, dy, dx + dw, dy + dh), p);
        int topCol = gameKind == WarriorsCompanionModel.GAME_AOC ? 0xB80E131B : 0xB8141311;
        int botCol = gameKind == WarriorsCompanionModel.GAME_AOC ? 0xD80A0F16 : 0xD8100E0C;
        Paint scrim = new Paint();
        scrim.setShader(new LinearGradient(0, 0, 0, BG_H, topCol, botCol, Shader.TileMode.CLAMP));
        c.drawRect(0, 0, BG_W, BG_H, scrim);
        return out;
    }

    private static Bitmap centerCropSquare(Bitmap src, int sz) {
        int s = Math.min(src.getWidth(), src.getHeight());
        int sx = (src.getWidth() - s) / 2, sy = (src.getHeight() - s) / 2;
        Bitmap out = Bitmap.createBitmap(sz, sz, Bitmap.Config.ARGB_8888);
        Canvas c = new Canvas(out);
        c.drawBitmap(src, new Rect(sx, sy, sx + s, sy + s), new Rect(0, 0, sz, sz), new Paint(Paint.FILTER_BITMAP_FLAG));
        return out;
    }

    private static File artDir(Context ctx) {
        File d = new File(ctx.getFilesDir(), DIR_NAME);
        if (!d.exists()) d.mkdirs();
        return d;
    }

    private static void saveBitmap(File dst, Bitmap bmp) {
        File tmp = new File(dst.getParentFile(), dst.getName() + ".tmp");
        try (FileOutputStream out = new FileOutputStream(tmp)) {
            bmp.compress(Bitmap.CompressFormat.PNG, 90, out);
            out.flush();
            if (!tmp.renameTo(dst)) tmp.delete();
        } catch (IOException e) {
            tmp.delete();
        }
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
            return n > 0 ? new String(buf, 0, n, StandardCharsets.UTF_8).trim() : "CACHE";
        } catch (IOException e) {
            return "CACHE";
        }
    }

    private static void deleteQuiet(File f) { if (f.exists()) f.delete(); }
}
