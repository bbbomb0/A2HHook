# A2HHook

A2HHook 是面向 REDMI K80 Ultra / K80U 的音乐触感模块，目标是在 KernelSU / ReKernelSU / ReSukiSU 环境下提供：

- 全局音乐触感；
- 官方 6 个音乐应用白名单；
- 额外 4 个自定义包名槽位，共 10 槽；
- 10 个槽位分别启用或关闭，关闭时保留已填写包名；
- 单一“全局模式”开关：开启为全局，关闭即为白名单模式；
- 手机重启后自动应用配置，并通过通知中心反馈结果；
- 模块栏 WebUI 入口；
- 文件管理器直接修改 `config/packages.txt` 后自动热更新。

当前版本：`v1.5.5-fix3`。`v1.5.5-fix` 与 `v1.5.5-fix2` 继续独立保留，本版是第三代修复迭代。

## 下载安装

请到 GitHub Releases 下载：

- `a2h_hook_v1.5.5-fix3.zip`

刷入方式：

1. 在 KernelSU / ReKernelSU / ReSukiSU 的模块页面安装 ZIP；
2. 重启手机；
3. 进入模块卡片 WebUI；
4. 开启“全局模式”即对所有应用生效；关闭后自动切换为白名单模式；
5. 10 个包名可分别使用右侧开关，模式、包名或开关变化会自动保存并应用。

默认官方白名单：

```text
cn.kuwo.player
com.miui.player
com.luna.music
com.tencent.qqmusic
com.netease.cloudmusic
com.kugou.android
```

第 7～10 槽用于自定义包名。每个槽位都可单独关闭，关闭后包名仍会保留。

也可以使用 Root 文件管理器直接编辑模块目录中的 `config/packages.txt`。后台 watcher 优先使用系统文件事件，连续确认写入稳定后才规范为 10 槽并热更新；监听因原子替换而重建时会短期轮询签名，缺少事件工具时则自动退回 2 秒轮询。新增或改名的有效唯一包名会自动启用，清空槽位会自动关闭。约 30 秒的稳态健康探测只读取 PID 与配置快照，不会启动 native check 或 ptrace；HAL PID、配置或失败重试状态变化时仍会自动应用。

模块在每次开机的 post-fs-data 阶段清理上次开机遗留的 A2HHook pending、锁和临时配置，再由 service 按持久配置重新应用；该清理在系统完成启动后不会执行。

## 兼容性说明

- 已按 HyperOS 2.x / 3.x 差异做通用定位与安全回退，并为 HyperOS 3.0.305 的已提供 HAL 加入严格识别；
- 当前主线保留双写入、I-cache 同步与失败回滚等兼容保险；
- 为避免播放过程中周期性暂停音频 HAL，稳态不会在 PID 与配置均未变化时主动抢回被其他模块覆盖的补丁；遇到冲突请停用同类模块，或切换一次模式/重启后重新应用；
- 已连接实机仅为 K80U / HyperOS 3.0.302。HyperOS 3.0.305、2.0.218 与 2.0.208 仍需对应系统实机回归，不会把静态分析结果写成实机验证；
- 模块不包含系统原厂音频库文件；
- 不建议和其它同类音频 HAL 修改模块同时启用。

如果遇到问题，请从 `/data/adb/modules/a2h_hook/` 取出 `a2h_patch.log` 与 `action.log`，并附上完整系统版本。

## 从源码构建

依赖：

- Android NDK r26 或更新版本；
- CMake 3.18+；
- Python 3（统一负责严格清单打包、CRC 和 Unix 权限校验）。

默认构建和发布链只生成 native patcher / trigger，不编译或打包 Dobby、Zygisk 注入库和 LD_PRELOAD 包装链。仓库保留的 Dobby 代码与静态库仅供 `A2H_BUILD_LEGACY_INJECTION=ON` 的 legacy/诊断构建使用，不是模块正常运行依赖。

Linux / macOS / MSYS2 Git Bash：

```bash
export ANDROID_NDK_HOME=/path/to/android-ndk
./build.sh ci
```

如果已经完成 native 编译，也可以用跨平台打包器单独生成并校验模块 ZIP：

```text
python package_module.py .
```

构建完成后会生成：

```text
a2h_hook_v1.5.5-fix3.zip
```

## 仓库结构

```text
.
├── src/                 # native 源码
├── webroot/             # KernelSU WebUI
├── config/              # 默认配置
├── bin/a2h_apply        # WebUI/开机服务内部应用入口
├── service.sh           # 开机后台应用逻辑
├── customize.sh         # 安装脚本
├── module.prop          # 模块信息与 WebUI 入口
├── package_module.py    # 规范化 ZIP 清单、CRC 与 Unix 权限
└── build.sh             # 本地/CI 构建脚本
```

构建产物、旧版本 ZIP、手机提取二进制和调试碎片不会进入公开仓库。

## 致谢

- KernelSU / ReKernelSU / ReSukiSU 等 Root 管理器生态；
- Zygisk Next 生态；
- Dobby inline hook framework（仅用于仓库内保留的 legacy/诊断构建，不进入默认发布包）。

## 开源协议

本项目代码以 MIT License 开源。第三方组件说明见 `THIRD_PARTY_NOTICES.md`。

## 免责声明

本项目仅用于学习、研究和个人设备调试。刷入模块存在系统稳定性风险，请自行备份并承担使用后果。
