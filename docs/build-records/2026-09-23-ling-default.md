# 2026-09-23 Ling 默认对话修复构建

修复提交：mdo `3ab7165`，锁定宿主：xserver `dbd86c7d7078d3978cd900b3b794260f2fa6e7c5`。根目录 `xs.exe`、`xsw.exe` 沿用此前重新构建的宿主，`mdo.exe` 已用当前源码重新打包。

新版模型客户端在未设置 `MDO_LING_*` 时使用随程序分发的 Ling 服务地址与公共访问令牌；非空环境变量仍可逐项覆盖。普通配置继续只保存 `secret_ref`，公开 API 不返回令牌。客户端令牌可被提取，服务端必须独立实施配额、滥用防护与轮换。

验证结果：

- 隔离的开发宿主在清除全部 `MDO_LING_*` 后，Responses、Chat Completions、Anthropic Messages 各完成一轮真实对话，均返回非空文本；
- 重新打包的单文件 `mdo.exe` 在独立临时目录、无 Ling 环境变量的条件下，默认协议完成“你好，你是谁？”真实对话；
- Windows 发布门禁通过 113 项检查、严格 GCC C11、16 个 xs/TCC 运行时探针、确定性 pack、单文件零写和 20 秒打包启动回归；未运行压力或高负载测试。

根目录 `mdo.exe` 大小 5,701,968 字节，SHA-256：`93d87afa7e2ba7b1ce1774574f43877110670faf0d2b467dcd70d223ee8efdc1`；它与门禁生成的 pack 逐字节一致。二进制按 `.gitignore` 不入库。
