# 更新日志

## v1.5.7-fix

- 本节与下方 `v1.5.7` 小节共同构成 `v1.5.7-fix` 相较 `v1.5.6-fix` 的完整变更：包含 WebUI/磁贴重构、配置与运行效率优化、任意普通应用包名的音轨补登记，以及本节新增的 callback-ready / AudioPolicy 租约修复。`v1.5.7` 是本次开发过程中的中间版本标识，GitHub 正式发布版本为 `v1.5.7-fix`。
- 修复控制中心磁贴长按进入伴生 WebUI 后“跟随系统”仍固定浅色的问题：伴生宿主不再继承固定 Light 主题，改由 Android DayNight `uiMode` 作为系统主题真值，并在运行中切换主题时同步页面、原生背景和系统栏；页面自身继续负责手动浅色/深色覆盖，禁止 WebView 算法暗化二次改色。
- 优化 KSU/ReSukiSU 内 WebUI 的交互帧优先级和配置落地：开关先完成 DOM 首帧反馈，点击与 Root 提交的短窗口暂停装饰 SVG 动画，完成后自动恢复；缺少原生震感桥时把可选 Root 震感排在配置命令入队之后，避免与保存争用同一 bridge/su。fast-controls 的模式/策略前后认证合并为每轮一份 `stat` 快照，继续保留锁内表不变校验、失败回滚、五文件事务和 native 最终验证。
- 修复 watcher 退出或 logcat 重启时与租约 trigger 正常关闭的竞态：先撤销全部 token，并为所有 worker 提供共享且有界的优雅关闭窗口，再终止仍存活的 PID+starttime owner。该等待只发生在退出/重启，不增加稳态轮询或 SoC 空转。
- 修复部分原生低延迟游戏有真实 `FAST` 扬声器音轨、却因系统未向 HAL 下发 `appname` 生命周期而无法进入全局/自定义 matcher 的问题。新增事件驱动音轨 watcher，从厂商 `audio_track_message` playback 事件读取实际包名，精确映射 `/data/system/packages.list` UID，并以该 UID 建立一次极短静音登记流；全局接受任意合法普通应用包，自定义只接受 10 槽中已启用的精确包名。实现不硬编码游戏、不猜前台应用、不持续轮询 `dumpsys`，以同包冷却阻止触发器事件递归，并使用 root `0711/0555` 固定运行时目录防止应用替换触发器；核心系统 UID 拒绝结果按本次开机缓存，重复系统提示音不再扫描 UID 或写日志。
- 修复 OS3.0.305 上短 AAudio 流在 HAL 完成包名登记前已经关闭、但旧触发器仍打印成功的问题。触发器现在等待 `STARTED` 和首次静音 data callback 后才发布真实 `session ready`，持续供给静音帧并检查异步错误，结束时验证 `STOPPED` 与 close；watcher 以 AudioPolicy `portId/session` 维持租约到最后一条真实应用音轨停止，兼容字段顺序变化、`stopOutput/stoptOutput` 和 stop 无 appname。厂商事件作为通用入口与无 policy 日志时的 70 秒有界 fallback；PID+starttime owner、防自 session 递归、异常重启清理和多端口状态机均已加入回归，不按 ROM 版本或游戏包名分支。
- 完整 ADB/NDK/四 ROM 严格套件为 `76 PASS / 0 FAIL / 0 GAP`。K80U / HyperOS OS3.0.302 / Android 16 已完成全局/自定义配置、任意包名音轨 watcher、fallback 到 callback-ready 再到真实 policy port 提升、stop 释放、主题切换和 35 秒稳态无周期 ptrace/日志增长的实机验证。OS3.0.305 结论来自问题日志、源码分析与归档 HAL 静态 fixture，仍需对应设备完成目标游戏实机验证；未声明全系统版本均已实机验证。
- 最终发布 APK 为 161,041 bytes，`a2h_hook_v1.5.7-fix.zip` 为 578,677 bytes；相较 638,651-byte 优化前基线减少 59,974 bytes（约 9.4%）。发布 ZIP SHA-256 为 `C415043ABF883E64D04B7B40256A24B7541E2CB8A2D59D1848138ACFA86A7613`。
- 版本迭代为 `v1.5.7-fix / versionCode=1571`，生成独立 `a2h_hook_v1.5.7-fix.zip`；用户指定的 v1.5.7 正确回滚 ZIP 保持原文件和哈希，不覆盖、不删除。

