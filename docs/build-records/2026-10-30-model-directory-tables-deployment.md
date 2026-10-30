# 模型目录表格与套餐入口部署

2026-10-30 第二轮更新已上线。菜单为 **墨斗·模型与服务 → 模型目录**，菜单 ID 73、父分类 ID 59，路径及管理权限保持不变。[摘要与回执](2026-10-30-model-directory-tables-deployment.json)。

## 页面与数据

页面改为与既有模型管理一致的 `_component` + layui 表格，分为供应商、API 产品/套餐、模型参数三个视图。支持搜索、类型筛选、分页、列工具、表格内横纵滚动；编辑放进受视口约束的可滚动 layer 弹窗。组件复用现有加载器的 ID 命名空间、请求取消、关闭保护与表格清理。独立页面由组件源码生成，两种入口一致。

已补充到 11 个供应商、28 个产品入口，其中 15 个套餐入口，参数表共 38 条记录。产品类型、套餐 Key 说明及官方文档单独保存；共用 API 地址的套餐不伪造独立域名，也不按地址推断计费类型。未核实的套餐模型上限继续留空。

主要官方接入资料：

- [智谱 Coding Plan](https://docs.bigmodel.cn/cn/coding-plan/overview)、[Z.ai Coding Plan](https://docs.z.ai/devpack/overview)。
- [百炼 Coding Plan](https://www.alibabacloud.com/help/en/model-studio/coding-plan)、[国际端点示例](https://www.alibabacloud.com/help/en/model-studio/qwen-code)。
- [Kimi Code](https://www.kimi.com/code/docs/en/)。
- [MiniMax M Plan 接入](https://platform.minimax.io/docs/m-plan/other-tools)、[国内 Token Plan 模型说明](https://www.minimaxi.com/models/text/m3)。
- [火山方舟 Coding Plan](https://console.volcengine.com/ark/region:cn-beijing/docs/coding-plan-personal-ai-other-tools)。

## 验收

- 原生 C 验证目录结构、产品类型及来源字段、重复项、CSRF、原子保存、版本冲突，以及 XHR 组件/独立页面分流。
- 表格投影、搜索、套餐筛选、数据校验及组件资源测试通过；配套客户端 16 项相关前端测试通过。
- 本地浏览器使用真实 layui、组件及独立页，模拟后台接口，验证新增供应商、编辑套餐、编辑参数与思考档位联动、保存、分页、双标签隔离及关闭清理。未重复生成 DOM ID，关闭 B 后 A 的查询条件保留。
- 720px 视口下，编辑区高 610px、内容高 774px、滚动为 auto，弹窗底部在 690.5px；保存提示出现后分页栏底部为 704.375px。表格内容高于数据区，滚动及分页均可到达剩余内容。
- 线上服务 active，目录计数及 64 位版本指纹正确；新脚本、组件加载器、样式的实际 HTTP 摘要与部署源文件一致。
- 生产菜单标题为“模型目录”，匿名管理接口保持拒绝访问；客户端真实连接线上目录，取得 11 个供应商、28 个产品、15 个套餐入口及 Key 说明，未使用旧缓存、未发起模型对话。

## 部署及边界

本轮仅更新 9 个源码/数据文件，先安装资源和依赖，再切换模块并重启既有服务。私有备份为 `/home/xs/mdo-client-catalog-20261030-526d89de85`，含源码、数据库一致快照及切换日志。现场没有已保存的自定义目录文件，因此只更新初始目录；没有覆盖自定义目录或用户模型配置。

既有模型、渠道、路由、扩展目录、原生 XS/配置和 9 个客户端更新/官网响应核对保持原状。未发布客户端安装包。生产没有管理员登录或目录试写；认证后的页面交互和保存测试来自本地环境，不能表述为生产保存验收。

更新后需对整个后台执行 **Ctrl+F5**，旧组件已加载时会显示刷新提示，不主动丢弃其他标签的草稿。截图位于本机 `.build/client-catalog-tables-20261030/`，为本地验收界面。
