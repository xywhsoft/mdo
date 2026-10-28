# 目录扩展商店

九类扩展统一使用普通 ZIP。唯一实体为 `mdo-home/<kind>/<id>/`；公开安装的 id 是平台作品 XID，本机创作可用自己的目录名。`extension.json` 在 ZIP 根目录，不再包一层文件夹。目录中的 JSON 是配置，不能承载文件字节。

类型：`agents`、`subagents`、`tools`、`mcp`、`skills`、`commands`、`external-tools`、`knowledge`、`plugins`。

```json
{"kind":"mcp","name":"示例服务","alias":"example-service","entry":"mcp.json","platforms":["any"],"version":"1.0.0","description":"面向用户的说明","author":"作者","copyright":"版权声明","license":"MIT"}
```

alias 是调用名称，独立于安装目录 ID。entry 必须指向目录内的真实文件；程序、库、脚本、文档、隐藏文件和空目录一起分发。不接受链接、路径穿越、大小写/Unicode 别名碰撞、设备路径和加密 ZIP。支持 Store/Deflate，校验所有文件 CRC 和整个归档 SHA-256。归档最大 512 MiB，展开最大 1 GiB、单文件 512 MiB、最多 50,000 条路径；创作中心配额另行收紧。

客户端只保留一份已安装目录。下载任务暂存于 data/extension-tasks；事务暂存和恢复日志放 data/extensions，提交完成后清理。更新在目录停用且活动调用/进程释放后进行原子切换；失败保留旧目录。卸载删除整个扩展目录。原生插件仍被执行环境引用时，会诚实显示等待重启，重启后按目录摘要确认删除。

设置每类均提供本机/商店列表、筛选、分页、作者与版权、详情弹窗和固定底部按钮。商店内容禁止编辑、复制为本机作品、导出再次分发；本机创作支持完整目录编辑、ZIP 导入导出和投稿。项目只保存选择，源码仍在唯一 Home 目录。

平台公开 API：`/api/v1/mdo/extensions/catalog|detail|files|preview|archive`；创作 API：同前缀的 `creator|uploads`；后台：`/admin/api/mdo/extensions`。九类后台页面为 `/admin/mdo/extensions/<kind>`，使用 `_component + layui table`。网页商店 `/mdo/ecosystem`，创作中心 `/mdo/creator`。

普通作者默认 3 件作品，每版本 ZIP + 审核附件合计 10 MiB；创作者 20 件、100 MiB。支持资格和数量扩容申请、管理员审批或直接授予。申请公布及更新需提交源码/构建/权限说明或审核附件；新版本审核时旧版本继续公开。下架撤回公开及待审版本，审核附件仅作者与有权限的管理员可读。Cookie 写入验证 CSRF；Bearer 使用既有账户身份。

上传和下载传输原始二进制分片（最多 1 MiB）；JSON 只保存元数据和收据。封存后校验全部 ZIP 与类型入口，服务端不运行上传的程序。使用相同 nonce 重试未知结果；取消、24 小时过期回收和对象引用检查防止重复提交或误删共享文件。

本机 SDK：extension.list/read/files/current/fingerprint/create/save/import/install/enable/remove/pack 及 extension.file.*。保存核对整目录指纹；二进制资源通过原始文件任务引用。完整设计与安全/运行时规则见 [目录归档设计](extension-archive-design.md)。