## v1.5.7

- WebUI 新增不依赖第三方模块的沉浸式系统栏适配：伴生 APK 使用透明状态栏/导航栏和原生 edge-to-edge，按四边 WindowInsets 将挖孔、状态栏与手势小白条安全区传入同一份 HTML；WebUI 使用 `viewport-fit=cover`、CSS safe-area 和宿主能力检测覆盖 KernelSU 系宿主，不调用会隐藏系统栏的全屏接口，也不修改其他应用的全局沉浸规则。深浅主题切换会同步 A2H 伴生界面的状态栏图标与手势条明暗。
- 根据 REDMI K80 Ultra / ReSukiSU 真机 WebView 可用高度进一步收紧纵向节奏，完整显示两个模式项、10 个白名单槽位和底栏，无需短距离滚动；保留 17px 主要文字、49×28 开关和至少 44px 槽位触控高度。伴生 Activity 将 WindowInsets 从物理像素换算为 WebView CSS 像素，WebUI 同时消费 KernelSU 注入的 `--safe-area-inset-*`，消除磁贴入口额外滚动并统一两种宿主的安全区。移除 BottomSheet 顶缘阴影并隔离圆角合成，修复“关于”页圆角处出现黑边的问题。
- WebUI 进一步按 Miuix/HyperOS 设置语义重构：`SwitchPreference`、28px 圆角 BottomSheet、反转色 Snackbar、浅/深色主题、间距与焦点行为均由原生 HTML/CSS/JavaScript 实现，不引入 Compose、Wasm、CDN 或新运行时。新增软键盘 `visualViewport` 避让、弹层焦点回归/拖拽关闭、Snackbar 滑动关闭和更完整的 Android 返回处理。
- 修复 KernelSU 模块 WebUI 没有震动反馈：伴生 APK 继续优先使用原生 HyperOS/Android 震感；KernelSU 管理器没有 `window.ksu.haptic` 时，通过原有 `window.ksu.exec` 调用 `vibrator_manager`，并兼容旧 `vibrator` 服务。触感调用独立、短时、失败静默，不改变配置事务。
- 将 WebUI 与控制中心的反向“游戏时暂停”改为正向“游戏时启动后台音乐触感”。底层 `game_auto_pause` 配置格式和默认值不变；默认关闭代表遵循官方暂停策略，开启代表游戏期间继续后台音乐触感。游戏磁贴的 active 状态、图标和副标题同步按新语义反转。
- WebUI 默认跟随系统浅/深色主题，并保留浅色、深色和跟随系统三态切换。标题改为“A2H 音乐触感白名单”；底栏爱心直接进入“支持我们”，关于页作者行可跳转酷安个人主页；QQ群入口显式指定 QQ 包名，无法启动 QQ 时只提示而不打开浏览器。
- WebUI 的“关于与支持”改为 Miuix 风格分层底部弹层。首先展示 A2HHook 项目卡、项目地址、QQ群和捐赠支持，不再打开即显示赞赏二维码。
- “支持我们”只列出微信支付、微信赞赏和支付宝三个选项，移除每项下方的扫码说明；三枚入口按用户最终参考图转换为 96×96 RGBA WebP，合计约 12.4 KB，首次进入支持页时才加载。用户选择后才加载对应二维码。点击底部弹层外或使用返回键，均按“二维码 → 支持我们 → 关于 → 主页”逐层动画收起，伴生 APK 在主页再次返回时才退出。
- 主页旧“A2”方块与关于页蓝底标识统一替换为透明底“双音轨环 + 双音符 + 连续弧形触感波”项目图标，完全移除三角/箭头元素。纯 CSS/SVG 图标只保留前台可见时的较快、大幅无限循环；循环只使用 `transform/opacity`，离开对应页面、进入后台或系统启用减少动态效果时立即停止，无定时器、帧循环、滤镜或后台服务。
- 项目地址使用 GitHub 图标；QQ群使用无眼睛、嘴部等表情细节的腾讯 QQ 纯色企鹅轮廓，图标保持静态并继承当前明暗主题。
- 四档交互震感滑块收敛为单一“强劲震感”开关。开启时开关、按钮和页面动作统一使用 HyperOS 强反馈，失败仍按原边界回退且不影响 Root 配置事务。
- 性能与体积收尾：配置队列保留五文件原子事务和完整 native 校验；patcher 能力按二进制版本缓存，现代 patcher 成功后不再额外执行第二次完整 ptrace check，单次进程内复用严格 ELF 符号结果。模块自身严格提交会生成带 schema 的配置指纹，模式、游戏策略与 23 参数完整提交可直接走快速准备；文件管理器外部修改仍因指纹失配回退全量规范化。配置变化直接启动唯一合并 worker，成功应用后删除冗余二次准备；快速连续提交产生的旧 pending 只有在当前五文件原始签名与刚完成事务完全一致时才会合并，检测到更新则继续保留，避免对同一 revision 重复执行 native 事务。稳定监听改为 FIFO 阻塞到事件或 30 秒健康超时，音轨事件常见路径合并为 shell 内建解析，logcat 异常使用 `2/4/8/16/30s` 封顶退避。worker/native 只尝试短时 `nice=-10`，不锁频、不固定 CPU、不常驻。阶段性候选 APK 为 161,040 bytes、ZIP 为 570,976 bytes；后续 callback-ready/AudioPolicy 修复改变了正式包内容，最终公开产物以 `v1.5.7-fix` 小节记录的 161,041-byte APK 与 578,677-byte ZIP 为准。
- 版本统一迭代为 `v1.5.7 / versionCode=1570`。保留 v1.5.6-fix 历史包与用户同目录副本，不覆盖或删除。

