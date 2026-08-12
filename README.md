# A2HHook

A2HHook 是面向 REDMI K80 Ultra / K80U 的音乐触感模块，目标是在 KernelSU / ReKernelSU / ReSukiSU 环境下提供：

- 全局音乐触感；
- 官方 6 个音乐应用白名单；
- 额外 4 个自定义包名槽位，共 10 槽；
- 10 个槽位分别启用或关闭，关闭时保留已填写包名；
- 单一“全局模式”开关：开启为全局，关闭即为白名单模式；
- 手机重启后自动应用配置，并通过通知中心反馈结果；
- 模块栏 WebUI 入口；
- 离线 Miuix 风格紧凑 WebUI，伴生 APK 与 Root 管理器复用同一份页面；
- 开关、按钮与“强劲震感”开关支持 HyperOS 震感反馈和兼容回退；
- 文件管理器直接修改 `config/packages.txt` 后自动热更新；
- 游戏 `FAST` / `FAST|RAW` / 空间音频输出迁移与应用生命周期适配；
- 对没有向 HAL 下发 `appname` 生命周期的低延迟游戏，按真实音轨事件和应用 UID 自动补登记；
- 可选择在游戏期间继续启动后台音乐触感，默认关闭并遵循小米官方暂停策略；
- 两枚控制中心磁贴：“A2H 全局音乐触感”切换全局/白名单，“游戏时启动后台音乐触感”切换游戏期间的后台触感；两者长按均打开同版 WebUI。

当前版本：`v1.5.7-fix`。本版保留 v1.5.7 的 Miuix 风格 WebUI、分层“关于/支持我们”交互、Hyper 强劲震感开关及既有游戏生命周期修复，并补齐没有被系统登记到 HAL 的游戏音轨；v1.5.7 正确回滚包继续作为独立历史基线保留。

本版性能收尾保留所有配置锁、五文件原子回滚、native 双写、两次 I-cache 同步和最终校验。重复的 patcher 能力探测按二进制大小与时间戳缓存；现代 patcher 成功后复用同一次事务内的最终验证，不再额外启动第二个完整 ptrace check；ELF 符号解析在单次进程内缓存。模块自身提交的严格规范配置会记录带 schema 的 inode/纳秒时间戳指纹，后续直接走快速准备；Root 文件管理器修改使指纹失配时仍自动回退完整规范化。配置提交只启动一个合并 worker；快速连续提交时，worker 只在当前五文件原始签名与刚完成事务完全一致时合并旧请求，检测到更新则继续保留待处理标记。稳定配置监听使用 FIFO 阻塞到事件或 30 秒健康超时，音轨 watcher 的常见事件走 shell 内建快路径，核心系统 UID 的拒绝在本次开机内只解析和记录一次。worker 仅在短时工作期间尝试提高调度优先级，不锁频、不常驻高优先级，也不绑定固定 CPU 编号。

## 下载安装

请到 GitHub Releases 下载：

- `a2h_hook_v1.5.7-fix.zip`

刷入方式：

1. 在 KernelSU / ReKernelSU / ReSukiSU 的模块页面安装 ZIP；
2. 重启手机；
3. 进入模块卡片 WebUI；
4. 开启“全局模式”即对所有应用生效；关闭后自动切换为白名单模式；
5. 10 个包名可分别使用右侧开关，模式、包名或开关变化会自动保存并应用。
6. 需要快捷切换时，将“A2H 全局音乐触感”和“游戏时启动后台音乐触感”添加到控制中心；首次使用按 Root 管理器提示授权。后者默认关闭并遵循官方暂停策略，开启后允许游戏期间继续后台音乐触感；两枚磁贴长按均打开 WebUI。

