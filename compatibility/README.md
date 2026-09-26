# A2HHook 兼容性资料采集规范

本目录只保存公开的规范和工具说明。小米原厂 HAL、用户日志、设备标识与本地回归产物必须放在被 Git 忽略的 `compatibility_archive/`，不得进入公开模块 ZIP 或 GitHub。

## 证据等级

| 等级 | 含义 |
|---|---|
| `runtime-verified` | 在目标 ROM 实机完成开机、全局、白名单、自定义槽、模式往返和稳态检查 |
| `static-verified` | 已取得实际 HAL 并完成 SHA256、Build ID、ELF、函数和字符串特征分析，但未在目标 ROM 实机运行 |
| `log-only` | 只有可信日志和系统身份，没有对应 HAL 或实机 |
| `unassigned` | 文件存在，但设备/ROM/是否被修改无法可靠确认 |

等级只能随证据增加而提升。不能根据文件名、大小相近或函数偏移相近自行猜测 ROM。

## 固定目录结构

```text
compatibility_archive/
  catalog.json
  CHECKSUMS.sha256
  devices/<model>__<device>/<full-rom-version>/
    manifest.json
    evidence.md
    originals/hal/
    originals/logs/<module-version>/
    originals/runtime/
    derived/hal_fingerprint.json
    derived/regression-result.md
  unassigned/<sha256-prefix>/
    manifest.json
    originals/
    derived/
```

`originals/` 中的文件只复制、不修改。重命名后的用途说明写入 `manifest.json`，同时保留 `original_name` 和 `source_path`。所有分析结果写入 `derived/`。

## 新 ROM 必采数据

1. `ro.product.model`、`ro.product.device`、Android API、完整 `ro.mi.os.version.incremental`、vendor/system build 版本。
2. `pidof` 得到的 audio HAL 服务名，以及 `/proc/<pid>/maps` 中全部 `audio.primary.*.so` 映射。
3. 从实际映射路径提取的 HAL 原件；不能用另一个文件名相似的副本代替。
4. HAL SHA256、文件大小、GNU Build ID、全部 `PT_LOAD`、`is_A2H_app`、官方六串偏移、stock/global/stub 签名命中数和安全页尾容量。
5. 当前模块版本、KernelSU 与 Zygisk Next 版本、模式、10 槽文本和开关状态。
6. 首次开机、无 hint 冷启动、全局、白名单、自定义第 7-10 槽、全局/白名单往返、至少一个 watcher 周期后的日志。
7. 实际功能结果必须与日志结果分开记录；“日志 OK”不能替代体感/设备功能验证。
8. 归档完成后重新生成并验证 `CHECKSUMS.sha256`。

## 工具

```text
python tools/new_compat_case.py compatibility_archive --model 25060RK16C --device dali --rom OS3.0.302.0.WONCNXM --evidence-level runtime-verified
python tools/hal_fingerprint.py path/to/audio.primary.mediatek.so --output derived/hal_fingerprint.json
python tools/archive_catalog.py compatibility_archive
python tools/archive_checksums.py write compatibility_archive
python tools/archive_checksums.py verify compatibility_archive
```

HAL 指纹工具只读取文件，输出稳定的结构化 JSON。校验工具覆盖档案内除 `CHECKSUMS.sha256` 自身以外的所有文件，可发现丢失、误改和未登记文件。

## 固定归档流程

1. 先用 `new_compat_case.py` 建空档案，禁止复制旧版本目录后直接改名。
2. 先保存系统身份和实际 maps，再从 maps 中显示的路径提取 HAL；文件名相似不能作为归属证据。
3. 原件放入 `originals/`，禁止反编译器、十六进制编辑器或格式化工具覆盖原件。
4. 对 HAL 运行 `hal_fingerprint.py`，将静态结论写入 `derived/`。
5. 在 `evidence.md` 中分别记录“日志判定”和“实机体感结果”，并列出所有缺口。
6. 来源不明、疑似修改、架构不同或只与日志特征相似的文件进入 `unassigned/`。
7. 运行 `archive_catalog.py` 重建检索目录，再运行 `archive_checksums.py write`。
8. 连续运行两遍 `archive_checksums.py verify`，两遍都通过后才算归档完成。

证据补齐后可以提升等级，但不能删除旧日志或改写旧结论。模块发布 ZIP 放在 `module_releases/`，并标注正式版、历史中间版和来源路径；它们不得混入设备 ROM 档案。