## v1.5.6-fix

- WebUI 重构为离线 Miuix 风格紧凑设置页：两个核心模式项与 10 槽白名单继续保持单页高密度布局，伴生 APK 和 KernelSU / ReKernelSU / ReSukiSU 模块页复用同一份资源，不引入 Compose、SolidJS、CDN 或本地网络守护进程。
- 新增统一交互震感接口。伴生 APK 优先反射 HyperOS `HapticFeedbackUtil`，再回退到 WebView/Android `VibrationEffect`；开关、按钮、赞赏方式和震感强度滑块使用不同的短反馈，强度可设为关闭、轻柔、清晰或强劲，反馈失败不影响配置事务。
- 新增“关于与支持”底部弹层，离线包含微信支付、微信赞赏和支付宝赞赏码；更新 QQ群入口为 `https://qm.qq.com/q/nOF82hSWwU`。三张资源均进入严格 WebView本地路径白名单、签名APK资产校验和模块ZIP固定清单，网络/文件/content访问边界不放宽。
- 修复锁屏解锁仍会触发短暂音乐触感，以及 APP 返回时偶发震动的问题。根因不是单一系统包名，也不是必须禁用所有 `flags=0` 输出，而是媒体/游戏 handoff 在应用表和活动输出都已归零后仍可能保持为 1，使后续系统短流继承旧 A2H mode。
- 保留 v1.5.6 的完整32位游戏 output flags、同包名多流交接与事件驱动重算；额外限制只有 `FAST`、`DEEP_BUFFER`、`COMPRESS_OFFLOAD`、`SPATIALIZER` 退出流可建立 handoff。该门禁用于阻止系统短流自行建立交接状态，但不再被误当作过期 handoff 的唯一修复。
- 在四份 HAL 一致的 `isA2HAllowed()` 零活动路径加入 guarded idle-clear。普通 `updateA2HMode()+0xec` 会先跳过 helper；只有零活动分支跳到 `+0xf0`，执行 `mov w19,wzr`、清除 manager `+0x519` handoff，再跳回原厂 `+0x18c mov w26,#1`，完整保留返回值 `2` 的清理语义。
- `updateA2HMode()`、`isA2HAllowed()`、`setParameters()`、`updateOutputPoolActive()` 与 `AudioALSAStreamOut::open()` 改为运行时唯一语义布局定位：函数大小和局部锚点允许整体正负漂移，实际补丁、快照、I-cache 与回滚地址使用同一组动态派生偏移，不依赖系统版本号或固定 ROM offset。208字节 RX 页尾 helper 前置包含 open 成功后重算入口，后续保留两阶段并发 helper、guard、应用策略、148字节 handoff helper、output-pool尾分支和三个 appname/重算事件点，全部纳入同一所有权与协调事务；`pending` 跨过游戏节点消失到正式第二流到达的过渡，`committed` 只在并发流确实回落后清除。任何半事务、锚点缺失/多命中或未知机器码都会安全拒绝；上一代176/160字节及旧64/56字节 helper 仅按完整机器码形态迁移。
- 修复活动游戏中切换“游戏时暂停”后必须等待下一次流事件才生效的问题。`a2h_apply`新增最后成功应用策略标记；只有策略实际变化且patch、live check、稳定快照全部成功时，才调用一次现有共享AAudio静音trigger触发原厂`updateA2HMode()`，双向切换均可立即重算。首次开机初始化、同值应用、模式/包名保存和30秒健康探针不调用trigger；trigger失败不提交applied元数据，由队列或watcher继续重试。
- 新增“游戏时暂停音乐触感”控制中心磁贴，点击独立切换“游戏时暂停”，长按与原磁贴一样打开 APK 内置同版 WebUI。图标采用简洁手柄、顶部音符和双侧触感线；开启暂停时由加长的粗 `\\` 斜线贯穿手柄，关闭时不带斜线，状态语义与配置一致。“A2H 全局音乐触感”在关闭全局/进入自定义模式时也使用同方向斜线。两枚磁贴均在点击后立即切换系统 active 状态与图标，后台再走原配置锁、队列、native应用和状态读回，失败才回滚视觉状态。
- 伴生应用改为干净生命周期：安装模块时先卸载固定包名及旧数据，再安装 ZIP 内同版签名 APK；开机兜底同时比较 versionCode 与 APK SHA-256，同版本修订也不会误留旧 APK；模块卸载时完整卸载伴生应用并清理固定 pending、lock、临时配置和日志。核心 native 模块在伴生安装失败时仍可独立运行。
- 当前公开 v1.5.6、早期 handoff、失败的 active-device-vector 候选和首版 fallthrough 错误候选仅作为完整历史迁移指纹；正式写入仍执行协调快照、双写、两次 EL0 I-cache同步、最终验证和失败全量回滚。
- OS2.0.208、OS2.0.218、OS3.0.302、OS3.0.305 四份归档 HAL 均通过零活动控制流、动态双向分支、helper机器码、游戏生命周期区域和 ROM 自身调用目标逐字验证；合成 fixture 同时覆盖 `+0x40` 布局漂移和双命中拒绝。没有预归档 HAL 的 ROM 会直接解析目标手机当前映射的 HAL；不满足完整唯一结构时拒绝写入并保留诊断信息。
- 自动门禁为 `66 PASS / 0 FAIL / 0 GAP`。ARM64事务故障注入覆盖24个辅助写故障、24个cache故障、6类历史迁移（含上一代176/160字节精确迁移）、4组策略/stream helper生成、4种output形态、24组生命周期语义和5组动态布局结果；新增策略刷新harness覆盖首次/同值跳过、双向一次触发、失败重试和policy->revision->snapshot提交顺序。
- OS3.0.302 已通过8槽自定义与全局媒体播放/暂停往返、flags=0短流归零、两次锁屏/解锁无误震、多轮 APP返回、当前音轨/下一首和“游戏时暂停”开启/关闭完整矩阵。后台酷狗概念版进入王者时，开启策略在第二真实扬声器流open提交后自动保持`mode=0`且音乐继续播放，无需暂停/重播；关闭策略在相同多流过程保持`mode=1`，活动现场双向切换也能一次性立即重算。
- 版本迭代为 `v1.5.6-fix` / `versionCode=1561`，使用独立tag、Release与 `a2h_hook_v1.5.6-fix.zip`；历史v1.5.6不覆盖、不删除。

