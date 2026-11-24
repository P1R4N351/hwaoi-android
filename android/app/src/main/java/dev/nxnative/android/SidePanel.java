package dev.nxnative.android;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.res.ColorStateList;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.CompoundButton;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;

/**
 * In-game Quick Menu for Age of Imprisonment (Hyrule Warriors: Age of Imprisonment).
 * Styled in obsidian charcoal basalt, Secret Stone amber-gold, and Zonai jade-teal, with bypassed
 * multi-game / non-AoI controls (FPS caps, DLSS stand-in, render_cfg, DRS, Cheats, Live options) ablated.
 */
final class SidePanel extends FrameLayout {
    private static final int SURF_SHEET = 0xF4171614, SURF_HEADER = 0xFF211F1B, SURF_CARD = 0xFF24211C;
    private static final int STROKE_GOLD = 0x55D99B26, STROKE_STONE = 0xFF3A352C, RULE_GOLD = 0x26E8B84B;
    private static final int FOCUS = 0xFFE8B84B, FOCUS_FILL = 0x26E8B84B, ON_GOLD = 0xFF171614;
    private static final int TEAL = 0xFF5AC8B8, TEAL_PILL = 0x225AC8B8;
    private static final int TEXT_PRI = 0xFFF3EFE6, TEXT_SEC = 0xFFB5ADA0;
    private static final int DANGER_BG = 0x2EE05A47, DANGER_STROKE = 0x88E05A47, DANGER_TEXT = 0xFFFFB4A8;

    private final Activity act;
    private final LinearLayout box, tabs;
    private final View sheet;
    private final ScrollView scroll;
    private final Rows rows;
    private TextView headerFps, fpsNow, lsfgNow, layerNow;
    private boolean open;
    private final Runnable tick = this::refresh;

    static final String[] TAB_NAMES = {"Performance", "Display", "Controls", "General"};
    private static final int TAB_PERF = 0, TAB_DISPLAY = 1, TAB_CONTROLS = 2, TAB_GENERAL = 3;
    private int tab = TAB_PERF;

    private int confirm = KeyEvent.KEYCODE_BUTTON_B, back = KeyEvent.KEYCODE_BUTTON_A;
    private int heldDir = PadPanel.NONE, stickHeld = PadPanel.NONE, repeats;
    private final Runnable repeat = this::repeatTick;
    private View lit;
    Runnable onOpen;

    interface Rows {
        void controls(LinearLayout box);
        void position(LinearLayout box);
    }

    private static final int ID_SIZE = 5000, ID_TEXT = 5100, ID_FIT = 5200, ID_MAP = 5300,
            ID_ONE = 5400, ID_LABEL = 5450, ID_DRIVER = 5600, ID_XBOX = 5610, ID_PAD_OPEN = 5620;

    SidePanel(Activity a, Rows rows) {
        super(a);
        act = a;
        this.rows = rows;
        float dp = getResources().getDisplayMetrics().density;
        View scrim = new View(a);
        scrim.setBackgroundColor(0x73000000);
        scrim.setOnClickListener(v -> close());
        addView(scrim, new LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT));

        box = new LinearLayout(a);
        box.setOrientation(LinearLayout.VERTICAL);
        int p = Math.round(16 * dp);
        box.setPadding(p, Math.round(12 * dp), p, p);
        scroll = new ScrollView(a);
        scroll.setVerticalScrollBarEnabled(false);
        scroll.addView(box);

        LinearLayout sh = new LinearLayout(a);
        sh.setOrientation(LinearLayout.VERTICAL);
        final float rad = 24 * dp;
        GradientDrawable shBg = new GradientDrawable();
        shBg.setColor(SURF_SHEET);
        shBg.setStroke(Math.max(1, Math.round(dp)), STROKE_GOLD);
        shBg.setCornerRadii(new float[]{rad, rad, 0, 0, 0, 0, rad, rad});
        sh.setBackground(shBg);
        sh.setOutlineProvider(new android.view.ViewOutlineProvider() {
            @Override public void getOutline(View v, android.graphics.Outline o) {
                o.setRoundRect(0, 0, v.getWidth() + Math.round(rad), v.getHeight(), rad);
            }
        });
        sh.setClipToOutline(true);

