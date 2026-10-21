# mdo Skill 编写约定

Skill 是一个含 UTF-8 `SKILL.md` 的目录，可带 references、scripts、templates、assets。
新 ID 推荐小写字母和连字符，最多 64 字符，避开 Windows 保留名与已有目录。
name 和 description 必填；优先让 name 与 ID 一致。不要填入密钥、令牌或私人会话原文。

## 最小结构

写清：何时使用、输入与输出、可执行步骤、验证依据、失败后怎样继续。
description 包含触发场景；步骤中引用真实工具名称和真实文件，不凭空承诺功能。
需要新增行为时先确认 mdo 是否支持，不要把未实现字段写进文件。

支持 `argument-hint`、`arguments`、`disable-model-invocation`、`user-invocable`。
`allowed-tools` / `tools` 声明依赖，不解除权限。context、agent、model、hooks、shell、
paths、disallowed-tools 等执行行为字段不受支持；正文中的 shell 替换不会执行。

## 参数

- `$ARGUMENTS` 保留整段原始输入；没用占位符时输入会附加到正文。
- `$0` / `$ARGUMENTS[0]` 是第一个参数，`$1` 是第二个；引号将含空格值作为一项。
- `arguments: [file, focus]` 把 `$file` / `$focus` 映射到前两个位置。
- 名称只能用 ASCII 字母、数字和下划线，首字符不能为数字，不能重复；ARGUMENTS 保留。
- 缺少具名参数变空，缺少位置参数保留原占位符。强制类型、必填、枚举校验放在工具/脚本里。
- 参数内容不执行、不递归替换。`${MDO_SKILL_DIR}` 指向加载后的真实资源目录。

## 资源和长度

SKILL.md 最多 128 KiB；输入参数最多 16 KiB；聊天内联展开还受 64 KiB 草稿限制。
保持正文短小，长资料放 references，模型可以分段读资源。
资源最多 128 个文件，深度最多 8，单文件 8 MiB、总计 16 MiB。
路径不可用绝对路径、反斜线、冒号、.. 或符号链接；脚本不会因安装而自动执行。
声明了旧式 scripts/templates/assets 列表时，只能访问列表内资源，且文件必须存在。

## 安装范围

项目新建：工作区 `.mdo/skills/<id>/`；个人新建：指定 mdo Home 下 `skills/<id>/`。
项目同名 Skill 覆盖个人定义；在兼容目录中 mdo 自己的目录优先。
个人同名定义覆盖内置内容。迁移、改名、覆盖、启停都应有明确用途，不能为修复一个
文件而改动其他项目、其他设备或无关资源。