## v1.5.6

- 重做游戏输出识别：从低字节判断改为读取完整 32 位 output flags，同时覆盖 `FAST`、`FAST|RAW` 与 `AUDIO_OUTPUT_FLAG_SPATIALIZER`；继续排除 MMAP、VOIP、通话和非扬声器安全边界。
- 修复游戏开场后切换正式音频流时音乐触感消失的问题。根因是新流先加入但同包名引用未增加，旧流随后退出会在新流仍播放时删除唯一应用节点；若把节点留在官方表中，又会污染下一款游戏的开场判断。本版改为在严格的同包名交接条件下写入 manager padding 的短生命周期 `handoff` 标志，官方应用表仍按原厂路径 erase；输出池变化只触发重算，新的 `+appname` 已正式提交后才清除 handoff，避免在包名登记前产生开场空窗。
- 新增 `setParameters()+0x2030` 的 148 字节 handoff/helper 区域，并将 `updateOutputPoolActive()+0x278` 的原厂尾跳入 helper。helper 保留原栈保护、寄存器恢复和 `updateA2HMode()` 尾调用；同包名流交接期间由事件驱动重算承接临时状态，不增加定时线程或周期 ptrace。
- 修复后台音乐与游戏并存时策略要等媒体卡片重播才更新的问题。“游戏时暂停”开启时保留 ROM 原厂行为，并在第二个应用加入的同一事件立即暂停；关闭时遍历完整活跃应用链表，任一应用命中当前全局/10 槽规则即可继续 A2H。
- 多应用循环使用 AArch64 被调用者保存寄存器 `x23` 持有链表节点；运行时从各 ROM 原厂 `BL is_A2H_app` 解码 PLT目标并为新调用点生成对应 `BL`，不写死 OS2/OS3 位移。
- 将 32 位输出 flags、72 字节 stock/relaxed handoff 应用策略、148 字节 handoff/helper、`updateOutputPoolActive` 尾分支、两个 appname 事件点、输出策略与 `is_A2H_app` 主补丁纳入同一协调事务。每个区域都执行磁盘/内存完整所有权、双写、两次 EL0 I-cache 同步、最终验证和失败时全量回滚；旧公开 v1.5.6 与第一代实验 handoff 都只能按各自完整 legacy 形态迁移。
- 四份已归档 HAL（OS2.0.208、OS2.0.218、OS3.0.302、OS3.0.305）均通过唯一 ELF 符号、精确函数大小、RX 映射、ROM 专属 BL/PLT 和目标区域逐字节静态验证；仅 OS3.0.302 已完成本版实机回归。
- 同一应用的多条扬声器输出在两种策略下都允许 A2H；WebUI 的“游戏时暂停”只控制不同应用并存策略并继续默认开启。关闭后任一活跃应用命中白名单即可继续，通话模式、非扬声器路由、振动流占用和 HAL 总开关保护不变。
- 新增标准 Android `TileService` 伴生 APK：控制中心磁贴点击复用 `a2h_apply toggle` 切换全局/白名单，长按打开 APK内置同版 WebUI；不申请无障碍、前台服务或网络权限，不依赖 KernelSU 私有 WebUI token。
- 修复伴生 WebUI 空白页与 Android 15/16 状态栏遮挡：WebView 只加载固定本地资源，并按系统 WindowInsets 动态留出状态栏/挖孔安全区。
- 修复 30 秒轻量健康检查无条件刷新日志 mtime；稳定 PID/配置下只读取元数据，不执行 native check/ptrace，也不写日志。`state` 与 `game_auto_pause` 继续使用同锁快照和失败回滚。
- 文件管理器热更新要求三次 2 秒采样保持不变，避免高负载下把多阶段保存的中间表单独应用；WebUI与磁贴继续使用即时队列，不增加交互等待。
- 模块打包器固定所有 ZIP 成员时间戳，独立校验器与静态回归同步拒绝继承源文件 mtime 的包；同一输入和工具链可重复生成相同发布 ZIP。
- OS3.0.302 已完成全局/白名单、原厂/放宽策略、FAST/空间音频迁移、两轮 HAL 重启和 slot 8 原子/多阶段热更新；最终 committed-app handoff 在 live HAL 上完成旧形态迁移与完整校验，HAL PID 未重启且 `TracerPid=0`，用户实机确认王者开场与后续正式页面音乐触感均正常。严格回归为 `62 PASS / 0 FAIL / 0 GAP`。
- 项目原创代码从本版本起按 `GPL-3.0-or-later` 分发，衍生分发必须继续开源并提供对应源码；贡献进入官方仓库须经维护者书面批准。`v1.5.5-fix3` 及更早 MIT历史对象不追溯改写，当前同名 v1.5.6 公开内容以本修复构建为准。
- 版本保持 `v1.5.6` / `versionCode=1560`；GitHub上的旧 v1.5.6 源码说明、tag目标、Release正文与 ZIP由本修复构建整体替换，不保留旧的同版本公开内容。

