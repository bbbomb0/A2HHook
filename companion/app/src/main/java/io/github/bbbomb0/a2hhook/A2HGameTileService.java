package io.github.bbbomb0.a2hhook;

public final class A2HGameTileService extends A2HToggleTileService {
    private static final String STATE_PATH = "/data/adb/modules/a2h_hook/config/game_auto_pause";

    @Override
    protected String statePath() {
        return STATE_PATH;
    }

    @Override
    protected String toggleCommand() {
        return "/data/adb/modules/a2h_hook/bin/a2h_apply toggle-background-music-fast";
    }

    @Override
    protected String tileLabel() {
        return "后台音乐触感";
    }

    @Override
    protected String activeSubtitle() {
        return "切换应用时保持";
    }

    @Override
    protected String inactiveSubtitle() {
        return "按官方策略";
    }

    @Override
    protected String pendingSubtitle() {
        return "应用中";
    }

    @Override
    protected String initialState() {
        return "enabled";
    }

    @Override
    protected boolean isActiveState(String state) {
        return "disabled".equals(state);
    }

    @Override
    protected int activeIcon() {
        return drawableId("ic_a2h_tile_background_on");
    }

    @Override
    protected int inactiveIcon() {
        return drawableId("ic_a2h_tile_background_off");
    }
}
