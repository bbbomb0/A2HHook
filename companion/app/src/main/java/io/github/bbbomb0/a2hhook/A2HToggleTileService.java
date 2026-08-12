package io.github.bbbomb0.a2hhook;

import android.graphics.drawable.Icon;
import android.os.Handler;
import android.os.Looper;
import android.service.quicksettings.Tile;
import android.service.quicksettings.TileService;
import android.util.Log;

import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;

/** Shared fast-feedback state machine for both A2H quick settings tiles. */
abstract class A2HToggleTileService extends TileService {
    private static final String TAG = "A2HToggleTile";
    private static final ExecutorService EXECUTOR = Executors.newFixedThreadPool(2, r -> {
        Thread thread = new Thread(r, "a2h-tile-root");
        thread.setDaemon(true);
        return thread;
    });

    private final Handler main = new Handler(Looper.getMainLooper());
    private final AtomicBoolean busy = new AtomicBoolean(false);
    private final AtomicBoolean refreshing = new AtomicBoolean(false);
    private volatile String knownState = "disabled";
    private volatile boolean loaded;

    protected abstract String statePath();

    protected abstract String toggleCommand();

    protected abstract String tileLabel();

    protected abstract String activeSubtitle();

    protected abstract String inactiveSubtitle();

    protected abstract String pendingSubtitle();

    protected String initialState() {
        return "disabled";
    }

    protected boolean isActiveState(String state) {
        return "enabled".equals(state);
    }

    protected abstract int activeIcon();

    protected abstract int inactiveIcon();

    @Override
    public void onStartListening() {
        super.onStartListening();
        if (!loaded) knownState = initialState();
        refresh(false);
    }

    @Override
    public void onClick() {
        if (!loaded) {
            knownState = initialState();
            updateTile(knownState, "读取中");
            refresh(true);
            return;
        }
        if (!busy.compareAndSet(false, true)) return;

        String previous = knownState;
        String next = "enabled".equals(previous) ? "disabled" : "enabled";
        knownState = next;
        // Predict the visual state immediately; root/native work remains serialized below.
        updateTile(next, pendingSubtitle());
        EXECUTOR.execute(() -> {
            RootShell.Result result = RootShell.run("sh " + toggleCommand(), 10000);
            String applied = parseState(result.stdout);
            boolean success = result.code == 0 && next.equals(applied);
            if (!success) {
                Log.w(TAG, tileLabel() + " toggle failed rc=" + result.code
                        + " stderr=" + concise(result.stderr));
            }
            main.post(() -> {
                busy.set(false);
                if (success) {
                    knownState = applied;
                    updateTile(applied, null);
                } else {
                    knownState = previous;
                    updateTile(previous, "应用失败");
                }
            });
        });
    }

    private void refresh(boolean force) {
        if ((!force && busy.get()) || !refreshing.compareAndSet(false, true)) return;
        EXECUTOR.execute(() -> {
            RootShell.Result result = RootShell.run("cat " + statePath(), 5000);
            String state = parseState(result.stdout);
            if (state == null) {
                Log.w(TAG, tileLabel() + " refresh failed rc=" + result.code
                        + " stderr=" + concise(result.stderr));
            }
            main.post(() -> {
                refreshing.set(false);
                // A click that started after this read owns the visible state.
                // Its completion path will publish the authoritative readback.
                if (busy.get()) return;
                loaded = true;
                if (state != null) {
                    knownState = state;
                    updateTile(state, null);
                } else {
                    updateTile(knownState, "未读取");
                }
            });
        });
    }

    private void updateTile(String state, String error) {
        Tile tile = getQsTile();
        if (tile == null) return;
        boolean enabled = isActiveState(state);
        tile.setLabel(tileLabel());
        tile.setSubtitle(error != null ? error : (enabled ? activeSubtitle() : inactiveSubtitle()));
        tile.setState(enabled ? Tile.STATE_ACTIVE : Tile.STATE_INACTIVE);
        tile.setIcon(Icon.createWithResource(this, enabled ? activeIcon() : inactiveIcon()));
        tile.updateTile();
    }

    protected final int drawableId(String name) {
        return getResources().getIdentifier(name, "drawable", getPackageName());
    }

    private static String parseState(String output) {
        if (output == null) return null;
        for (String line : output.split("\\R")) {
            String state = line.trim();
            if ("enabled".equals(state) || "disabled".equals(state)) return state;
        }
        return null;
    }

    private static String concise(String value) {
        if (value == null) return "";
        String singleLine = value.replace('\r', ' ').replace('\n', ' ').trim();
        return singleLine.length() <= 160 ? singleLine : singleLine.substring(0, 160);
    }
}