## v1.5.5-fix3

- 修复 K80U / HyperOS 3.0.302 播放音频时约每 30 秒出现一次短暂卡顿的问题。根因是 watcher 的周期 `a2h_apply check` 会让 native patcher 通过 ptrace 暂停整个音频 HAL 线程组。
- 保留约 30 秒的轻量健康探测，但稳态仅核对 HAL PID、配置快照、revision 与失败重试状态，不再进入 native check；通知状态确认也改用同一组非侵入式元数据，避免状态不一致时从间接路径重新触发 ptrace。
- 修复 `inotifyd` 重建单文件监听时原子替换事件可能落入注册窗口、导致 `packages.txt` 热更新延迟到下一次 30 秒轻量探测的问题；监听重建后用短期签名轮询覆盖窗口，不触发 native check 或 ptrace。
- 保留真实 apply 后的 native 验证、补丁双写、两次 I-cache 同步、完整 76 字节 matcher、页尾白名单表与失败回滚，不以削弱 OS2/OS3 兼容保护换取流畅度。
- 新增生产 watcher 40 周期运行回归：稳定 PID 与配置下必须得到 `native=0`、`apply=0`，同时保持两次轻量 PID 探测；静态契约也禁止稳态 watcher 调用 check/status/show。
- OS3.0.302 的 HAL 重启回归改为轮询 `last_pid`、applied snapshot 与 revision，确认 watcher 已完成恢复后只执行一次最终 native 验证，避免测试本身高频暂停音频 HAL。
- 开机早期会清理 `/data/local/tmp` 中仅属于 A2HHook 的上次开机 pending、worker/apply/config 锁与临时配置；清理受 `sys.boot_completed != 1` 限制，系统启动后手动触发脚本不会干扰当前 worker。
- 同 PID 且配置未变化时不再周期抢回被其他模块覆盖的补丁；这是避免实时音频中断的有意边界，发生冲突时应停用同类模块，或切换模式/重启后重新应用。
- 版本独立迭代为 `v1.5.5-fix3` / `versionCode=1553`；`v1.5.5-fix` 与 `v1.5.5-fix2` 的历史标签、Release 和下载资产不覆盖、不删除。

