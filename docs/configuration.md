# mdo 配置合同

mdo 配置 schema v1 由一份内置基线、三份可选用户 patch 和一层进程期覆盖组成。内置基线在外部 Home 挂载之前从应用 VFS 读取，因此外部同名文件不能改变产品基线。

```text
内置 config/defaults.json
  + mdo-home/config/settings.json
  + mdo-home/config/models.json
  + mdo-home/config/permissions.json
  + MDO_CONFIG_OVERRIDE
  + --config-override <json>
  = effective config
```

后三个文件采用同一 envelope：

```json
{
  "schema_version": 1,
  "patch": {
    "appearance": {
      "theme": "dark"
    }
  }
}
```

对象按键递归合并，数组和标量整体替换。envelope 的未知键会被拒绝；`patch` 内的未知键会被保留并参与导出，便于后续版本和外接模块扩展。保存前会删去与内置基线相同的值，因此磁盘只记录用户修改。环境覆盖先应用，命令行覆盖后应用；两者只影响当前进程，不写入 Home。

所有读取、预览和提交经过同一 schema v1 验证器。保存使用同目录临时文件、flush 和原子替换；已有文件在替换或恢复之前复制为 `.bak`。写入失败时继续使用已发布的内存配置，原文件保持不变。只读介质上的导入失败，不会把 ephemeral 配置伪装成已保存。

普通 JSON 配置不能保存 `api_key`、token、password、client secret、private key 或 Authorization 等敏感值。模型凭据只能保存为 `secret_ref`，v1 接受 `env:`、`file:`、`keychain:` 和 `prompt:` 引用。secret resolver 在使用模型时解析引用，配置导入和导出始终只处理引用文本。

`ling-3.0-tiny` 是内置、免费、不可编辑且不可删除的模型。服务端验证器逐字段核对其完整 descriptor，并确认默认模型仍存在；前端禁用控件只是交互提示，不承担保护职责。内置 descriptor 声明 OpenAI Chat Completions、OpenAI Responses 和 Anthropic Messages 三种线上协议。真实线上验证只有在运行环境同时提供三条显式 URL 和临时 key 时执行，离线 fixture 不作为线上成功证据。

`MdoConfigPreviewImport`、`MdoConfigImport`、`MdoConfigPreviewRestore` 和 `MdoConfigRestore` 为设置页提供预览后提交流程。预览不创建 Home；导出结果仍是可导入 envelope。`MdoConfigEffectiveJson` 返回当前完整有效配置的拥有式快照，调用方用 `xrtFree` 释放。
