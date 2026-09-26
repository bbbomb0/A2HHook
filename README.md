<p align="center">
  <img src="webui.png" alt="A2HHook" width="120">
</p>

<h1 align="center">A2HHook</h1>

<p align="center">
  <b>让 HyperOS 的音乐触感按你的规则工作</b><br>
  <span>面向 REDMI K80 Ultra / K80U 的全局、白名单与后台音乐触感模块</span>
</p>

<p align="center">
  <a href="https://github.com/bbbomb0/A2HHook/releases/latest"><img src="https://img.shields.io/github/v/release/bbbomb0/A2HHook?display_name=tag&label=release" alt="Release"></a>
  <a href="https://github.com/bbbomb0/A2HHook/stargazers"><img src="https://img.shields.io/github/stars/bbbomb0/A2HHook?style=flat&label=stars" alt="Stars"></a>
  <a href="https://github.com/bbbomb0/A2HHook/issues"><img src="https://img.shields.io/github/issues/bbbomb0/A2HHook" alt="Issues"></a>
  <img src="https://img.shields.io/badge/KernelSU%20%2F%20Magisk-compatible-3DDC84" alt="KernelSU and Magisk">
  <img src="https://img.shields.io/badge/version-v1.5.9.5-informational" alt="Version v1.5.9.5">
  <img src="https://img.shields.io/badge/license-GPL--3.0--or--later-blue" alt="GPL-3.0-or-later">
</p>

<p align="center">
  <a href="./README.md">简体中文</a> ·
  <a href="https://github.com/bbbomb0/A2HHook/releases/latest">下载最新版本</a> ·
  <a href="https://github.com/bbbomb0/A2HHook/issues">提交问题</a>
</p>

---

## 目录

