# mdo Skill 编写约定

Skill 是一个含 extension.json 和 UTF-8 `SKILL.md` 的目录，可带 references、scripts、templates、assets。
新 ID 推荐小写字母和连字符，最多 64 字符，避开 Windows 保留名与已有目录。
name 和 description 必填；SKILL.md 的 name 必须与根清单 alias 一致，目录 ID 独立。
不要填入密钥、令牌或私人会话原文。

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
目录资源遵循统一归档限制：最多 50,000 项、文件 512 MiB、目录合计 1 GiB；
平台每版本容量以账号额度为准，普通用户 10 MiB、创作者 100 MiB（含审核附件）。
路径不可用绝对路径、反斜线、冒号、.. 或符号链接；脚本不会因安装而自动执行。
声明了旧式 scripts/templates/assets 列表时，只能访问列表内资源，且文件必须存在。

## 安装范围

唯一安装目录是指定 mdo Home 下 `skills/<目录名或XID>/`。项目仅保存启停偏好。
其他程序的技能来源可显式导入完整目录；原目录不会作为 mdo 的运行时安装位置。
调用名称在根清单 alias 中声明，同名别名冲突需要用户处理，不能偷偷覆盖其他定义。
发布和导出使用整个目录的标准 ZIP，安装解压到一个目录，卸载移除整个目录。
