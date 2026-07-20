# 更新日志

## v1.5.2-fix

- 修复日志分享后 ZIP 长时间不自动删除的问题：watcher 改为合并读取 window / activity / top 状态，不再因只看到无关 window 信息而误判 `no-chooser`。
- 优化 QQ 等应用分享识别：进入目标分享应用后，离开目标界面或达到安全延迟会自动清理日志 ZIP；未选择目标/取消分享仍尽量保留。
- 分享 URI 改为优先使用 ExternalStorageProvider 文档 URI，并保留 MediaStore Downloads URI 作为可见性与回退，降低 QQ 预览阶段“文件不存在”的概率。
- 增加分享 watcher 详细日志：记录 target 命中、清理原因、保留原因和最后一次前台状态，便于继续追踪不同 ROM/QQ 版本差异。

## v1.5.2

- 修复 Zygisk Next 侧 `strcmp` hook 的 A2H 调用窗口硬编码问题，改为读取 `config/func_off` 动态适配不同系统版本。
- 优化第 7～10 自定义白名单槽位在“游戏自动关闭音乐触感”链路中的兼容性。
- 日志分享优先使用 MediaStore Downloads `content://` URI，降低 QQ 提示“文件不存在”的概率。
- 调整分享后删除 watcher：取消/未发送保留压缩包，稳定识别目标应用后再延迟删除，避免过早删除导致接收端读不到文件。
- 分享过程追加 `[share]` 诊断日志到 `a2h_patch.log` / `action.log`。
- 本地构建脚本优先使用 Ninja，避免 Windows 下默认 NMake 缺失导致构建失败。

## v1.5.1

- 保留 v1.5.0 全版本通杀核心逻辑，并继续兼容 HyperOS 2.x / 3.x。
- 保留白名单 stub 与全局 patch 的双写入、双 I-cache flush 兼容保险。
- 新增 `profile-fast` / `hint+profile` 快路径，减少已知版本重复大范围扫描。
- WebUI 保存改为“先写配置，再后台排队应用”，降低前台等待和卡顿体感。
- 日志分享默认截取尾部 256KB，减少压缩耗时与分享界面卡顿。
- 修复日志分享 ZIP CRC 生成问题，已通过 Android 端 `unzip -t` 校验。
- 同步模块版本到 `v1.5.1` / `versionCode=1510`。

## v1.5.0

- 综合早期 v1.0 通用扫描思路，改进 is_A2H_app 定位逻辑。
- 支持全局音乐触感与 10 槽白名单模式。
- 强化安装脚本，避免 UTF-8 BOM / 反斜杠路径导致双模块或 KSU 异常。
- 增强日志诊断，输出系统识别、定位路径、patch 校验等信息。