## v1.5.5-fix2

- 保留 `v1.5.5-fix` 第一代修复版的标签、Release 与下载资产；本版使用独立的 `v1.5.5-fix2` / `versionCode=1552`，作为第二代修复迭代。
- 收紧 `build.sh clean` 的清理范围，只删除当前版本生成包，不再清理项目根目录中的第一代或其他历史模块包。
- 修复 Root 文件管理器只修改 `packages.txt` 第 7～10 槽时，旧 `package_states` 校验值轻微漂移会阻止新包名自动启用、随后又被错误吸收到隐藏基线的问题。现在由“包名基线变化且 generation 未变化”识别单文件编辑，state 差异仅记录诊断。
- watcher 优先通过系统 `inotifyd` 接收配置事件，仅在事件、debounce 或健康周期到达时计算签名；缺少 `inotifyd` 时自动退回 2 秒签名轮询。连续两次签名一致后才处理，HAL native live-check 始终保持约 30 秒周期；事件模式可减少空闲 checksum/awk，轮询回退也不会提高 ptrace 频率。
- watcher 在配置、apply 或 queue worker 正忙时快速延后，减少锁等待、重复应用和 `lock-timeout` 噪声；失败重试仍保留约 50 秒起的有界指数退避。
- WebUI 成组提交顺序改为 `packages.txt`、`package_states`、`config_generation`，generation 最后落盘作为完整事务标记；提交前备份旧配置，普通写入失败或中断时在释放锁前完整回滚，并为 Root 命令设置严格等待上限和超时余量。
- queue worker 在应用失败并释放锁后会重新接管并发到达的 pending 请求，避免极窄竞态下配置请求无人消费；`inotifyd` 回调只响应四个用户配置文件，忽略 baseline、revision 与临时文件事件。
- 安装和覆盖升级时保留 generation 与有效旧 baseline；仅在 baseline 缺失或损坏且配置完整时重建，既关闭首次开机服务规范化前的识别空窗，也保留尚未处理的包名差异。
- 修正干净首次安装时对尚不存在的隐藏 baseline 直接做输入重定向而产生的无害报错；现在先检查文件存在性，再验证或安静重建基线。
- 新增生产 normalizer 运行回归，覆盖第 8 槽单文件改名、state CRC 漂移、清空槽位、WebUI 显式关闭、baseline 缺失恢复与幂等；OS3.0.302 设备脚本新增 watcher-only 第 8 槽验证并完整备份派生配置。
- native HAL 定位、76 字节 matcher、页尾白名单表、补丁双写、两次 I-cache flush 与失败事务回滚保持不变。

