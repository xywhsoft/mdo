# 2026-10-09 八版本重新编译

八份单文件产物均放在仓库根目录，版本为 `0.1.0-dev`。根目录 `mdo-builds.json` 保存构建号、字节数、SHA-256、依赖版本及验证范围。

本次先重新编译 Windows GUI/服务、Android ARM64 及 Linux glibc/musl GUI/服务的原生宿主，最后复用这些已核验宿主，将所有产品重新打包到同一份稳定的应用源码快照。构建期间新增的搜索错误恢复代码已经包含在最终产物中。记录时 HEAD 为 `3b852b7d1849b7c06f73e41dd0cf1d42dda2194d`，还包含工作区内未提交的扩展工具和通知等改动；不能仅以 HEAD 表示完整输入。

共同应用文件为 414 个，每份应用包包含 418 个文件；差异为平台配置与生成的编译定义。应用源文件指纹为 `e63759338a5b671048beae6a1f3b9fff3e70ff651267bd76ec62db2b7e60599b`。已用正式 xs 解包，并逐文件核对打包暂存目录、共同应用源码与最终产物一致。完整源文件指纹、产物回执、测试日志和截图保存在 `.build/rebuild-all-20261009/`。

依赖为 xs `153315a092b8c37d9d79f090122f81ed2bcc139d`、xrt `6040abda647bd947bd561cd185f37a3751f122f4`。通过 `deps.lock` 验证的独立 xs 检出位于 `.build/locked-xs/153315a092b8c37d9d79f090122f81ed2bcc139d`。Linux 使用 Debian 10、GCC 8.3、glibc 2.28 基线和对应 musl 工具链，实际 ELF 版本导入检查通过。Windows GUI 与服务版共享 Windows 构建号，以产品 edition 区分。

| 产品 | 根目录文件 | 构建号 | 字节数 | SHA-256 |
| --- | --- | ---: | ---: | --- |
| Windows App | `mdo.exe` | 30000047 | 4510545 | `90873258d0164f8d4e94ece3734d02c454cd4ad5a4e1220e45570a2c1be9d013` |
| Windows 服务 | `mdo-server.exe` | 30000047 | 4316325 | `c5eea8ac1cf24bdf07d2c6bf9bc78808ecb1886fa8f1a604f650a4747122c192` |
| Linux glibc App | `mdo-linux-x86_64-glibc` | 30000048 | 4057372 | `000e244369e9e08748d11b84672173ab2424f26698cb706f8b1fb145c88a6e1d` |
| Linux glibc 服务 | `mdo-server-linux-x86_64-glibc` | 30000049 | 4028465 | `1c4d81ccb9fedd0aeeaad2752b2daae7aafa0fdff4e25fab0e7853d833efe5a8` |
| Linux musl App | `mdo-linux-x86_64-musl` | 30000050 | 5899027 | `ce7f4234cad63e0992d40afd785ad339671e7abe08b4f9600d667f702997975d` |
| Linux musl 服务 | `mdo-server-linux-x86_64-musl` | 30000051 | 5870185 | `d81f8600f3b8ee6cd1d55a089e41663ffaeaaebb29d0d2746884ce17fc09a2d3` |
| Android App（精简） | `mdo-arm64-v8a.apk` | 30000052 | 4138221 | `bbb9ab14249a85e9de2f4cb02705645b91860eb3cd3fa78dd6e5c171f3e1f84a` |
| Android Full App | `mdo-full-arm64-v8a.apk` | 30000053 | 60891982 | `21e38c0fe23c61ba916d30ee7ee829bba3db5d6b189301075f838ef7185f5102` |

## 验证

- Windows GUI：AMD64 PE、图形子系统、单文件启动、bootstrap 与写令牌、便携 WebView2 目录、自动关窗并正常退出。
- Windows 服务及 Linux 四版：打包 TCC 启动、bootstrap 与写令牌、项目 API、项目保存、正常停机及重启后持久化读取；全部通过。
- Linux：glibc 版本实际最高导入为 2.28；musl 核心无 ELF 解释器及动态依赖。两份 GUI 在现代 WebKitGTK 4.1 环境验证真实前端和关窗停服，musl GUI 另在 Debian 10 / WebKitGTK 4.0 环境验证通过。
- musl 静态链接只适用于核心；Linux GUI 的内嵌窗口辅助程序仍使用 glibc，并需要系统 GTK3 和 WebKitGTK 4.1 或 4.0（至少 2.30）。纯服务版没有此依赖。
- Android：两份 APK 的版本、edition、ZIP CRC、ARM64 ABI、原生依赖闭包、工具映射、v2/v3 签名及 16 KiB ELF/ZIP 对齐检查通过。精简版有 1 个原生库；完整版有 75 个原生文件、125 条工具映射。本轮未安装手机或执行真机测试。
- APK 沿用本机已有开发签名，证书 SHA-256 为 `f91d4271ee0b3a959699fd80e735232ca47852326000e2dc1464fddf2970183e`；未生成或替换签名密钥。
- 应用严格 C11 / `-Wall -Wextra -Werror` 语法检查通过；152 个前端模块解析及引用检查通过。Linux 构建契约在旧基线中通过 6 项，跳过 1 项仅 Windows 路径转换检查；该路径检查在 Windows 通过。第一次测试把临时目录放在 `.build` 内，导致“外部目录保护”测试的前提不成立；改用独立内存临时目录后通过，未更改测试或保护规则。
- 未执行压力、高负载测试，也未调用收费模型。

## 构建入口

Windows 使用 `tools/build_mdo.py --edition gui` 与 `--edition server`；Linux 使用 `tools/build_linux.py --wsl --baseline-root /root/.cache/mdo-linux-buster` 一次构建四版。Android 使用 `tools/build_android.py --edition both --wsl`，SDK 为 `/home/ubuntu/.cache/mdo-android-toolchain/sdk`，JDK 为同目录下 `java/usr/lib/jvm/java-1.17.0-openjdk-amd64`。均显式传入上述锁定 xs 路径。

本轮 D 盘一度耗尽，Android 完整版中间 ZIP 写入失败。恢复可用空间后重新完整打包、验签与解包校验。临时移到 C 盘的编译缓存及早期 Windows 测试文件已全部搬回仓库，C 盘临时缓存目录没有剩余文件；后续 Windows 验证临时目录使用仓库内 `.build`，Linux GUI 验证副本使用内存文件系统。
