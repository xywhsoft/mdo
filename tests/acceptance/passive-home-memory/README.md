# 被动读取、迁移与 Markdown 记忆验收

生产提交：`2d863e85219cf2b1bc39b5544ab08e36f5632818`。
宿主：deps.lock 中的 xs `f47b0de5037f458f6289483d1323dee417d99434`。
候选为 `.build/conversation-acceptance/passive-home/clean/mdo.exe`，来自干净提交，
4,534,910 字节。`candidate.json` 记录包哈希及四个改变的生产文件的逐字节核对。

`native.json` 来自真实 xs/TCC HTTP 路径，公告由回环 HTTP 服务提供，每个场景
先接收第一代、再正式刷新到第二代。它不是浏览器或 Android 实机验收，也不声称
干净包连接了该测试公告服务。干净包单独通过 Home 独占与内置模块目录启动测试。

四个隔离 Home 场景：首次使用、仅便携浏览器缓存、目标位于旧数据目录内、已有用户
文件。验证内容包括：

- 公告可见且更新；前两种 Home 保留导入资格，目录重叠准确拒绝导入而不创建目标。
- 来源保持一个原始配置文件，预览有效且前后完全相同，来源/浏览器/已有用户字节不变。
- 记忆 GET、HEAD、缺失或陈旧 If-Match 写入不创建记忆目录；已有用户 Home 正常保存公告。

记忆原生探针另覆盖旧条目在内存预览、已有 Markdown 优先、陈旧写入零副作用、
转换前后指纹相同，以及正文/索引已发布后完成标记写入失败和后续恢复。项目清理
协调器、HTTP 和持久回执探针同时覆盖 Markdown 目录及遗留 JSON/备份的清理，
其他项目、保留快照及用户工作区不受影响。

本阶段还通过完整 API 运行门禁、142 项 Python（3 项环境跳过）、124 项契约、
883 项 Node、157 个模块图解析、严格 GCC C11，以及独立的线上模型授权恢复 12 场景。
完整 API 夹具明确使用本地 provider；线上授权规则由独立测试覆盖，不在生产代码中绕过。
未运行压力/高负载测试，未更新日用程序、手机或官网。

复查命令（当前锁定宿主）：

```powershell
python tests/test_passive_home_runtime.py --host .build/conversation-acceptance/android-page-restore/host/xs.exe --evidence tests/acceptance/passive-home-memory/native.json
python tests/test_memory_runtime.py --host .build/conversation-acceptance/android-page-restore/host/xs.exe
python tests/test_api_runtime.py --host .build/conversation-acceptance/android-page-restore/host/xs.exe
python tests/acceptance/passive-home-memory/check_evidence.py --exe .build/conversation-acceptance/passive-home/clean/mdo.exe
```

下一阶段继续通用设置保存、交互中断/恢复和其他独立问题；本记录不是全功能稳定声明。