- [简介](#简介)
- [功能](#功能)
- [默认白名单](#默认白名单)
- [安装](#安装)
- [使用说明](#使用说明)
- [兼容性](#兼容性)
- [从 v1.5.9 到 v1.5.9.5](#从-v159-到-v1595)
- [发布文件与校验](#发布文件与校验)
- [故障排查](#故障排查)
- [反馈](#反馈)
- [开发与验证](#开发与验证)
- [许可证](#许可证)

## 简介

A2HHook 是一个面向 **REDMI K80 Ultra（K80U）** 的 Root 音乐触感模块。它在系统音频 HAL 层识别音乐、视频、游戏和系统短音的生命周期，根据全局模式或 10 槽白名单决定何时启用 A2H 音乐触感。

模块提供 KernelSU、ReKernelSU、ReSukiSU 管理器使用的根目录安装流程，也在发布 ZIP 中提供 Magisk v20.4+ recovery 安装入口。模块 WebUI 与伴生 APK 共用同一套离线页面，不依赖网络服务、CDN 或额外后台守护进程。

> 当前公开版本：**v1.5.9.5**（`versionCode=1595`）

> [!IMPORTANT]
> A2HHook 会修改目标设备音频 HAL 的运行时行为。请只在自己能够恢复或救砖的设备上使用，并在刷入前保留模块禁用方式和重要数据备份。
>
> [!WARNING]
> 本项目不是通用 Android 音频增强器。当前公开目标是 REDMI K80 Ultra / K80U；未列出的设备、深度修改过的 ROM、修改过的音频 HAL，以及与其他音频注入模块叠加的场景，都不能视为已验证兼容。

## 功能

<details>
<summary><b>全局模式与白名单</b></summary>

- **全局模式**：对所有符合安全条件的普通应用启用音乐触感。
- **白名单模式**：只匹配已启用的包名槽位。
- 内置 6 个官方音乐应用包名，另有 4 个自定义槽位，共 10 槽。
- 每个槽位独立开关；关闭槽位时保留包名，重新开启即可恢复。
- WebUI、控制中心磁贴和 `config/packages.txt` 均可修改配置。
- 配置提交使用原子事务、锁所有权和失败回滚，避免半写入状态。

</details>

<details>
<summary><b>后台音乐触感</b></summary>

- 选项名称为 **后台音乐触感**，默认关闭，默认行为遵循小米官方的跨应用暂停策略。
- 开启后，后台白名单音乐与前台游戏、视频、浏览器、设置、桌面或普通应用同时使用扬声器时，仍可保持音乐触感。
- 判定依据是活动应用、音轨生命周期和当前全局/白名单规则，不硬编码前台游戏包名。
- 同一应用的多条扬声器流不会被误判为不同应用并发。
- 锁屏、提示音、视频焦点变化和游戏进出场由事件驱动的 handoff/concurrent-latch 逻辑承接。

</details>

<details>
<summary><b>游戏与低延迟音频</b></summary>

- 识别 `FAST`、`DEEP_BUFFER`、`COMPRESS_OFFLOAD` 和 `AUDIO_OUTPUT_FLAG_SPATIALIZER` 等合法媒体/游戏输出。
- 对部分不会向 HAL 下发 `appname` 的低延迟游戏，结合厂商 playback 事件与 AudioPolicy 输出生命周期补齐应用登记。
- 通过真实应用 UID、`portId` 和 `session` 管理登记流，停止最后一条应用音轨后释放对应租约。
- 不猜测前台应用、不硬编码游戏包名、不使用周期性 `dumpsys` 或 `ptrace` 维持功能。

</details>

<details>
<summary><b>WebUI 与控制中心</b></summary>

- 离线 Miuix / HyperOS 设置页风格，伴生 APK 和 Root 管理器复用同一份页面。
- 全局模式切换时，应用白名单使用收起与拉起动画。
- “关于”“支持我们”等底部页面支持单指下拉、速度/距离阈值关闭、回弹和返回栈清理。
- 支持跟随系统、浅色、深色三种主题；减少动态效果时自动停止装饰动画。
- 两枚控制中心磁贴：
  - **A2H 全局音乐触感**：切换全局模式与白名单模式；
  - **后台音乐触感**：切换跨应用保持策略。
- 磁贴点击先反馈预测状态，再提交配置并读回确认；失败时自动恢复图标和状态。

</details>

<details>
<summary><b>日志、升级与安全边界</b></summary>

- WebUI 提供“日志记录”开关，可停止或恢复 `a2h_patch.log` 与 `action.log` 的持久写入。
- 稳态 watcher 使用事件唤醒和有界健康探测，不在稳定状态下周期启动 native 检查。
- 模块更新时不会预先卸载伴生 APK；相同签名的覆盖安装保留应用数据和已有 Root 授权。
- 只有正式卸载模块时才清理伴生 APK 与模块固定运行时文件。
- native 写入保留双写、I-cache 同步、完整校验和失败回滚。

</details>

## 默认白名单

| 槽位 | 包名 | 默认状态 |
| :---: | :--- | :---: |
| 1 | `cn.kuwo.player` | 开启 |
| 2 | `com.miui.player` | 开启 |
| 3 | `com.luna.music` | 开启 |
| 4 | `com.tencent.qqmusic` | 开启 |
| 5 | `com.netease.cloudmusic` | 开启 |
| 6 | `com.kugou.android` | 开启 |
| 7–10 | 自定义包名 | 关闭 |

第 7–10 槽可以填写任意需要匹配的普通应用包名。包名必须是 Android 实际包名，不能填写应用名称或带空格的显示文本。

## 安装

### 安装前

1. 解锁 Bootloader，并确认设备已经 Root。
2. 关闭会同时修改同一音频 HAL 或 A2H 策略的同类模块。
3. 备份当前模块配置；升级会保留正常的白名单和策略文件，但仍建议保留回滚方案。

### KernelSU / ReKernelSU / ReSukiSU

1. 从 [GitHub Releases](https://github.com/bbbomb0/A2HHook/releases/latest) 下载 `a2h_hook_v1.5.9.5.zip`。
2. 在 Root 管理器的模块页面选择 ZIP 安装。
3. 重启设备。
4. 打开模块 WebUI，确认 Root 授权和配置读取正常。
5. 根据需要选择全局模式、白名单槽位和后台音乐触感。

### Magisk

发布 ZIP 包含 `META-INF/com/google/android/update-binary` 和 `#MAGISK` updater 标记，支持 Magisk v20.4 及以上 recovery 安装入口。通过 Magisk 模块页面安装时，按 Magisk 的提示完成安装并重启设备。

KernelSU 系列管理器仍使用 ZIP 根目录的 `module.prop`、`customize.sh` 和 service 脚本，不需要额外转换 ZIP。

### 伴生 APK 与 Root 授权

伴生包名为 `io.github.bbbomb0.a2hhook`。首次从磁贴长按进入或首次读取配置时，Root 管理器可能显示授权请求。请允许该请求后重新打开伴生页面。

覆盖升级使用保留数据的安装方式；签名不匹配时会保留旧 APK 并报告失败，不会先卸载旧版本。正式卸载模块时，`uninstall.sh` 才会执行伴生 APK 的清理。

## 使用说明

### WebUI

- **全局模式开启**：所有符合安全边界的普通应用都可触发 A2H。
- **全局模式关闭**：只有已启用槽位中的包名可触发 A2H。
- **后台音乐触感开启**：后台命中的白名单音乐可在跨应用播放时继续触感。
- **日志记录关闭**：停止持久写入详细日志，不会停用模块补丁或配置应用。
- **强劲震感**：只控制 WebUI、按钮和磁贴操作的触感反馈，不改变音频判定。

### 直接编辑配置

Root 文件管理器可以编辑模块目录中的 `config/packages.txt`。每行一个包名，最多 10 行；前 6 行对应官方槽位，后 4 行对应自定义槽位。文件修改完成并保持稳定后，watcher 会自动规范化并热更新。

推荐优先使用 WebUI 修改配置。直接编辑时不要同时保存多个临时版本，也不要删除 `config/package_states`、`config/state` 或 `config/game_auto_pause`。

### 控制中心磁贴

将以下磁贴添加到控制中心后，可以快速切换策略：

- **A2H 全局音乐触感**：全局 ↔ 白名单。
- **后台音乐触感**：遵循官方策略 ↔ 后台保持。

长按任一磁贴可打开伴生 APK 内置 WebUI。

## 兼容性

| 项目 | 说明 |
| :--- | :--- |
| 目标设备 | REDMI K80 Ultra / K80U |
| 系统范围 | 运行时按 ELF/语义定位；公开验证覆盖 K80U / HyperOS 4.0.0.7 Beta 与 HyperOS 3.0.302 |
| 设计目标 | 兼容 HyperOS 2.x / 3.x / 4.x 的布局变化；每台设备仍需通过运行时门禁 |
| Root | KernelSU、ReKernelSU、ReSukiSU；发布 ZIP 提供 Magisk v20.4+ recovery 入口 |
| 架构 | ARM64（`arm64-v8a`） |
| 音频实现 | 目标设备需使用可被运行时解析的 `audio.primary.*.so` |
| 适配结论 | K80U / HyperOS 3.0.302 已完成公开回归；其他 ROM 需在目标设备上完成门禁验证 |

模块不会把未验证的 ROM 宣称为通用兼容。运行时解析遇到符号缺失、锚点缺失、重复命中、函数边界异常或所有权校验失败时，会拒绝写入并保留诊断信息。

## 从 v1.5.9 到 v1.5.9.5

### v1.5.9

- 适配 K80U HyperOS 4.0.0.7 Beta 的运行时 inline path，使用 ELF 符号和语义布局定位，不依赖固定 ROM profile。
- 修复 WebUI 配置读取边界，确保日志开关、10 槽包名和槽位状态可以正确回读。
- 新增运行日志开关，关闭后不再持久写入 `a2h_patch.log` 和 `action.log`。
- 模式切换后对正在播放的媒体流即时重算；保留 AudioPolicy 端口、session、PID/starttime 所有权和单例 watcher。
- 通过事件驱动 handoff、输出池重算和 guarded idle-clear，修复游戏往返、锁屏解锁和 APP 返回时的异常继承。

### v1.5.9.5

- 增加 Magisk v20.4+ recovery 的 `META-INF` 安装入口。
- 全局模式切换增加应用白名单拉起/收回动画；关于等页面支持下拉关闭和回弹清理。
- 模块更新或安装时不预先卸载伴生 APK，尽量保留原有 Root 授权和应用数据。
- 将“游戏时启动后台音乐触感”统一命名为“后台音乐触感”，同步更新磁贴图标、标签、副标题和无障碍描述。
- 修复锁屏、提示音、视频等场景的异常音乐触感。
- 修复官方游戏与其他 APP 往返时的暂停音乐触感及触感回归。
- 修复游戏/媒体 UID 的 AAudio session 原子发布：session 目录使用 `root:<实际 UID>` 和 `1730` 权限。

## 发布文件与校验

最新发布页：[A2HHook v1.5.9.5](https://github.com/bbbomb0/A2HHook/releases/tag/v1.5.9.5)

| 文件 | 用途 |
| :--- | :--- |
| `a2h_hook_v1.5.9.5.zip` | KernelSU / ReKernelSU / ReSukiSU 模块安装包，也包含 Magisk recovery 入口 |
| `companion/a2h_companion.apk` | 伴生 APK，随模块安装或升级处理 |

当前公开 ZIP 的 SHA-256：

```text
2F18AB1C726BD6D819DBD12B301D982B0D10006D5C67D8D350251D4DEA4BABD8
```

安装前可以在 Windows PowerShell 中校验：

```powershell
Get-FileHash .\a2h_hook_v1.5.9.5.zip -Algorithm SHA256
```

## 故障排查

### WebUI 显示“设备配置读取失败”

1. 确认 Root 管理器已向 `io.github.bbbomb0.a2hhook` 授权。
2. 关闭伴生 APK 后重新打开，再点击“重新读取”。
3. 确认模块目录中的 `config/state`、`config/packages.txt` 和 `config/package_states` 存在且可读。
4. 如果是升级后出现，重启一次设备，让 service 重新应用配置。

### 开启后没有音乐触感

1. 先切换到全局模式验证基础链路。
2. 白名单模式下确认包名拼写、槽位开关和实际运行包名一致。
3. 确认音频输出为扬声器；通话、耳机、蓝牙和系统安全门禁不会被绕过。
4. 打开日志记录，重新触发一次音频事件，再查看模块日志。
5. 如果 native 解析失败，不要反复强制写入；保留日志并提交问题。

### 出现异常触感或应用切换后状态不对

1. 确认没有同时启用其他修改音频 HAL 或音乐触感的模块。
2. 先关闭后台音乐触感，重现一次官方策略，再根据需要重新开启。
3. 重启设备后再次测试锁屏、提示音、视频和游戏往返场景。
4. 提交问题时附上设备型号、完整 HyperOS 版本、Root 管理器版本、模块日志和复现步骤。


### 已知限制

- 模块只对扬声器路径和符合安全条件的普通应用音频生效；通话、耳机、蓝牙、振动流和系统保护路径不会被强行接管。
- 不同 ROM 可能裁剪 ELF 符号、改变音频 HAL 布局或替换厂商事件格式。解析失败时模块应拒绝写入；请不要通过关闭校验强行使用。
- Magisk recovery 入口和 KernelSU 系列管理器入口共用同一个发布 ZIP，但具体安装界面和 Root 授权流程由对应管理器决定。
- 后台音乐触感属于可选策略。关闭时遵循小米官方的跨应用暂停行为，开启后才允许命中的后台白名单音乐继续触感。

## 反馈

请通过 [GitHub Issues](https://github.com/bbbomb0/A2HHook/issues) 提交问题，并附上：

1. 设备型号、HyperOS 完整版本和 Android API；
2. KernelSU / ReKernelSU / ReSukiSU / Magisk 版本；
3. A2HHook 版本和当前全局/白名单、后台音乐触感设置；
4. 复现步骤、预期结果和实际结果；
5. 相关日志与截图。

请不要在公开 Issue 中上传包含设备序列号、账号信息或其他个人数据的完整系统转储。

## 开发与验证

本仓库包含 native patcher、watcher、伴生 APK、离线 WebUI 和确定性打包脚本。开发检查可运行：

```powershell
python tests/static_regression.py --repo . --archive .\a2h_hook_v1.5.9.5.zip
python tests/verify_module_zip.py .\a2h_hook_v1.5.9.5.zip `
  --expected-version v1.5.9.5 --expected-code 1595
```

发布 ZIP 使用 `package_module.py` 的固定清单生成，测试文件、设备归档和本地诊断文件不会进入模块包。Android 设备专用检查需要 `--adb` 和已连接的 Root 设备。

## 许可证

本项目以 [GNU General Public License v3.0 或更高版本](./LICENSE) 发布。GPL 授予的既有权利不能被追溯撤销；提交代码、文档或其他可受版权保护的内容前，请确认你有权提交，并接受项目的贡献审查规则。第三方组件和许可证说明见 [THIRD_PARTY_NOTICES.md](./THIRD_PARTY_NOTICES.md)。

## 相关链接

- [最新 Release](https://github.com/bbbomb0/A2HHook/releases/latest)
- [更新日志](./CHANGELOG.md)
- [兼容性资料规范](./compatibility/README.md)
- [问题反馈](https://github.com/bbbomb0/A2HHook/issues)