        sh.addView(buildHeaderBar(a, dp), new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));
        tabs = buildTabsContainer(a, dp);
        sh.addView(tabs, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));
        View rule = new View(a);
        rule.setBackgroundColor(RULE_GOLD);
        sh.addView(rule, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, Math.max(1, Math.round(dp))));
        sh.addView(scroll, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));

        addView(sh, new LayoutParams(PanelLook.widthPx(Prefs.panelSize(a), dp, a.getResources().getDisplayMetrics().widthPixels), LayoutParams.MATCH_PARENT, Gravity.END));
        sheet = sh;
        getViewTreeObserver().addOnGlobalFocusChangeListener(this::focusMoved);
        setVisibility(GONE);
    }

    private LinearLayout buildHeaderBar(Activity a, float dp) {
        LinearLayout bar = new LinearLayout(a);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        bar.setBackgroundColor(SURF_HEADER);
        bar.setPadding(Math.round(14 * dp), Math.round(10 * dp), Math.round(14 * dp), Math.round(4 * dp));

        ImageView crest = new ImageView(a);
        int iconPx = Math.round(22 * dp);
        try {
            crest.setImageDrawable(new android.graphics.drawable.InsetDrawable(a.getDrawable(R.drawable.ic_aoi_foreground), -0.14f));
        } catch (RuntimeException ignored) { /* test context safety */ }
        bar.addView(crest, new LinearLayout.LayoutParams(iconPx, iconPx));

        TextView title = new TextView(a);
        title.setText(Tx.s(R.string.app_name));
        title.setTextColor(FOCUS);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        title.setSingleLine(true);
        title.setEllipsize(android.text.TextUtils.TruncateAt.END);
        LinearLayout.LayoutParams tl = new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
        tl.setMarginStart(Math.round(8 * dp));
        tl.setMarginEnd(Math.round(8 * dp));
        bar.addView(title, tl);

        headerFps = new TextView(a);
        headerFps.setText("--");
        headerFps.setTextColor(TEAL);
        headerFps.setTypeface(Typeface.DEFAULT_BOLD);
        headerFps.setTextSize(TypedValue.COMPLEX_UNIT_SP, 11);
        headerFps.setIncludeFontPadding(false);
        headerFps.setPadding(Math.round(9 * dp), Math.round(3 * dp), Math.round(9 * dp), Math.round(3 * dp));
        GradientDrawable pill = new GradientDrawable();
        pill.setColor(TEAL_PILL);
        pill.setCornerRadius(999 * dp);
        pill.setStroke(Math.max(1, Math.round(dp)), 0x445AC8B8);
        headerFps.setBackground(pill);
        bar.addView(headerFps);
        return bar;
    }

    private LinearLayout buildTabsContainer(Activity a, float dp) {
        LinearLayout t = new LinearLayout(a);
        t.setOrientation(LinearLayout.HORIZONTAL);
        t.setGravity(Gravity.CENTER_VERTICAL);
        int tp = Math.round(6 * dp);
        t.setPadding(tp, Math.round(4 * dp), tp, tp);
        t.setBackgroundColor(SURF_HEADER);
        return t;
    }

    void padButtons(int[] cb) { confirm = cb[0]; back = cb[1]; }
    boolean isOpen() { return open; }
    void toggle() { if (open) close(); else open(); }
    void padToggle() { if (open) close(); else { open(); post(this::ensureFocus); } }

    void open() {
        if (open) return;
        open = true;
        if (onOpen != null) onOpen.run();
        box.removeAllViews();
        build();
        look();
        setVisibility(VISIBLE);
        sheet.setTranslationX(sheet.getLayoutParams().width);
        sheet.animate().translationX(0).setDuration(180).start();
        post(() -> { if (open && !isInTouchMode()) focusFirst(); });
        removeCallbacks(tick);
        refresh();
    }

    void close() {
        if (!open) return;
        open = false;
        heldDir = stickHeld = PadPanel.NONE;
        removeCallbacks(repeat);
        sheet.animate().translationX(sheet.getLayoutParams().width).setDuration(150).withEndAction(() -> { if (!open) setVisibility(GONE); }).start();
    }

    boolean padKey(KeyEvent e) {
        int k = e.getKeyCode();
        if (k == KeyEvent.KEYCODE_BACK) return false;
        boolean press = e.getAction() == KeyEvent.ACTION_DOWN && e.getRepeatCount() == 0, up = e.getAction() == KeyEvent.ACTION_UP;
        int dir = PadPanel.ofKey(k);
        if (dir != PadPanel.NONE) { if (press) press(dir); else if (up) release(dir); return true; }
        if (!press) return true;
        boolean lost = findFocus() == null || isInTouchMode();
        if (k == back) close();
        else if (k == KeyEvent.KEYCODE_BUTTON_L1 || k == KeyEvent.KEYCODE_BUTTON_R1) { if (lost) ensureFocus(); showTab(PadPanel.tab(tab, k == KeyEvent.KEYCODE_BUTTON_L1 ? -1 : 1, TAB_NAMES.length)); }
        else if (k == confirm || k == KeyEvent.KEYCODE_DPAD_CENTER || k == KeyEvent.KEYCODE_ENTER) { if (lost) ensureFocus(); else activate(); }
        return true;
    }

    boolean padMotion(MotionEvent e) {
        if (e.getAction() != MotionEvent.ACTION_MOVE) return true;
        int d = PadPanel.stick(e.getAxisValue(MotionEvent.AXIS_HAT_X), e.getAxisValue(MotionEvent.AXIS_HAT_Y), stickHeld);
        if (d == PadPanel.NONE) d = PadPanel.stick(e.getAxisValue(MotionEvent.AXIS_X), e.getAxisValue(MotionEvent.AXIS_Y), stickHeld);
        if (d != stickHeld) {
            if (stickHeld != PadPanel.NONE) release(stickHeld);
            stickHeld = d;
            if (d != PadPanel.NONE) press(d);
        }
        return true;
    }

    private void press(int dir) {
        if (dir == heldDir) return;
        heldDir = dir;
        repeats = 0;
        move(dir);
        removeCallbacks(repeat);
        postDelayed(repeat, PadPanel.repeatDelay(0));
    }
    private void release(int dir) { if (dir == heldDir) { heldDir = PadPanel.NONE; removeCallbacks(repeat); } }
    private void repeatTick() {
        if (!open || heldDir == PadPanel.NONE) return;
        move(heldDir);
        postDelayed(repeat, PadPanel.repeatDelay(++repeats));
    }

    private void move(int dir) {
        View f = findFocus();
        if (f == null || isInTouchMode()) { ensureFocus(); return; }
        if (dir == PadPanel.LEFT || dir == PadPanel.RIGHT) {
            int code = dir == PadPanel.LEFT ? KeyEvent.KEYCODE_DPAD_LEFT : KeyEvent.KEYCODE_DPAD_RIGHT;
            if (f instanceof SeekBar) { f.onKeyDown(code, new KeyEvent(KeyEvent.ACTION_DOWN, code)); return; }
            if (f instanceof RadioButton && f.getParent() instanceof RadioGroup && stepRadio((RadioGroup) f.getParent(), f, dir == PadPanel.RIGHT ? 1 : -1)) return;
        }
        int fd = dir == PadPanel.UP ? View.FOCUS_UP : dir == PadPanel.DOWN ? View.FOCUS_DOWN : dir == PadPanel.LEFT ? View.FOCUS_LEFT : View.FOCUS_RIGHT;
        View n = f.focusSearch(fd);
        if (n != null && inside(n)) n.requestFocus();
        else if (fd == View.FOCUS_UP || fd == View.FOCUS_DOWN) scroll.smoothScrollBy(0, (fd == View.FOCUS_UP ? -1 : 1) * scroll.getHeight() / 3);
    }

    private boolean stepRadio(RadioGroup g, View f, int delta) {
        boolean[] can = new boolean[g.getChildCount()];
        for (int i = 0; i < can.length; i++) { View c = g.getChildAt(i); can[i] = c instanceof RadioButton && c.isEnabled() && c.getVisibility() == VISIBLE; }
        View checked = g.getCheckedRadioButtonId() != View.NO_ID ? g.findViewById(g.getCheckedRadioButtonId()) : null;
        int cur = g.getOrientation() == LinearLayout.VERTICAL && checked != null ? g.indexOfChild(checked) : g.indexOfChild(f);
        int t = PadPanel.step(can, cur, delta);
        if (t == cur) return g.getOrientation() == LinearLayout.VERTICAL;
        View c = g.getChildAt(t);
        c.requestFocus();
        c.performClick();
        return true;
    }

    private void activate() {
        View f = findFocus();
        if (f != null && !(f instanceof SeekBar)) f.performClick();
    }

    private boolean inside(View v) {
        for (Object p = v; p instanceof View; p = ((View) p).getParent()) if (p == sheet) return true;
        return false;
    }

    private void ensureFocus() {
        if (!open) return;
        View f = findFocus();
        if (f != null && !isInTouchMode()) return;
        View t = tabs.getChildAt(tab);
        if (t != null) t.requestFocusFromTouch();
        focusFirst();
    }

    private void focusFirst() {
        java.util.ArrayList<View> l = box.getFocusables(View.FOCUS_FORWARD);
        View f = l.isEmpty() ? tabs.getChildAt(tab) : l.get(0);
        if (f != null) f.requestFocus();
    }

    private void focusMoved(View old, View now) {
        if (lit != null) { lit.setForeground(null); lit = null; }
        if (now == null || !inside(now) || isInTouchMode()) return;
        float dp = getResources().getDisplayMetrics().density;
        GradientDrawable d = new GradientDrawable();
        d.setColor(FOCUS_FILL);
        d.setStroke(Math.round(2.5f * dp), FOCUS);
        d.setCornerRadius(14 * dp);
        now.setForeground(d);
        lit = now;
    }

    private AlertDialog padDialog(AlertDialog d) {
        d.setOnKeyListener((di, k, e) -> {
            if (k != confirm && k != back) return false;
            if (e.getAction() == KeyEvent.ACTION_DOWN && e.getRepeatCount() == 0) {
                if (k == back) di.cancel();
                else { d.dispatchKeyEvent(new KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_DPAD_CENTER)); d.dispatchKeyEvent(new KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_DPAD_CENTER)); }
            }
            return true;
        });
        Button no = d.getButton(AlertDialog.BUTTON_NEGATIVE);
        if (no != null && !isInTouchMode()) no.requestFocus();
        return d;
    }

    private void tabBar() {
        tabs.removeAllViews();
        float dp = getResources().getDisplayMetrics().density;
        int h = Math.max(Math.round(44 * dp), PanelLook.rowPx(Prefs.panelSize(act), dp) - Math.round(4 * dp)), pad = Math.round(4 * dp);
        for (int i = 0; i < TAB_NAMES.length; i++) {
            int k = i;
            boolean on = i == tab;
            TextView t = new TextView(act);
            t.setText(Tx.arr(R.array.arr_panel_tabs)[i]);
            t.setGravity(Gravity.CENTER);
            t.setMaxLines(1);
            t.setIncludeFontPadding(false);
            t.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
            t.setPadding(pad, 0, pad, 0);
            t.setTypeface(on ? Typeface.DEFAULT_BOLD : Typeface.DEFAULT);
            t.setTextColor(on ? ON_GOLD : TEXT_PRI);
            GradientDrawable pill = new GradientDrawable();
            pill.setColor(on ? FOCUS : 0);
            pill.setCornerRadius(999 * dp);
            t.setBackground(pill);
            t.setSelected(on);
            t.setFocusable(true);
            t.setOnClickListener(v -> showTab(k));
            t.setContentDescription(Tx.s(R.string.game_tab_n_of_m, Tx.arr(R.array.arr_panel_tabs)[i], i + 1, TAB_NAMES.length) + (on ? Tx.s(R.string.game_selected) : ""));
            LinearLayout.LayoutParams tl = new LinearLayout.LayoutParams(0, h, 1f);
            tl.setMarginStart(Math.round(2 * dp)); tl.setMarginEnd(Math.round(2 * dp));
            tabs.addView(t, tl);
        }
        TextView x = new TextView(act);
        x.setText("✕");
        x.setGravity(Gravity.CENTER);
        x.setTextSize(17);
        x.setTextColor(TEXT_SEC);
        x.setFocusable(true);
        x.setOnClickListener(v -> close());
        x.setContentDescription(Tx.s(R.string.game_close_the_panel_back_to));
        tabs.addView(x, new LinearLayout.LayoutParams(h, h));
        tabs.post(this::tabTextSize);
    }

    private void tabTextSize() {
        if (tabs.getChildCount() <= TAB_NAMES.length) return;
        float sp = getResources().getDisplayMetrics().scaledDensity;
        android.graphics.Paint p = new android.graphics.Paint();
        p.setTypeface(Typeface.DEFAULT_BOLD);
        p.setTextSize(15 * sp);
        String[] names = Tx.arr(R.array.arr_panel_tabs);
        float[] w = new float[TAB_NAMES.length];
        float words = 0, room = 0, chrome = 0;
        for (int i = 0; i < TAB_NAMES.length; i++) {
            TextView t = (TextView) tabs.getChildAt(i);
            LinearLayout.LayoutParams lp = (LinearLayout.LayoutParams) t.getLayoutParams();
            w[i] = p.measureText(names[i]);
            words += w[i];
            room += t.getWidth();
            chrome += t.getPaddingLeft() + t.getPaddingRight() + lp.getMarginStart() + lp.getMarginEnd();
        }
        float fit = words > 0 && room > 0 ? 15 * Math.max(0, room - chrome) / words : 15;
        float size = Math.max(9, Math.min(15, (float) Math.floor(fit * 2) / 2));
        for (int i = 0; i < TAB_NAMES.length; i++) {
            TextView t = (TextView) tabs.getChildAt(i);
            t.setTextSize(TypedValue.COMPLEX_UNIT_SP, size);
            t.setEllipsize(android.text.TextUtils.TruncateAt.END);
            LinearLayout.LayoutParams lp = (LinearLayout.LayoutParams) t.getLayoutParams();
            lp.weight = Math.max(1f, w[i]);
            t.setLayoutParams(lp);
        }
    }

    private void showTab(int k) {
        if (!open || k == tab) return;
        tab = k;
        box.removeAllViews();
        build();
        look();
        scroll.scrollTo(0, 0);
        if (!isInTouchMode()) focusFirst();
        refresh();
    }

    private void look() {
        float dp = getResources().getDisplayMetrics().density;
        int size = Prefs.panelSize(act), w = PanelLook.widthPx(size, dp, getResources().getDisplayMetrics().widthPixels);
        if (sheet.getLayoutParams().width != w) { sheet.getLayoutParams().width = w; sheet.requestLayout(); }
        look(box, PanelLook.textScale(Prefs.panelText(act)), PanelLook.rowPx(size, dp));
        themeTree(box);
    }

    private static void look(View v, float text, int row) {
        if (v instanceof TextView) {
            TextView t = (TextView) v;
            if (text != 1f) t.setTextSize(TypedValue.COMPLEX_UNIT_PX, t.getTextSize() * text);
            if (v instanceof Button || v instanceof CompoundButton) t.setMinHeight(row);
        } else if (v instanceof SeekBar) v.setMinimumHeight(row);
        if (v instanceof ViewGroup) for (int i = 0; i < ((ViewGroup) v).getChildCount(); i++) look(((ViewGroup) v).getChildAt(i), text, row);
    }

    /** Apply AoI Zonai colors uniformly across rows created both here and by Lsfg/Overlay/TouchControls */
    private static void themeTree(View v) {
        if (v instanceof CompoundButton) {
            CompoundButton cb = (CompoundButton) v;
            cb.setTextColor(TEXT_PRI);
            ColorStateList tint = new ColorStateList(new int[][]{{android.R.attr.state_checked}, {}}, new int[]{TEAL, TEXT_SEC});
            cb.setButtonTintList(tint);
        } else if (v instanceof SeekBar) {
            SeekBar sb = (SeekBar) v;
            sb.setProgressTintList(ColorStateList.valueOf(TEAL));
            sb.setThumbTintList(ColorStateList.valueOf(TEAL));
            sb.setProgressBackgroundTintList(ColorStateList.valueOf(STROKE_STONE));
        } else if (v instanceof TextView && !(v instanceof Button) && !Boolean.TRUE.equals(v.getTag())) {
            TextView tv = (TextView) v;
            if (tv.getTextSize() <= 13 * v.getResources().getDisplayMetrics().scaledDensity) tv.setTextColor(TEXT_SEC);
            else tv.setTextColor(TEXT_PRI);
        }
        if (v instanceof ViewGroup) {
            ViewGroup g = (ViewGroup) v;
            for (int i = 0; i < g.getChildCount(); i++) themeTree(g.getChildAt(i));
        }
    }

    private void restyle(int focusId) {
        post(() -> {
            if (!open) return;
            int y = scroll.getScrollY();
            box.removeAllViews();
            build();
            look();
            scroll.post(() -> {
                scroll.scrollTo(0, y);
                View f = focusId != 0 ? box.findViewById(focusId) : null;
                if (f != null) f.requestFocus();
            });
            refresh();
        });
    }

    private TextView head(String t) {
        float dp = getResources().getDisplayMetrics().density;
        TextView h = new TextView(act);
        h.setText(t);
        h.setTag(Boolean.TRUE);
        h.setTextColor(FOCUS);
        h.setTypeface(Typeface.DEFAULT_BOLD);
        h.setTextSize(14);
        h.setPadding(0, Math.round(16 * dp), 0, Math.round(4 * dp));
        box.addView(h);
        View rule = new View(act);
        rule.setBackgroundColor(RULE_GOLD);
        LinearLayout.LayoutParams rl = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, Math.max(1, Math.round(dp)));
        rl.bottomMargin = Math.round(4 * dp);
        box.addView(rule, rl);
        return h;
    }

    private TextView note(String t) {
        TextView n = new TextView(act);
        n.setText(t);
        n.setTag(Boolean.TRUE);
        n.setTextSize(12);
        n.setTextColor(TEXT_SEC);
        box.addView(n);
        return n;
    }

    private Button actionButton(String label, boolean danger) {
        float dp = getResources().getDisplayMetrics().density;
        Button b = new Button(act);
        b.setText(label);
        b.setAllCaps(false);
        b.setTextSize(14);
        b.setTypeface(Typeface.DEFAULT_BOLD);
        b.setTextColor(danger ? DANGER_TEXT : TEXT_PRI);
        GradientDrawable bg = new GradientDrawable();
        bg.setColor(danger ? DANGER_BG : SURF_CARD);
        bg.setStroke(Math.max(1, Math.round(dp)), danger ? DANGER_STROKE : STROKE_STONE);
        bg.setCornerRadius(12 * dp);
        b.setBackground(bg);
        b.setStateListAnimator(null);
        b.setPadding(Math.round(14 * dp), Math.round(8 * dp), Math.round(14 * dp), Math.round(8 * dp));
        return b;
    }

    private void addAction(Button b, int topDp) {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        lp.topMargin = Math.round(topDp * getResources().getDisplayMetrics().density);
        box.addView(b, lp);
    }

    private String game() { return String.valueOf(act.getIntent().getStringExtra("title_id")); }

    private void build() {
        tabBar();
        fpsNow = lsfgNow = layerNow = null;
        if (tab == TAB_DISPLAY) { display(); return; }
        if (tab == TAB_CONTROLS) { controls(); return; }
        if (tab == TAB_GENERAL) { general(); return; }
        performance();
    }

    /** Performance: Frame generation (LSFG) + Next start GPU driver & fast cores (bypassed FPS caps, DLSS, DRS, Cheats ablations omitted) */
    private void performance() {
        fpsNow = note(Tx.s(R.string.game_this_game_picks_its_own_2));
        lsfgNow = Lsfg.rows(act, box, "", () -> { Lsfg.push(act); refresh(); });
        head(Tx.s(R.string.game_next_start));
        driverRow();
        CheckBox pin = new CheckBox(act);
        pin.setText(Tx.s(R.string.common_use_fast_cores));
        pin.setChecked(Prefs.pinThreads(act, game()));
        pin.setOnCheckedChangeListener((b, on) -> Prefs.pinThreads(act, game(), on));
        box.addView(pin);
    }

    private void driverRow() {
        GpuDriver.Info cur = GpuDriver.selected(act, game());
        Button b = row(ID_DRIVER, Tx.s(R.string.common_gpu_driver), cur == null ? Tx.s(R.string.common_system) : cur.label());
        b.setOnClickListener(v -> pickDriver());
    }

    private void pickDriver() {
        java.util.List<GpuDriver.Info> inst = GpuDriver.installed(act);
        GpuDriver.Info cur = GpuDriver.selected(act, game());
        String[] labels = new String[inst.size() + 1];
        labels[0] = Tx.s(R.string.game_system_the_phone_s_own);
        int checked = 0;
        for (int i = 0; i < inst.size(); i++) {
            labels[i + 1] = inst.get(i).label();
            if (cur != null && inst.get(i).dir.equals(cur.dir)) checked = i + 1;
        }
        padDialog(new AlertDialog.Builder(act).setTitle(Tx.s(R.string.game_gpu_driver_takes_effect_at))
                .setSingleChoiceItems(labels, checked, (d, w) -> {
                    GpuDriver.choice(act, game(), w == 0 ? GpuDriver.SYSTEM : null);
                    GpuDriver.select(act, w == 0 ? null : inst.get(w - 1));
                    d.dismiss();
                    restyle(ID_DRIVER);
                })
                .setPositiveButton(Tx.s(R.string.game_install), (d, w) -> GpuDriver.dialog(act, GameActivity.PICK_DRIVER))
                .setNegativeButton(Tx.s(R.string.common_cancel), null).show());
    }

    /** Display: Picture fit/size/position, Clean picture toggle, Overlay toggles, and Shader label */
    private void display() {
        picture();
        if (("04003F40232DE000".equals(game()) || "01002B00111A2000".equals(game())) && act instanceof GameActivity) {
            GameActivity ga = (GameActivity) act;
            head(Tx.s(R.string.game_companion_hud));
            CheckBox drawer = new CheckBox(act);
            drawer.setText(Tx.s(R.string.game_companion_drawer));
            drawer.setChecked(ga.companionDrawerOpen());
            drawer.setOnCheckedChangeListener((b, on) -> ga.companionDrawer(on));
            box.addView(drawer);
            CheckBox ovSecond = new CheckBox(act);
            ovSecond.setText(Tx.s(R.string.game_overlay_second_screen));
            ovSecond.setChecked(Prefs.overlayOnSecondScreen(act));
            ovSecond.setOnCheckedChangeListener((b, on) -> Prefs.overlayOnSecondScreen(act, on));
            box.addView(ovSecond);
            CheckBox stickSync = new CheckBox(act);
            stickSync.setText(Tx.s(R.string.game_bifrost_brightness));
            stickSync.setChecked(Prefs.bifrostScreenSync(act));
            stickSync.setOnCheckedChangeListener((b, on) -> {
                Prefs.bifrostScreenSync(act, on);
                BifrostBridge.syncScreenBrightness = on;
            });
            box.addView(stickSync);
        }
        final boolean allHidden = Prefs.clean(act);
        Button clean = actionButton(Tx.s(allHidden ? R.string.game_show_everything : R.string.game_hide_everything_clean_picture), false);
        clean.setOnClickListener(v -> {
            if (act instanceof GameActivity) ((GameActivity) act).clean(!allHidden);
            close();
        });
        addAction(clean, 10);
        head(Tx.s(R.string.settings_overlay));
        Overlay.rows(act, box, null);
    }

    /** Controls: Player 1, on-screen controls, Rumble, Motion sensitivity, Button mapping, Xbox swap & panel combo */
    private void controls() {
        playerOneRow();
        head(Tx.s(R.string.common_on_screen_controls));
        rows.controls(box);
        CheckBox rum = new CheckBox(act);
        rum.setText(Tx.s(R.string.game_rumble_the_game_s_vibration));
        rum.setChecked(Prefs.rumble(act, game()));
        rum.setOnCheckedChangeListener((b, on) -> { if (act instanceof GameActivity) ((GameActivity) act).rumble(on); else Prefs.rumble(act, game(), on); });
        box.addView(rum);

        TextView motionHead = head(Tx.s(R.string.game_motion_sensitivity));
        box.addView(StepSlider.view(act, Tx.s(R.string.game_motion_sensitivity), Prefs.MOTION_NAMES, Prefs.motionSens(act, game()), "Slower", "Faster", motionHead::setText,
                i -> { Prefs.motionSens(act, game(), i); Native.motionSens(Prefs.MOTION_PCT[i] / 100f); }));
        nextStartRows();
    }

    /** Shaders section kept without CheckBox before general() per shader-layer contract */
    private void shaderRows() {
        if (!(act instanceof GameActivity) || !((GameActivity) act).layerShown()) return;
        head(Tx.s(R.string.game_shaders));
        layerNow = note(((GameActivity) act).layerLine());
        note(Tx.s(R.string.game_shaders_the_game_lacks_are_3));
        Button lab = row(ID_LABEL, Tx.s(R.string.game_building_shaders_label), Overlay.labelChoices()[Prefs.layerLabel(act)]);
        lab.setOnClickListener(v -> padDialog(new AlertDialog.Builder(act).setTitle(Tx.s(R.string.game_building_shaders_label))
                .setSingleChoiceItems(Overlay.labelChoices(), Prefs.layerLabel(act), (d, w) -> { Prefs.layerLabel(act, w); d.dismiss(); restyle(ID_LABEL); })
                .setNegativeButton(Tx.s(R.string.common_cancel), null).show()));
    }

    /** General: Panel look, Shaders status, Pause, Setup/Settings, Stop, Back */
    private void general() {
        note(Tx.s(R.string.game_the_game_runs_on_behind));
        panelRows();
        shaderRows();

        Button pause = actionButton(Tx.s(R.string.game_pause_the_game), false);
        pause.setOnClickListener(v -> { close(); if (act instanceof GameActivity) ((GameActivity) act).pause(); });
        addAction(pause, 16);

        CheckBox ret = new CheckBox(act);
        ret.setText(Tx.s(R.string.game_pause_when_the_game_comes));
        ret.setChecked(Prefs.pauseOnReturn(act, game()));
        ret.setOnCheckedChangeListener((b, on) -> Prefs.pauseOnReturn(act, game(), on));
        box.addView(ret);

        Button setup = actionButton(Tx.s(R.string.home_settings), false);
        setup.setOnClickListener(v -> {
            close();
            act.startActivity(new Intent(act, MainActivity.class).putExtra("open_setup", true).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
        });
        addAction(setup, 8);

        Button stop = actionButton(Tx.s(R.string.game_stop_the_game), true);
        stop.setOnClickListener(v -> padDialog(new AlertDialog.Builder(act).setTitle(Tx.s(R.string.game_stop_the_game_2)).setMessage(Tx.s(R.string.game_the_game_ends_unsaved_progress))
                .setPositiveButton(Tx.s(R.string.common_stop), (d, w) -> { Native.note("stopped by the user"); ExitWatch.stopped(); if (act instanceof GameActivity) ((GameActivity) act).backToFrontend(); android.os.Process.killProcess(android.os.Process.myPid()); })
                .setNegativeButton(Tx.s(R.string.game_keep_running), null).show()));
        addAction(stop, 16);

        Button done = actionButton(Tx.s(R.string.game_back_to_the_game), false);
        done.setOnClickListener(v -> close());
        addAction(done, 8);
    }

    private void picture() {
        head(Tx.s(R.string.game_picture));
        GameActivity ga = act instanceof GameActivity ? (GameActivity) act : null;
        boolean live = ga != null && ga.live();
        boolean st = live ? ga.runStretch() : Prefs.stretch(act, game());
        int sz = live ? ga.runPictureSize() : Prefs.pictureSize(act, game());
        RadioGroup fit = new RadioGroup(act);
        fit.setOrientation(RadioGroup.HORIZONTAL);
        String[] names = {Tx.s(R.string.game_fit), Tx.s(R.string.game_stretch)};
        for (int i = 0; i < names.length; i++) { RadioButton b = new RadioButton(act); b.setId(ID_FIT + i); b.setText(names[i]); fit.addView(b); }
        fit.check(ID_FIT + (st ? 1 : 0));
        box.addView(fit);
        TextView sl = new TextView(act);
        sl.setTextColor(TEXT_PRI);
        SeekBar ss = new SeekBar(act);
        ss.setMax(PictureFit.steps());
        ss.setProgress(PictureFit.stepOfSize(sz));
        ss.setKeyProgressIncrement(1);
        Runnable label = () -> sl.setText(Tx.s(R.string.game_size_percent, PictureFit.sizeOfStep(ss.getProgress())) + (ss.getProgress() == PictureFit.steps() ? Tx.s(R.string.game_full) : ""));
        label.run();
        Runnable send = () -> { if (ga != null) ga.picture(fit.getCheckedRadioButtonId() == ID_FIT + 1, PictureFit.sizeOfStep(ss.getProgress())); };
        fit.setOnCheckedChangeListener((g, id) -> send.run());
        ss.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar b, int v, boolean user) { label.run(); send.run(); }
            @Override public void onStartTrackingTouch(SeekBar b) {}
            @Override public void onStopTrackingTouch(SeekBar b) {}
        });
        box.addView(sl);
        box.addView(ss);
        note(live ? Tx.s(R.string.game_fit_keeps_shape_note) : Tx.s(R.string.game_applies_at_the_next_start));
        rows.position(box);
    }

    private void panelRows() {
        head(Tx.s(R.string.game_panel));
        String[] what = {Tx.s(R.string.game_size), Tx.s(R.string.game_text_size)};
        int[] ids = {ID_SIZE, ID_TEXT}, cur = {Prefs.panelSize(act), Prefs.panelText(act)};
        for (int k = 0; k < 2; k++) {
            TextView l = new TextView(act);
            l.setText(what[k]);
            l.setTextColor(TEXT_PRI);
            box.addView(l);
            RadioGroup g = new RadioGroup(act);
            g.setOrientation(RadioGroup.HORIZONTAL);
            for (int i = 0; i < Tx.arr(R.array.arr_panel_look).length; i++) { RadioButton b = new RadioButton(act); b.setId(ids[k] + i); b.setText(Tx.arr(R.array.arr_panel_look)[i]); g.addView(b); }
            g.check(ids[k] + cur[k]);
            int base = ids[k];
            g.setOnCheckedChangeListener((gr, id) -> {
                if (base == ID_SIZE) Prefs.panelSize(act, id - base); else Prefs.panelText(act, id - base);
                restyle(id);
            });
            box.addView(g);
        }
    }

    private void nextStartRows() {
        if (!(act instanceof GameActivity)) return;
        GameActivity ga = (GameActivity) act;
        head(Tx.s(R.string.game_next_start));
        String sum = PadMapUi.label(act, game());
        Button map = row(ID_MAP, Tx.s(R.string.common_button_mapping), sum);
        map.setContentDescription(Tx.s(R.string.game_button_mapping_applies, sum));
        map.setOnClickListener(v -> PadMapUi.forGame(act, ga.game(), () -> restyle(ID_MAP)));

        int xcur = Prefs.xboxButtons(act, game());
        Button xb = row(ID_XBOX, Tx.s(R.string.settings_xbox_button_label_swap), Prefs.xboxLabel(xcur));
        xb.setOnClickListener(v -> padDialog(new AlertDialog.Builder(act).setTitle(Tx.s(R.string.settings_xbox_button_label_swap))
                .setSingleChoiceItems(Tx.arr(R.array.arr_xbox), xcur, (d, w) -> { Prefs.xboxButtons(act, game(), w); d.dismiss(); restyle(ID_XBOX); })
                .setNegativeButton(Tx.s(R.string.common_cancel), null).show()));

        int pcur = Prefs.panelPad(act, game());
        Button pp = row(ID_PAD_OPEN, Tx.s(R.string.settings_controller_in_game_panel), Tx.arr(R.array.arr_pad_open)[pcur]);
        pp.setOnClickListener(v -> padDialog(new AlertDialog.Builder(act).setTitle(Tx.s(R.string.common_open_the_in_game_panel))
                .setSingleChoiceItems(Tx.arr(R.array.arr_pad_open), pcur, (d, w) -> { Prefs.panelPad(act, game(), w); d.dismiss(); restyle(ID_PAD_OPEN); })
                .setNegativeButton(Tx.s(R.string.common_cancel), null).show()));
    }

    private Button row(int id, String label, String value) {
        Button map = new Button(act);
        map.setId(id);
        map.setAllCaps(false);
        TypedValue bg = new TypedValue();
        act.getTheme().resolveAttribute(android.R.attr.selectableItemBackground, bg, true);
        map.setBackgroundResource(bg.resourceId);
        map.setStateListAnimator(null);
        map.setGravity(Gravity.START | Gravity.CENTER_VERTICAL);
        map.setTextColor(TEXT_PRI);
        map.setTextSize(14);
        map.setSingleLine(true);
        map.setEllipsize(android.text.TextUtils.TruncateAt.END);
        map.setMinWidth(0);
        map.setMinimumWidth(0);
        map.setPadding(0, 0, 0, 0);
        android.text.SpannableStringBuilder sb = new android.text.SpannableStringBuilder(label).append("   ");
        int s = sb.length();
        sb.append(value);
        sb.setSpan(new android.text.style.ForegroundColorSpan(TEXT_SEC), s, sb.length(), android.text.Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        map.setText(sb);
        android.graphics.drawable.Drawable chev = act.getDrawable(R.drawable.ic_chevron);
        if (chev != null) { chev = chev.mutate(); chev.setTint(FOCUS); }
        map.setCompoundDrawablesRelativeWithIntrinsicBounds(null, null, chev, null);
        box.addView(map, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));
        return map;
    }

    private void playerOneRow() {
        if (!(act instanceof GameActivity)) return;
        GameActivity ga = (GameActivity) act;
        java.util.List<PadOrder.Pad> pads = ga.gamepads();
        if (pads.isEmpty()) return;
        head(Tx.s(R.string.game_controllers));
        PadOrder.Pad one = ga.playerOne();
        String chosen = Prefs.padFirst(act);
        Button b = row(ID_ONE, Tx.s(R.string.game_player_1), (one != null ? one.name : Tx.s(R.string.game_none_lower)) + (chosen.isEmpty() ? Tx.s(R.string.game_automatic_paren) : ""));
        b.setOnClickListener(v -> {
            String[] names = new String[pads.size() + 1];
            names[0] = Tx.s(R.string.game_automatic);
            int checked = 0;
            for (int i = 0; i < pads.size(); i++) { names[i + 1] = pads.get(i).name; if (!chosen.isEmpty() && chosen.equals(pads.get(i).key)) checked = i + 1; }
            padDialog(new AlertDialog.Builder(act).setTitle(Tx.s(R.string.game_player_1)).setSingleChoiceItems(names, checked, (d, w) -> {
                ga.choosePlayerOne(w == 0 ? "" : pads.get(w - 1).key);
                d.dismiss();
                restyle(ID_ONE);
            }).setNegativeButton(Tx.s(R.string.common_cancel), null).show());
        });
    }

    private void refresh() {
        if (!open) return;
        String[] f = Native.fpsCapStatus().split(" ");
        int cap = f.length > 0 ? parseNum(f[0], -1) : -1;
        int now = f.length > 2 ? parseNum(f[2], 0) : 0;
        if (headerFps != null) headerFps.setText(now > 0 ? Tx.s(R.string.game_n_fps, now) : "--");
        if (layerNow != null && act instanceof GameActivity) layerNow.setText(((GameActivity) act).layerLine());
        if (tab != TAB_PERF) {
            removeCallbacks(tick);
            if (layerNow != null || headerFps != null) postDelayed(tick, 1000);
            return;
        }
        if (fpsNow != null) {
            fpsNow.setText(cap < 0 ? Tx.s(R.string.game_this_build_has_no_frame)
                    : now <= 0 ? Tx.s(R.string.settings_starting)
                    : Tx.s(R.string.game_now_fps_own, now));
        }
        if (lsfgNow != null) {
            String st = Native.frameGenStatus();
            int sp = st.indexOf(' ');
            lsfgNow.setText(Tx.s(R.string.game_now_status, sp > 0 ? st.substring(sp + 1) : st));
        }
        removeCallbacks(tick);
        postDelayed(tick, 500);
    }

    private static int parseNum(String s, int def) {
        try { return Integer.parseInt(s.trim()); } catch (RuntimeException e) { return def; }
    }
}