## v1.5.5-fix

- 针对已提供的 HyperOS 3.0.305 HAL 增加严格 profile 与 ELF / 运行时映射身份校验；遇到无法证明归属的外部跳板时安全拒绝，不按短函数头盲目覆盖。该系统仍标记为待对应实机验证。
- 白名单路径继续使用经 ELF 验证的页尾存储和完整 matcher 校验，避免旧版普通 `.bss` 区域被 HAL 后续初始化覆盖。
- 修复 KernelSU / ReSukiSU WebUI 命令桥接与状态保持：切换模式或修改槽位后回读设备配置，只有实际状态一致才显示成功，重新打开页面以设备状态为准。
- 通知仅在系统命令确认发布成功后记录状态；临时失败最多重试 3 次，同一运行状态达到上限后停止重试，状态变化或下次开机再重新尝试。
- 默认发布链收敛为 native patcher / trigger；Dobby、Zygisk 注入库与 LD_PRELOAD 路径仅作为 legacy/诊断源码保留，不进入模块 ZIP。
- `build.sh`、GitHub Actions 与本地打包统一调用 `package_module.py` 的显式文件清单，避免递归夹带配置临时文件、日志或旧注入产物。
- 版本统一为 `v1.5.5-fix` / `versionCode=1551`；HyperOS 3.0.305、2.0.218 与 2.0.208 仍需对应系统实机回归，不宣称已完成全版本实机验证。

## v1.5.5

- `packages.txt` 支持文件管理器直接修改后的自动热更新；watcher 先确认配置写入稳定，再规范化并应用，避免读取截断写入过程中的半成品。
- 新增隐藏配置基线，区分文件管理器单文件编辑与 WebUI 成组提交：外部新增或改名的有效唯一包名自动启用，清空槽位自动关闭，WebUI 设置的独立开关状态不会被误覆盖。
- WebUI 去除独立“白名单/自定义模式”按钮，仅保留“全局模式”开关；开启为全局，关闭自动进入自定义白名单模式。
- WebUI 完全移除配置备份/恢复、模块工作状态栏和一键分享日志，保留 `a2h_patch.log`、`action.log` 的后台记录与开机诊断。
- WebUI 收敛为紧凑单页布局，保留 10 槽编辑、独立开关、恢复官方默认、主题切换和失败时的手动命令回退。
- WebUI 底部增加作者标记，以及可直接跳转的酷安主页、GitHub 项目和 QQ群入口；通过 Android 系统 Intent 打开外部应用，避免内嵌 WebView 拦截 HTTP 或无法识别 `mqqapi://`。
- 模块包移除不再使用的日志分享脚本与相关打包权限项，减少无效代码和后续维护耦合。
- native 跨版本定位、页尾白名单表、补丁双写、两次 EL0 I-cache flush 与失败回滚逻辑保持 v1.5.4 行为不变。

## v1.5.4

