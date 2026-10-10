# 当前来源的正式 Windows 发布门禁

2026-10-11。从锁定 xs `f05f7661062fdca04da8d571ad859eb4b87c0889` 源码重编译
精简宿主，没有使用 `--skip-host-build`。在独立工作区完成完整有界门禁，退出码 0。

- Python：150 项，3 项环境跳过。
- 前端：1002 项通过，零失败、零跳过；165 个模块通过语法检查。
- 严格 C11 编译、82 项原生运行探针、12 项实际打包程序探针通过。
- Windows 便携 Home/移动/覆盖路径检查及 20 秒打包启动检查通过。
- 两次实际打包 SHA-256 相同，应用、锁定依赖、宿主配置与宿主字节在门禁中未变。

[核对结果](proof.json) 保留候选、宿主和日志摘要；[完整日志](release-gate.txt)
保留实际执行记录。这一门禁不包含 Android 设备操作或系统复制粘贴。

输入基于本轮已在 Windows/Android/Linux 验收的 431 个应用文件，包含另一批
未提交修改；只在独立副本中将候选构建号设为 30000065–30000072。
没有将这些副本冒称纯净提交，也没有覆盖原仓库的发布配置、日用程序或 Home。
Windows/Android 之前完成的系统交接见各自 `current-*-native` 记录。
Linux 系统复制与保存按用户安排后置；本项也不代表各平台候选已上线。

```powershell
$env:MDO_XADMIN_ROOT = 'D:\GIT\x-admin'
$env:MDO_TEST_WEBSITE_REPO = 'D:\GIT\home'
$env:MDO_ECOSYSTEM_SITE_ROOT = 'D:\GIT\home\host\xywhsoft_ai'
python tools/qa_release.py --xserver-root <锁定 xs 副本>
```

运行前需要按 `tools/runtime/catalog.json` 验证工具素材并准备两个工具包夹具。
本次校验 452 个 Windows 素材文件，重新生成归档后再逐一核对大小和摘要。
只使用隔离副本与本地模型；没有压力、高负载或 soak 检查，没有官网发布。
