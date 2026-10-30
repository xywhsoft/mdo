# 知识库

知识库是带索引的文档目录。设置 → 扩展能力 → 知识库支持本机/商店列表、搜索、筛选、分页、详情、目录文件管理和 ZIP 分发。它与项目文档共用真实文件，Agent 用已有文件工具按需查看资料。

## 目录与索引

```text
knowledge/xrt-reference/
  extension.json
  knowledge.json
  README.md
  index.md
  reference/runtime.md
  assets/diagram.png
```

`README.md` 用于项目说明；默认索引是 `index.md`。索引中每节包含用途说明和对应文件路径，帮助模型快速定位：

```markdown
# XRT 文档索引

## 生命周期与内存
说明初始化、对象所有权和内存释放约定。
文档：reference/runtime.md

## 示例
按模块给出最小可运行示例。
文档：examples/index.md
```

外层清单：

```json
{
  "kind":"knowledge", "id":"knowledge:acme.xrt", "name":"XRT 开发参考",
  "alias":"xrt-reference", "entry":"knowledge.json", "readme":"README.md",
  "platforms":["any"], "dependencies":[]
}
```

`knowledge.json`：

```json
{
  "format":"mdo.knowledge.v1",
  "index":"index.md",
  "prompt":"涉及 XRT 时参考 {$path}，通过 index.md 定位文档。",
  "inject_prompt":true
}
```

`index` 可以指定另一个真实 UTF-8 文本文件。必须非空、可读且不超过 4 MiB。文档、图片、PDF、隐藏文件和空目录按普通目录 ZIP 分发；不把文件编码塞进 JSON。格式、路径和容量边界见[目录商店](ecosystem-store.md)。

## 依赖与使用

知识库只依赖其他知识库，关系写在 `extension.json.dependencies`，不重复写入 `knowledge.json`。例如 mdo 库可依赖 XServer 库，XServer 再依赖 XRT；直接使用 XRT 接口的 mdo 文档也可显式依赖 XRT，宿主去重。单纯相关资料可以保留正文链接，或声明为可选依赖。

详情显示依赖、必需/可选、说明、状态和直接使用者；平台详情可以点击跳转。停用基础库前会显示包含传递使用者的计划；卸载只删除所选库，使用者保留但停用。商店更新检查整组内容并恢复原启用选择。

`{$path}` 变为该设备的 `@knowledge/<directory_id>`。移动 Home 无需修改文档。这个明确挂载继承给 SubAgent，仍受文件工具权限和项目选择限制，不开放其他目录。

每库提示最多 8 KiB，注入条目总预算 128 KiB。宿主先为完整必需集合保留 ID、名称、路径和索引，再填充提示，父库不会缺少依赖定位信息而单独出现。正文不整体进入上下文。索引读取另有合计 16 MiB 预算，超限报告不可用。

关闭注入、项目停用、索引缺失或必需集合无法进入预算时，不注入受影响集合；明确依赖该集合的角色或工具在执行前被阻止。运行中的每次模型派发和工具执行也会重检，修复后可重试。

## 管理接口

- `GET /api/v1/extensions/knowledge`：列表；返回稳定 `id` 和物理 `directory_id`。
- `GET /api/v1/extensions/knowledge/<id或directory_id>`：清单、整目录修订、依赖详情和使用者。
- `PUT` 同一路径：`manifest`、有界 `edits`、`expected_revision`；二进制编辑通过原始文件暂存任务引用。
- `GET .../files`、`GET .../file?path=...`：分页文件列表和预览。
- `POST /api/v1/ecosystem`：安装、更新、启用、停用、卸载的计划与应用。

本机编辑、商店不可变性、事务回执、项目选择、迁移与平台接口见[扩展身份与依赖](extension-dependencies.md)。
