# 分平台发行说明与压缩包读回

2026-10-11。发行工具曾对所有平台写入 Linux 说明，Windows 用户拿到的 ZIP
包含 `apt`、glibc 和 GTK 构建指引。现在根据产物平台选择 Windows 或 Linux
说明，保留可执行文件、便携目录及构建记录的原有布局。

使用本轮已经验收的真实 Windows GUI 与 Linux glibc GUI 程序生成 ZIP/tar.gz，
逐一读回压缩包：程序与输入逐字节相同，说明与对应平台文档逐字节相同；
ZIP CRC 正确，tar 中的 Linux 程序保留 `0755` 权限，归档摘要和大小与清单一致。
Windows 说明包含 `mdo.exe` 和 WebView2，不再包含 `apt install`。
见 [核对结果](proof.json)。

```powershell
python tools/package_products.py --dist <分别带有 build.json 的平台目录根路径>
```

隔离产物在 `.build/conversation-acceptance/platform-package-readme-20261011`。
本项只验证实际归档和说明，不冒称重新完成原生对话或正式发布验收；没有运行
个人实例、生产模型、压力或高负载检查，没有替换日用程序或发布官网。
