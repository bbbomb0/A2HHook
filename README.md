# A2HHook

A2HHook 是面向 REDMI K80 Ultra / K80U 的音乐触感模块，目标是在 KernelSU / ReKernelSU / ReSukiSU 环境下提供：

- 全局音乐触感；
- 官方 6 个音乐应用白名单；
- 额外 4 个自定义包名槽位，共 10 槽；
- 10 个槽位分别启用或关闭，关闭时保留已填写包名；
- 全局模式 / 白名单模式一键切换；
- 手机重启后自动应用配置，并通过通知中心反馈结果；
- 模块栏 WebUI 入口；
- 日志分享、备份与恢复配置等日常维护功能。

当前公开版本：`v1.5.4`。

## 下载安装

请到 GitHub Releases 下载：

- `a2h_hook_v1.5.4.zip`

刷入方式：

1. 在 KernelSU / ReKernelSU / ReSukiSU 的模块页面安装 ZIP；
2. 重启手机；
3. 进入模块卡片 WebUI；
4. 选择“全局模式”或“白名单模式”；
5. 白名单模式下可编辑 10 个包名并分别使用右侧开关；模式、包名或开关变化会自动保存并应用。

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

## 兼容性说明

- 已按 HyperOS 2.x / 3.x 差异做通用定位与回退处理；
- 当前主线保留双写入与 I-cache flush 兼容保险；
- 当前连接实机仅为 K80U / HyperOS 3.0.302；HyperOS 2.0.208 修复依据离线日志与 OS2.0.218 HAL 静态分析，仍需对应系统用户实测反馈；
- 模块不包含系统原厂音频库文件；
- 不建议和其它同类音频 HAL 修改模块同时启用。

如果遇到问题，请优先在 WebUI 底部状态栏右侧使用“分享日志”，并附上 `a2h_patch.log` 与 `action.log`。

## 从源码构建

依赖：

- Android NDK r26 或更新版本；
- CMake 3.18+；
- `zip` 或 Python 3；Windows 使用 `zip` 时还需要 Python 3 规范 ZIP 权限元数据；
- 仓库自带已验证的 Dobby 静态库；若你要替换版本，可自行重新编译。

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
a2h_hook_v1.5.4.zip
```

## 仓库结构

```text
.
├── src/                 # native 源码
├── webroot/             # KernelSU WebUI
├── config/              # 默认配置
├── bin/a2h_apply        # WebUI/开机服务内部应用入口
├── service.sh           # 开机后台应用逻辑
├── share_logs.sh        # 日志打包分享
├── customize.sh         # 安装脚本
├── module.prop          # 模块信息与 WebUI 入口
├── package_module.py    # 规范化 ZIP 清单、CRC 与 Unix 权限
└── build.sh             # 本地/CI 构建脚本
```

构建产物、旧版本 ZIP、手机提取二进制和调试碎片不会进入公开仓库。

## 致谢

- Dobby inline hook framework（仓库内带有已验证的静态库）；
- KernelSU / Zygisk Next 生态。

## 开源协议

本项目代码以 MIT License 开源。第三方组件说明见 `THIRD_PARTY_NOTICES.md`。

## 免责声明

本项目仅用于学习、研究和个人设备调试。刷入模块存在系统稳定性风险，请自行备份并承担使用后果。