WebUI 使用本地 HTML/CSS/JavaScript 实现 Miuix 设置页风格，不依赖网络、CDN、Compose、Wasm 或 SolidJS 运行时。主页保留两个核心开关和 10 槽编辑；“关于”“支持我们”和二维码均采用带遮罩的底部弹层，点击弹层外会复用同一历史栈带动画逐层返回。关于弹层先展示 A2HHook 项目、项目地址、QQ群和捐赠支持，进入“支持我们”并选择赞赏方式后才按需显示对应二维码；主页底栏爱心会直接进入“支持我们”，作者行可打开酷安个人主页。浅色/深色主题首次默认跟随系统，也可在跟随系统、浅色和深色三种模式间切换。主页旧“A2”方块与关于页蓝底标识已统一替换为透明底“双音轨环 + 双音符 + 连续弧形触感波”SVG，不包含三角或箭头结构；动画仅改变合成友好的位移、旋转、缩放和透明度，并在页面不可见或系统要求减少动态效果时停止。项目地址使用 GitHub 图标，QQ群入口通过显式 Android 包名直接交给 QQ，不回退浏览器。微信支付、微信赞赏与支付宝入口使用本地 WebP，并在首次进入支持页时加载。Android 返回会按当前弹层历史逐层收起。伴生 APK 优先调用 HyperOS/Android 原生震感；KernelSU 模块 WebUI 在没有原生震感接口时，通过现有 `window.ksu.exec` 调用系统振动服务作为短时回退。界面只保留一个“强劲震感”开关，任何震感失败都不会阻断配置保存。

磁贴长按打开的伴生页面以 Android DayNight `uiMode` 作为“跟随系统”的主题来源，系统运行中切换深浅色也会同步页面、背景和系统栏；KSU/ReSukiSU 宿主继续使用其 WebView 媒体查询。开关和按钮交互会在首帧短暂停止纯装饰标识循环，把视觉反馈与配置提交优先排入执行队列，结束后自动恢复无限动画；KSU 宿主缺少原生震感接口时，可选 Root 震感会在配置命令已启动后执行，不改变配置返回码或失败恢复。

“游戏时启动后台音乐触感”默认关闭，此时保持小米对不同应用并存的官方暂停行为；开启后，后台音乐与游戏同时使用扬声器输出时也可继续音乐触感。同一应用切换多条扬声器音频流时，两种策略都允许继续音乐触感。开关在活动游戏中变化时，模块只在 native 应用与校验成功后建立一次共享 AAudio 静音流，借助事件驱动入口立即调用原厂重算；首次初始化、同值应用、普通包名保存和稳态 watcher 不触发。通话、耳机/蓝牙等非扬声器路由、振动流占用和 HAL 总开关保护不会被绕过。

部分原生低延迟游戏虽然存在真实扬声器音轨，但系统不会像普通播放器、视频应用或提示音那样向 HAL 下发 `appname=+包名`，因此旧实现没有活动包名可交给全局/白名单 matcher。模块现在同时消费厂商 `audio_track_message` playback 与系统 AudioPolicy 输出生命周期：从 `/data/system/packages.list` 精确解析真实 UID，以该 UID 建立持续静音的 AAudio 登记流，并按真实 `portId/session` 在最后一条应用音轨停止时释放。解析不依赖字段顺序，兼容 `stopOutput()` / `stoptOutput()` 以及 stop 行没有 appname 的形态；触发器只有在流进入 `STARTED` 且静音 callback 实际执行后才发布 `session ready`，自身 session 会被排除，避免递归。没有可用 AudioPolicy 事件的系统仍使用厂商事件和 70 秒有界 fallback，不会无限常驻。全局模式接受事件中的任意合法普通应用包名，自定义模式只接受 `packages.txt` 与 `package_states` 中已启用的精确槽位；`UID < 10000` 的核心系统包不会启动登记流，并在 root-only 运行目录缓存拒绝结果。worker 以 PID 与 `/proc` starttime 成对校验，异常重启会清理端口、token 和 session；不猜前台应用、不硬编码游戏包名，也不增加周期 `dumpsys` 或 `ptrace`。

磁贴点击时先在主线程立即显示预测状态和对应图标，再在后台通过同一配置锁与 `a2h_apply` 队列提交；成功后读回确认，失败则恢复原状态。安装新模块时会先完整卸载固定伴生包 `io.github.bbbomb0.a2hhook` 及旧数据，再安装 ZIP 内同版 APK；模块卸载时也会卸载伴生应用并清理模块固定运行时文件，因此升级后需要重新把磁贴添加到控制中心。

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

