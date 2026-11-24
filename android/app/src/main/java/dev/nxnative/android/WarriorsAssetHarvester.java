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
import android.os.Looper;
import android.util.Log;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * BYOG runtime asset harvester for the Warriors second-screen companion.
 * Zero game assets ship in the repository or APK: backgrounds, crests, and Purah Pad
 * UI textures are harvested at runtime from the user's installed game files (.nup,
 * pack images, RomFS WebM/EBML attract movies) or live Vulkan UI atlas blits, and
 * cached under filesDir/companion_art/.
 */
final class WarriorsAssetHarvester {
    private static final String TAG = "WarriorsArt";
    private static final String DIR_NAME = "companion_art";
    private static final String BG_FILE = "backdrop.png";
    private static final String EMBLEM_FILE = "emblem.png";
    private static final String PURAH_FILE = "purah_tex.png";
    private static final String SRC_FILE = "source.txt";
    private static final int BG_W = 620, BG_H = 540, EMBLEM_SZ = 192, PURAH_SZ = 256;
    private static final int MAX_FILES_SCAN = 96;

    private volatile Bitmap backdrop;
    private volatile Bitmap emblem;
    private volatile Bitmap purahTex;
    private volatile String source = "NONE";
    private volatile boolean scanning;
    private volatile boolean suppressed;
    private volatile Bitmap minimap;
    private volatile Bitmap objective;

    Bitmap minimap() { return minimap; }
    void setMinimap(Bitmap bmp) { minimap = bmp; }
    Bitmap objective() { return objective; }
    void setObjective(Bitmap bmp) { objective = bmp; }
    Bitmap backdrop() { return backdrop; }
    Bitmap emblem() { return emblem; }
    Bitmap purahTex() { return purahTex; }
    String source() { return source; }
    boolean isSuppressed() { return suppressed; }
    void setSuppressed(boolean s) { suppressed = s; }

    void loadCached(Context ctx) {
        File dir = artDir(ctx);
        backdrop = decodeFileSimple(new File(dir, BG_FILE));
        emblem = decodeFileSimple(new File(dir, EMBLEM_FILE));
        purahTex = decodeFileSimple(new File(dir, PURAH_FILE));
        File sf = new File(dir, SRC_FILE);
        if (sf.isFile()) {
            source = readSmallText(sf);
        } else if (backdrop != null || purahTex != null) {
            source = "CACHE";
        }
    }

