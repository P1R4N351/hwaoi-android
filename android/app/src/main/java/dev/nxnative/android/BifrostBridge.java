// SPDX-License-Identifier: MIT
package dev.nxnative.android;

import android.app.Activity;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.Process;
import android.os.SystemClock;
import android.provider.Settings;
import android.util.Log;
import java.util.List;
import java.util.Map;

/**
 * Android broadcast bridge driving AYN Thor Bifrost stick LEDs from {@link Native#companionState(long[])}.
 * Only active for Ocarina of Time ({@code 0400486024DEC000}) when Bifrost is installed and enabled.
 */
final class BifrostBridge {
    private static final String TAG = "OotBifrost";
    private static final String OOT_TITLE_ID = "0400486024DEC000";
    private static final String AOI_TITLE_ID = "04003F40232DE000";
    private static volatile BifrostBridge current;
    static volatile boolean syncScreenBrightness = true;
    static volatile int lastScreenPct = -1;

    static float readScreenBrightness(Context ctx) {
        try {
            int raw = Settings.System.getInt(ctx.getContentResolver(), Settings.System.SCREEN_BRIGHTNESS, -1);
            if (raw < 0) return -1f;
            float denom = raw <= 255 ? 255f : (raw <= 1023 ? 1023f : 4095f);
            float norm = Math.max(0f, Math.min(1f, raw / denom));
            lastScreenPct = Math.round(norm * 100f);
            return norm;
        } catch (RuntimeException e) {
            return -1f;
        }
    }

    private final Context context;
    private final BifrostLooks.Feed feed;
    private final long[] words = new long[CompanionState.FIELDS];
    private final CompanionState waist = new CompanionState();
    private final BifrostLooks.State state = new BifrostLooks.State();
    private HandlerThread thread;
    private volatile Handler worker;
    private volatile boolean running, dead;
    private volatile int generation;
    private String said = "";

    static BifrostBridge create(Activity activity, String gameId) {
        boolean aoi = AOI_TITLE_ID.equals(gameId);
        if (!OOT_TITLE_ID.equals(gameId) && !aoi) return null;
        int mode = Native.bifrostMode();
        if (mode == 0) return null;
        return new BifrostBridge(activity.getApplicationContext(), mode == 2, aoi);
    }

    private BifrostBridge(Context context, boolean classic, boolean aoi) {
        this.context = context;
        this.feed = new BifrostLooks.Feed(classic, aoi);
    }

    void start() {
        if (running || dead) return;
        try {
            if (worker == null) {
                context.getPackageManager().getPackageInfo(BifrostLooks.PACKAGE, 0);
                thread = new HandlerThread("oot-bifrost", Process.THREAD_PRIORITY_BACKGROUND);
                thread.start();
                worker = new Handler(thread.getLooper());
                Log.i(TAG, "Bifrost installed: stick LEDs active" + (feed.classic() ? " (classic)" : ""));
            }
        } catch (PackageManager.NameNotFoundException absent) {
            return;
        } catch (Throwable failed) {
            fail(failed);
            return;
        }
        running = true;
        current = this;
        int token = ++generation;
        Handler queue = worker;
        queue.post(new Runnable() {
            @Override public void run() {
                if (token != generation || dead) return;
                try {
                    boolean have = Native.companionState(words)
                            && waist.set(words)
                            && BifrostLooks.fromState(waist, state);
                    long now = SystemClock.elapsedRealtime();
                    List<BifrostLooks.Command> cmds;
                    String reason;
                    float bri = syncScreenBrightness ? readScreenBrightness(context) : -1f;
                    synchronized (feed) {
                        feed.setScreenBrightness(bri);
                        cmds = feed.poll(now, have ? state : null);
                        reason = feed.muteReason(now);
                    }
                    send(cmds);
                    if (!reason.equals(said)) {
                        said = reason;
                        if (!reason.isEmpty()) Log.i(TAG, reason + "; retrying later");
                    }
                } catch (Throwable failed) {
                    fail(failed);
                    return;
                }
                if (token == generation) queue.postDelayed(this, BifrostLooks.Feed.POLL_MS);
            }
        });
    }

    void stop() {
        if (!running) return;
        running = false;
        int token = ++generation;
        Handler queue = worker;
        if (queue == null) return;
        queue.post(() -> {
            if (token != generation) return;
            try { send(stopped()); } catch (Throwable failed) { fail(failed); }
        });
    }

    void close() {
        running = false;
        ++generation;
        if (current == this) current = null;
        HandlerThread ending = thread;
        thread = null;
        Handler queue = worker;
        worker = null;
        if (ending == null) return;
        if (queue != null) queue.removeCallbacksAndMessages(null);
        ending.quitSafely();
        try { ending.join(200); } catch (InterruptedException ignored) { Thread.currentThread().interrupt(); }
        try { send(stopped()); } catch (Throwable failed) { fail(failed); }
    }

    private List<BifrostLooks.Command> stopped() {
        synchronized (feed) { return feed.stop(SystemClock.elapsedRealtime()); }
    }

    static void touched() {
        BifrostBridge bridge = current;
        if (bridge == null || !bridge.running || bridge.dead) return;
        Handler queue = bridge.worker;
        int token = bridge.generation;
        if (queue == null) return;
        queue.post(() -> {
            if (token != bridge.generation || bridge.dead) return;
            try {
                List<BifrostLooks.Command> cmds;
                synchronized (bridge.feed) { cmds = bridge.feed.touch(SystemClock.elapsedRealtime()); }
                bridge.send(cmds);
            } catch (Throwable failed) { bridge.fail(failed); }
        });
    }

    private void fail(Throwable failed) {
        if (!dead) Log.w(TAG, "Bifrost bridge disabled for this session", failed);
        dead = true;
        running = false;
    }

    private void send(List<BifrostLooks.Command> commands) {
        for (BifrostLooks.Command command : commands) {
            Intent intent = new Intent(command.action).setClassName(BifrostLooks.PACKAGE, BifrostLooks.RECEIVER);
            for (Map.Entry<String, Object> extra : command.extras.entrySet()) {
                Object value = extra.getValue();
                if (value instanceof Integer) intent.putExtra(extra.getKey(), (int) (Integer) value);
                else if (value instanceof Long) intent.putExtra(extra.getKey(), (long) (Long) value);
                else if (value instanceof Float) intent.putExtra(extra.getKey(), (float) (Float) value);
                else if (value instanceof Boolean) intent.putExtra(extra.getKey(), (boolean) (Boolean) value);
                else intent.putExtra(extra.getKey(), String.valueOf(value));
            }
            Handler queue = worker;
            if (!command.wantsResult || queue == null) {
                context.sendBroadcast(intent);
                continue;
            }
            context.sendOrderedBroadcast(intent, null, new BroadcastReceiver() {
                @Override public void onReceive(Context c, Intent i) {
                    try {
                        synchronized (feed) { feed.result(SystemClock.elapsedRealtime(), getResultCode()); }
                    } catch (Throwable ignored) {}
                }
            }, queue, BifrostLooks.RESULT_UNHANDLED, null, null);
        }
    }
}