也可以使用 Root 文件管理器直接编辑模块目录中的 `config/packages.txt`。后台 watcher 优先使用系统文件事件，并要求三次 2 秒采样中的内容保持不变后才规范为 10 槽并热更新，避免设备负载较高时把文件管理器的中间文件单独应用；WebUI 与磁贴继续使用即时队列，不受该静默窗口影响。监听因原子替换而重建时会短期轮询签名，缺少事件工具时自动退回 2 秒轮询。新增或改名的有效唯一包名会自动启用，清空槽位会自动关闭。约 30 秒的稳态健康探测只读取 PID 与配置快照，不会启动 native check 或 ptrace；HAL PID、配置或失败重试状态变化时仍会自动应用。

模块在每次开机的 post-fs-data 阶段清理上次开机遗留的 A2HHook pending、锁和临时配置，再由 service 按持久配置重新应用；该清理在系统完成启动后不会执行。

## 兼容性说明

- 已按 HyperOS 2.x / 3.x 差异做运行时语义定位。模块直接读取目标手机当前映射的 `audio.primary.*.so`，不要求预先收集该 ROM 的 HAL、版本号或固定偏移；四个生命周期函数都必须在各自 ELF 函数范围内形成唯一的多锚点控制流关系；
- 游戏兼容继续读取完整 32 位 output flags，并覆盖 `FAST`、`DEEP_BUFFER`、`COMPRESS_OFFLOAD` 与 `AUDIO_OUTPUT_FLAG_SPATIALIZER`。同包名新旧流交接仍使用短生命周期 handoff 承接事件驱动重算；只有当前退出流属于这些合格媒体/游戏输出时才允许建立 handoff，`flags=0` 系统短流不能自行建立交接状态；
- 音轨 watcher 只弥补包名登记缺口：实际包名必须来自厂商 playback 或 AudioPolicy start 并能精确映射到普通应用 UID；核心系统 UID 拒绝结果在本次开机内缓存，全局/自定义配置仍分别决定是否允许，运行时触发器副本位于 root 所有的固定 `0711/0555` 目录，应用 UID 只能执行、不能替换；
- 输出层始终允许同一应用的多条扬声器流。“游戏时启动后台音乐触感”开启后遍历完整活跃应用链表，任一应用命中当前全局/10 槽规则即可继续 A2H；关闭并遵循官方策略时，由不同应用事件建立两阶段并发 latch：`pending` 会跨过游戏节点短暂消失、正式输出尚未建立的单流过渡，208 字节 RX 页尾 helper 前置补齐 `AudioALSAStreamOut::open()` 成功事件，再遍历 manager 的真实 stream 槽表和每条流的设备向量；至少两条活动扬声器流时提交并持续暂停，已提交状态回落为单流且只剩一个应用时才清除。这样既避免原 HAL 按相同 flags 聚合计数后漏暂停，也不让单应用多流误触发暂停；
- v1.5.6-fix 在 `isA2HAllowed()` 的原厂“活动输出为零”路径加入 guarded idle-clear：普通 `updateA2HMode()` 控制流会跳过 helper，只有零活动分支进入 helper 清除旧 handoff，再返回原厂 `mov w26,#1`，完整保留返回值 `2` 的清理语义。这样媒体真正归零后，后续锁屏解锁、APP 返回等 `flags=0` 系统短音无法继承旧 A2H 状态；
- 所有游戏生命周期与 idle-clear 补丁均通过 ELF 唯一符号、可变函数大小、完整磁盘/内存所有权、ROM 自身 `BL` 目标和标准 AArch64 PLT 形态验证；`updateA2HMode()`、`isA2HAllowed()`、`setParameters()` 与 `updateOutputPoolActive()` 的局部布局允许整体正负漂移，实际补丁、快照、I-cache 与回滚地址统一由唯一语义布局派生，不写死 OS2/OS3 位移。符号被裁剪、锚点缺失/多命中或控制流不一致时会安全拒绝并在日志中给出采集线索，不按版本号或短函数头猜测写入；当前公开 v1.5.6、早期 handoff 和已作废实验候选只能按各自完整机器码迁移；handoff 只使用四份 HAL 均验证无原厂直接引用的 manager `+0x519..+0x51f` padding；
- 当前主线保留双写入、I-cache 同步与失败回滚等兼容保险；
- 为避免播放过程中周期性暂停音频 HAL，稳态不会在 PID 与配置均未变化时主动抢回被其他模块覆盖的补丁；遇到冲突请停用同类模块，或切换一次模式/重启后重新应用；
- K80U / HyperOS 3.0.302 已完成全局/白名单媒体往返、当前音轨/下一首、官方/后台触感策略、FAST/空间音频迁移、配置热更新和稳态回归；后台酷狗概念版进入王者时，新开关关闭可在第二真实流 open 提交后自动保持 A2H 暂停，音乐会话继续播放且无需暂停/重播，开启则在相同多流过程持续保留 A2H；活动游戏内双向切换策略也已验证一次性事件立即重算。锁屏/解锁无误震和 APP 返回边界继续保留；
- HyperOS 3.0.305、2.0.218 与 2.0.208 的已归档 HAL 已逐字节通过同一生命周期补丁静态核验；无归档样本 ROM 会在目标设备上走同一运行时语义验证，但仍属于 best-effort 兼容，未实机验证的版本不描述为已经通杀；
- 模块不包含系统原厂音频库文件；
- 不建议和其它同类音频 HAL 修改模块同时启用。

