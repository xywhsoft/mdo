# 创作中心客户端接入

版本名称保持 0.5.0；新构建号 Windows 50000009/10、Linux glibc 50000011/12、Linux musl 50000013/14、Android 50000015/16。大于 6 MiB 的源码包要求最低构建号 50000009。

外部工具分享对话框支持私有保存、申请公布、固定版本来源链接、本机审核附件和构建安全说明。程序与源码附件分别流式上传，不自动收集其他文件；合计超过账号容量时在上传前终止。失败与取消会尝试取消暂存上传，重启仍保持原有显式重试规则。令牌始终由账号层管理。

其他八类配置资源通过原生导出后提交到同一作品模型。私有保存不会进入审核，公布时提交完整源码与审核说明。源码包采用分块上传；大源码下载通过鉴权分块读取并检查完整 SHA-256，避免突破原生 fetch 的 64 MiB 上限。专用 API 的输入、输出与安装回滚日志按有界的大包格式扩容，其他图片及普通 API 限额保持原值。插件文件校验使用 64 KiB 缓冲区流式计算哈希。

网页创作中心：<https://ai.xywhsoft.com/mdo/creator>。mdo 分享页提供跳转入口；网页登录通过网站自身的会话进行，不向 URL 或 JavaScript 暴露原生账号令牌。

主要回归：`tests/test_creator_center_runtime.py`、`tests/test_ecosystem_runtime.py`、`tests/test_ecosystem_nine_runtime.py`、`tests/test_ecosystem_package_v2.py`、`tests/test_cli_tools_runtime.py`。

构建产物和线上部署收据将在验证后追加。
