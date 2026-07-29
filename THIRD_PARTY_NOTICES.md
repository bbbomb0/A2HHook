# 第三方组件说明

本项目默认构建与发布链不链接、打包或加载 Dobby，也不使用 Zygisk 注入库或 LD_PRELOAD 包装链。Dobby inline hook framework 仅供仓库中保留的 legacy/诊断构建选项使用。

- Project: Dobby
- Repository: https://github.com/jmpews/Dobby
- License: Apache License 2.0

仓库内包含一份供 `A2H_BUILD_LEGACY_INJECTION=ON` 使用的 `libs/libdobby.a` 与 `libs/include/dobby.h`；这些文件不进入默认模块 ZIP。启用该可选构建时，Dobby 仍受 Apache License 2.0 约束。Dobby 源码仓库和本地构建目录不提交到本仓库。