如果遇到问题，请从 `/data/adb/modules/a2h_hook/` 取出 `a2h_patch.log` 与 `action.log`，并附上完整系统版本。

## 从源码构建

依赖：

- Android NDK r26 或更新版本；
- CMake 3.18+；
- Python 3（统一负责严格清单打包、CRC 和 Unix 权限校验）。

若需要重新构建控制中心伴生 APK，还需要 Android SDK Platform 36、Build Tools 36、JDK 21 和自己的 Android 签名密钥。仓库不包含作者签名私钥；正式提交的 APK由 `companion/build.ps1` 从 `webroot/` 同步资源后构建。

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
a2h_hook_v1.5.7.zip
```

## 仓库结构

```text
.
├── src/                 # native 源码
├── companion/           # 控制中心磁贴与同版 WebUI 伴生 APK
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
- [Miuix](https://github.com/compose-miuix-ui/miuix)、[Hybrid Mount](https://github.com/Hybrid-Mount/meta-hybrid_mount) 与 [HyperLight](https://github.com/KiminonawaResa/HyperLight) 的设置页设计思路（仅作视觉与交互参考，不进入运行时依赖）。

## 开源协议

从许可证迁移提交及 `v1.5.6` 起，本项目原创代码按 `GPL-3.0-or-later` 开源。分发本项目或其衍生作品时，必须继续使用兼容的 GPL 条款，向接收者提供对应源码与相同权利，不得将衍生版本闭源。完整条款见 `LICENSE`，执行边界见 `LICENSE_POLICY.md`，第三方组件见 `THIRD_PARTY_NOTICES.md`。

代码进入官方 A2HHook 仓库前必须经过作者或当前维护者明确书面批准并实际合入；仓库的 CODEOWNERS 与分支保护共同执行这一要求，详细流程见 `CONTRIBUTING.md` 和 `LICENSE_POLICY.md`。这项规则只管理官方仓库，不限制 GPL 允许的 fork、修改和再分发。

`v1.5.5-fix3` 及更早版本曾按 MIT 发布。已经从这些历史 tag、附件或副本获得的 MIT 权利不能被追溯撤销，相关历史对象保持原样。`v1.5.6` 及后续发布只按 `GPL-3.0-or-later` 提供。

## 免责声明

本项目仅用于学习、研究和个人设备调试。刷入模块存在系统稳定性风险，请自行备份并承担使用后果。