    void harvestFromGameDirsAsync(Context ctx, File[] roots, int gameKind, Runnable onUpdated) {
        suppressed = false;
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

    void clearCache(Context ctx) {
        suppressed = true;
        File dir = artDir(ctx);
        deleteQuiet(new File(dir, BG_FILE));
        deleteQuiet(new File(dir, EMBLEM_FILE));
        deleteQuiet(new File(dir, PURAH_FILE));
        deleteQuiet(new File(dir, SRC_FILE));
        backdrop = null;
        emblem = null;
        purahTex = null;
        source = "NONE";
    }

    /** Ingests a one-shot 512x512 RGBA8 UI texture captured via Vulkan blit when not suppressed. */
    boolean ingestGpuUiTex(Context ctx, byte[] rgba, int w, int h, int gameKind) {
        if (suppressed || rgba == null || w < 128 || h < 128 || w * h * 4 > rgba.length) return false;
        if (!hasRichUiDetail(rgba, w, h)) return false;
        Bitmap src = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
        src.copyPixelsFromBuffer(ByteBuffer.wrap(rgba, 0, w * h * 4));
        Bitmap tile = Bitmap.createScaledBitmap(src, PURAH_SZ, PURAH_SZ, true);
        saveBitmap(new File(artDir(ctx), PURAH_FILE), tile);
        purahTex = tile;
        if (backdrop == null || "NUP".equals(source)) {
            Bitmap styled = styleBackdrop(src, gameKind);
            saveBitmap(new File(artDir(ctx), BG_FILE), styled);
            writeSmallText(new File(artDir(ctx), SRC_FILE), "PURAH PAD");
            backdrop = styled;
            source = "PURAH PAD";
        }
        if (emblem == null) {
            Bitmap sq = centerCropSquare(src, EMBLEM_SZ);
            saveBitmap(new File(artDir(ctx), EMBLEM_FILE), sq);
            emblem = sq;
        }
        src.recycle();
        return true;
    }

    private static boolean hasRichUiDetail(byte[] rgba, int w, int h) {
        int stepX = Math.max(1, w / 32), stepY = Math.max(1, h / 32);
        int nonBlack = 0, colored = 0, lumaMin = 255, lumaMax = 0;
        for (int y = 0; y < h; y += stepY) {
            for (int x = 0; x < w; x += stepX) {
                int o = (y * w + x) * 4;
                int r = rgba[o] & 0xFF, g = rgba[o + 1] & 0xFF, b = rgba[o + 2] & 0xFF, a = rgba[o + 3] & 0xFF;
                if (a <= 32) continue;
                int mx = Math.max(r, Math.max(g, b)), mn = Math.min(r, Math.min(g, b));
                int lum = (r * 3 + g * 6 + b) / 10;
                if (r + g + b > 24) nonBlack++;
                if (mx - mn > 22) colored++;
                if (lum < lumaMin) lumaMin = lum;
                if (lum > lumaMax) lumaMax = lum;
            }
        }
        return nonBlack >= 120 && (colored >= 48 || (lumaMax - lumaMin) >= 64);
    }

    private boolean scanRoots(Context ctx, File[] roots, int gameKind) {
        boolean changed = false;
        for (File root : roots) {
            if (root != null && root.isFile() && Nup.extOf(root.getName()) != null) {
                if (extractNupArt(ctx, root, gameKind)) changed = true;
            }
        }
        List<File> images = new ArrayList<>();
        List<File> movies = new ArrayList<>();
        int[] budget = {MAX_FILES_SCAN};
        for (File root : roots) {
            if (root != null && root.exists()) walkBounded(root, 0, budget, images, movies);
        }
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

    private boolean extractNupArt(Context ctx, File nupFile, int gameKind) {
        if (emblem != null && backdrop != null) return false;
        try {
            Nup n = Nup.open(nupFile);
            Nup.Meta meta = n.gameMeta();
            if (meta == null || meta.icon == null || meta.icon.length < 128) return false;
            Bitmap decoded = BitmapFactory.decodeByteArray(meta.icon, 0, meta.icon.length);
            if (decoded == null || decoded.getWidth() < 64 || decoded.getHeight() < 64) return false;
            boolean did = false;
            if (emblem == null) {
                Bitmap sq = centerCropSquare(decoded, EMBLEM_SZ);
                saveBitmap(new File(artDir(ctx), EMBLEM_FILE), sq);
                emblem = sq;
                did = true;
            }
            if (backdrop == null) {
                Bitmap styled = styleBackdrop(decoded, gameKind);
                saveBitmap(new File(artDir(ctx), BG_FILE), styled);
                writeSmallText(new File(artDir(ctx), SRC_FILE), "NUP");
                backdrop = styled;
                source = "NUP";
                did = true;
            }
            decoded.recycle();
            return did;
        } catch (IOException | RuntimeException e) {
            Log.w(TAG, "nup meta harvest failed", e);
            return false;
        }
    }

    private static void walkBounded(File dir, int depth, int[] budget, List<File> imgs, List<File> movs) {
        if (depth > 4 || budget[0] <= 0 || !dir.isDirectory()) return;
        File[] list = dir.listFiles();
        if (list == null) return;
        for (File f : list) {
            if (budget[0] <= 0) return;
            if (f.isDirectory()) {
                walkBounded(f, depth + 1, budget, imgs, movs);
                continue;
            }
            budget[0]--;
            String n = f.getName().toLowerCase(Locale.ROOT);
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

    private static Bitmap decodeFileSimple(File f) {
        return (f.isFile() && f.length() > 0) ? BitmapFactory.decodeFile(f.getAbsolutePath()) : null;
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
        int topCol = gameKind == WarriorsCompanionModel.GAME_AOC ? 0xC4080D11 : 0xC40B0C0E;
        int botCol = gameKind == WarriorsCompanionModel.GAME_AOC ? 0xE604070A : 0xE6050608;
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
