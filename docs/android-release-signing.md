# Android 正式签名身份

2026-10-11，用户授权创建 mdo 的长期正式签名。2026-11-02 按用户要求，
已将私有材料迁入项目内的 `D:\GIT\mdo\.private\android-signing`。
`.private/` 已加入 Git 忽略；这是长期材料，不能作为 `.build` 构建缓存清理。

| 项目 | 值 |
| --- | --- |
| 密钥库 | `D:\GIT\mdo\.private\android-signing\mdo-release.p12` |
| 别名 | `mdo-release` |
| 算法 | RSA 4096 / SHA256withRSA |
| 有效期 | 10,000 天 |
| 证书 SHA-256 | `94b1947b60f3ce0852b19d9c37a47cc4a084fe9d601692c6a1f6b204c2174e84` |

公开证书和记录见 [DER 证书](releases/mdo-android-release.der) 与
[身份记录](releases/android-signing-2026-10-11.json)。私有目录内的
`signing-private.json` 保存密码，构建时读入临时环境变量，使用已有构建器的
`--keystore`、`--key-alias` 和 `--store-password-env` 参数；不得把密码传入
命令行、发布包、Git 或日志。Windows ACL 限制为当前用户、管理员及 SYSTEM。

请随项目保留整个私有目录，并保护备份的访问权限。不要将正式签名材料放到项目
外或临时构建目录；发布、清理、迁移项目时必须保留 `.private/android-signing`。
迁移时已验证密钥库可读取，证书与已发布 APK 一致，原项目外文件已移除。
后续精简版与完整版必须共用
此密钥，不能在清理缓存、换电脑或升级工具链时重新生成。

这套密钥与旧开发签名不同，不能直接覆盖旧签名的安装。首次迁移需要先导出并
核对会话及附件，再重装并导入；卸载前保全需要保留的数据。新证书还需登记到
网站的 Android App Links 关联。本次只创建签名身份，没有修改手机安装或网站，
也没有据此声明正式 APK 已构建、升级或发布。
