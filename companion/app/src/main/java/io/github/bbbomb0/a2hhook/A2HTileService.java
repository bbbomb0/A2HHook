package io.github.bbbomb0.a2hhook;

import android.os.Handler;
import android.os.Looper;
import android.service.quicksettings.Tile;
import android.service.quicksettings.TileService;
import android.util.Log;

import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;

public final class A2HTileService extends TileService {
    private static final String TAG = "A2HTileService";
    private static final String STATE_PATH = "/data/adb/modules/a2h_hook/config/state";
    private static final ExecutorService EXECUTOR = Executors.newSingleThreadExecutor(r -> {
        Thread thread = new Thread(r, "a2h-tile-root");
        thread.setDaemon(true);
        return thread;
    });
    private final Handler main = new Handler(Looper.getMainLooper());
    private final AtomicBoolean busy = new AtomicBoolean(false);
    private volatile String knownMode = "disabled";

    @Override
    public void onStartListening() {
        super.onStartListening();
        refresh(false);
    }

    @Override
    public void onClick() {
        if (!busy.compareAndSet(false, true)) return;
        setTile("切换中", Tile.STATE_UNAVAILABLE);
        EXECUTOR.execute(() -> {
            RootShell.Result result = RootShell.run(
                    "sh /data/adb/modules/a2h_hook/bin/a2h_apply toggle; rc=$?; printf '\\n__A2H_TILE_MODE__\\n'; cat "
                            + STATE_PATH + " 2>/dev/null; exit $rc", 20000);
            String mode = parseMode(result.stdout);
            if (result.code != 0 || mode == null) {
                Log.w(TAG, "toggle failed rc=" + result.code + " stderr=" + concise(result.stderr));
            }
            main.post(() -> {
                busy.set(false);
                if (result.code == 0 && mode != null) {
                    knownMode = mode;
                    updateTile(mode, null);
                } else {
                    updateTile(knownMode, "应用失败");
                }
            });
        });
    }

    private void refresh(boolean force) {
        if (!force && busy.get()) return;
        EXECUTOR.execute(() -> {
            RootShell.Result result = RootShell.run("cat " + STATE_PATH, 5000);
            String mode = parseMode(result.stdout);
            if (mode == null) {
                Log.w(TAG, "refresh failed rc=" + result.code + " stderr=" + concise(result.stderr));
            }
            main.post(() -> {
                if (mode != null) {
                    knownMode = mode;
                    updateTile(mode, null);
                } else {
                    updateTile(knownMode, "未读取");
                }
            });
        });
    }

    private void setTile(String subtitle, int state) {
        Tile tile = getQsTile();
        if (tile == null) return;
        tile.setLabel("A2H 音乐触感");
        tile.setSubtitle(subtitle);
        tile.setState(state);
        tile.updateTile();
    }

    private void updateTile(String mode, String error) {
        Tile tile = getQsTile();
        if (tile == null) return;
        boolean global = "enabled".equals(mode);
        tile.setLabel("A2H 音乐触感");
        tile.setSubtitle(error != null ? error : (global ? "全局" : "自定义"));
        tile.setState(global ? Tile.STATE_ACTIVE : Tile.STATE_INACTIVE);
        tile.updateTile();
    }

    private static String parseMode(String output) {
        if (output == null) return null;
        int marker = output.lastIndexOf("__A2H_TILE_MODE__");
        String value = marker >= 0 ? output.substring(marker + "__A2H_TILE_MODE__".length()) : output;
        for (String line : value.split("\\R")) {
            String mode = line.trim();
            if ("enabled".equals(mode) || "disabled".equals(mode)) return mode;
        }
        return null;
    }

    private static String concise(String value) {
        if (value == null) return "";
        String singleLine = value.replace('\r', ' ').replace('\n', ' ').trim();
        return singleLine.length() <= 160 ? singleLine : singleLine.substring(0, 160);
    }
}