- 白名单 10 槽存储改为当前 HAL 最后一个可写 `PT_LOAD` 声明结束后的页尾空隙，不再扫描或占用任意 `.bss` 零区，降低不同 ROM/OTA 上覆盖 HAL 自有数据的风险。
- 新增 ELF64 program header、映射权限、段重叠、容量和零值检查；无法证明页尾区域可安全使用时，白名单模式会明确失败且不写入，全局模式仍可独立工作。
- 将白名单身份校验扩展为完整 16 字节 marker，并要求 10 个非空指针精确指向各自固定 64 字节槽位。
- 使用模块自身生成的完整 19 指令（76 字节）stub 作为未知 ROM 旧补丁恢复证据；不再把普通 8/16 字节函数头当作未知系统恢复依据。
- 修复旧白名单 stub 存在但旧 `.bss` 表已失效时无法重新应用的问题；已知 profile 和未知 OTA 均可在严格验证后重建页尾表。
- 保留补丁双写和两次 EL0 合法 I-cache flush，并为字符串、指针表、marker 和函数代码保留失败回滚。
- 明确区分已知 HAL：OS3.0.302 profile 为 `0x3e3fc0`，OS2.0.218 profile 为 `0x3e4280`；两份 OS2.0.218 `mediatek/mt6991` 文件内容一致。
- 验证边界：OS3.0.302 已完成实机白名单、全局、模式往返与 10 槽关闭检查；OS2.0.218 已完成静态 HAL 核验；OS2.0.208 目前只有日志候选 `0x3e3b90`，仍需对应 HAL 或目标设备实测，未虚报为已验证。

## v1.5.3

- 修复 HyperOS 2.0.208 上旧临时配置反向覆盖持久配置的问题；启动时以模块配置为唯一可信来源，并使用原子写入和固定 10 槽规范化。
- 修复白名单 10 个指针全空仍显示成功、配置变化可能被锁竞争丢失以及 watcher 只检查函数头的问题。
- 加强跨版本函数定位：未知旧 hint 不再仅凭通用 8 字节函数头优先采用，降低 OTA 后误命中同形函数的风险。
- 移除 KernelSU 模块页无内容的“执行”按钮；应用逻辑迁移为模块内部命令，WebUI 入口保持不变。
- 重启后自动应用当前模式，并在通知中心显示全局/白名单启用成功或失败。
- WebUI 新增 10 槽独立开关，取消“保存并应用”；模式切换、包名修改、开关、恢复默认和导入备份均自动保存并合并应用。
- 配置备份升级为 v4，保留每槽开关状态并兼容旧版备份。
- 收紧安装脚本的历史模块清理范围，避免按宽泛名称误删其他音频或触感模块。
- 全局补丁增加固定 16 字节状态标记，并兼容旧版 stock-tail 与白名单 stub-tail，避免模式切换后下一次应用无法定位。
- 白名单仅在找到并验证当前 HAL 的 RW 零空洞后写入；空间不足或映射异常时失败退出，不再写入硬编码地址。
- 配置校验改为只拦截启用槽位；关闭槽位可保留待用文本，运行时仍严格输出固定 10 行有效名单。
- 统一 10 个槽位的 63 字符包名容量，修复前 6 槽填写较长自定义包名后 live check 误判失效的问题。
- WebUI 的包名、槽位开关和模式改为在共享配置锁内成组提交，避免 watcher 读到跨版本混合配置并恢复默认。
- apply / check / status 共用 HAL 操作锁，修复锁目录刚创建但 PID 尚未写入时可能并发修改音频进程的竞态。
- 白名单字符串、指针表、marker 与函数代码采用快照、完整写回校验和失败回滚，避免极端写入失败留下半完成状态。
- watcher 稳态检查启用静默配置准备，停止每 25 秒重复写入两份日志；通知在发布前重新校验，并在成功/失败状态转换时更新。
- Windows 回退打包流程显式规范 ZIP Unix 权限：脚本/二进制 `0755`，配置/资源 `0644`。
- Windows 使用 `zip` 时还需要 Python 3 规范 ZIP 权限元数据。
- 验证边界：当前 ADB 实机为用户本人的 K80U（HyperOS 3.0.302 / Android 16）；HyperOS 2.0.208 结论仅来自离线日志，未标记为实机验证。

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
