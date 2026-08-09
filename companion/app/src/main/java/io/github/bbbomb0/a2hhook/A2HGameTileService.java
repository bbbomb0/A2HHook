package io.github.bbbomb0.a2hhook;

public final class A2HGameTileService extends A2HToggleTileService {
    private static final String STATE_PATH = "/data/adb/modules/a2h_hook/config/game_auto_pause";

    @Override
    protected String statePath() {
        return STATE_PATH;
    }

    @Override
    protected String toggleCommand() {
        return "/data/adb/modules/a2h_hook/bin/a2h_apply toggle-game-auto-pause-fast";
    }

    @Override
    protected String tileLabel() {
        return "游戏时暂停音乐触感";
    }

    @Override
    protected String activeSubtitle() {
        return "已开启";
    }

    @Override
    protected String inactiveSubtitle() {
        return "已关闭";
    }

    @Override
    protected String pendingSubtitle() {
        return "应用中";
    }

    @Override
    protected int activeIcon() {
        return drawableId("ic_a2h_tile_game_on");
    }

    @Override
    protected int inactiveIcon() {
        return drawableId("ic_a2h_tile_game_off");
    }
}
