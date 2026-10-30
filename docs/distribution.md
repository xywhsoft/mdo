# 应用及扩展分发

应用版本与安装编号规则见 [版本编码](version-codes.md)。当前发布状态以 app/release.json、mdo-builds.json 和线上目录共同核对；本地编译不代表已发布。

应用更新、扩展更新使用全局下载任务窗口。应用更新提示直接打开详情，无稍后提醒；普通线上通知直接显示内容，可在本次运行中暂缓。提示使用运行期随机标识和通知 revision 隔离，不保存身份凭据。外部工具首次安装仅为推荐提示，不提供旧整体工具包更新。

九类生态统一 [整目录 ZIP](ecosystem-store.md)。下载与原始分片都验证长度、SHA-256，ZIP 展开再验证每个文件和类型配置，失败保留当前目录；不会把文件放进 JSON。源码及审核资料分开保存，私有连接凭据不在包中。

Android 精简版与完整版使用同应用 ID、相同签名和各自递增安装编号；完整版固定包含最初 8 个独立工具目录 ZIP：BusyBox、curl、jq、OpenSSH、aria2、ripgrep、7-Zip、Python。Git、SQLite CLI、Poppler、FFmpeg、ImageMagick 等后续商店工具不随 APK 打包。内置名单由 `tools/android/runtime_bundle.py` 明确维护，增加商店工具或素材目录不会自动扩大 APK。受 Android 执行限制，ELF 同时置于系统可安装 nativeLibraryDir，对应映射只在验证 APK 索引和摘要后生成。应用私有 Home 仍按 external-tools/<id> 存完整文件，派生链接不进入发布包。无需在 Java 端维护 revision 安装树。

APK 的外层 ZIP 读取器专门支持 Android 签名块；普通扩展 ZIP 校验不放宽。自动检查覆盖全部 ZIP/ELF 摘要、CRC、依赖闭包、16 KiB 对齐与实际入口。自动检查和 Linux 执行器测试不代表 Android 真机执行通过。

平台维护源是 D:/GIT/home/host/xywhsoft_ai/plugin/mdo，应用下载使用现有 updates 模块，生态只使用 extensions/catalog.db。账号、会话、计费和模型路由由原有模块管理，不迁移或删除这些数据。上线必须核对源文件及对象摘要，私有备份旧生态后再切换；其他任务修改须保留。
