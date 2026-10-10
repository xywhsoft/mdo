# 当前 ARM64 APK 的真机对话与系统接口验收

2026-10-11，RMX5090 / Android 16（API 36）/ ARM64 / WebView 138.0.7204.179。
使用独立 `org.xleaves.mdo.qa.stability`，版本码 30000064。实际安装 APK 与候选
逐字节一致：4,199,661 字节，SHA-256
`2f74b8455953d48f9680bd43663ffa83a35c7411dca547acc8cb7549985f660d`。
设备上原 QA 包已不存在，因此本次是全新安装，不能称为升级保留数据验收。
没有操作日用包、个人会话、生产账号或真实模型；没有压力或高负载测试。

应用输入是当前工作区的 431 个文件，包含未提交源码；摘要
`82cb0b6354b30226a4d69dead949fe36df061ee86ed782d1e3c34d3b0a8945e2` 与
[当前集成候选](../current-integration/README.md) 和 Linux 补验相同。
验收结束时所有输入摘要仍一致。ARM64 宿主来自锁定 xs `f05f766`，完整安装前
检查、签名和 16 KiB 对齐见该集成记录；本目录补充实际设备结果。

## 真实 C/TCC 对话路径

使用 [有界 ARM64 入口](../../test_android_conversation_runtime.py)，通过正式
HTTP API 调用手机内的 C/TCC 服务，模型由本地回环夹具提供：

| 情况 | 实际模型请求 | 结果 |
| --- | ---: | --- |
| 临时 429 | 3 | 自动退避后完成，零最终错误 |
| 未交付内容时断开连接 | 2 | 自动重试后完成，零最终错误 |
| 流式缺少结束标记 | 2 | 丢弃失败草稿后恢复，零最终错误 |
| 每日额度不足 | 1 | 仅一条 `daily_token_limit` 最终错误，不重试 |
| 在 10 秒退避中停止 | 1，随后显式继续 1 | 97 毫秒内取消，取消后不继续重试；明确继续成功 |
| 普通续聊与进程重启后续聊 | 各 1 | 历史、草稿及 Android 凭据保留，零最终错误 |

共 12 次有界模型请求，原输入在继续和重启后的上下文中各出现一次。
详见 [原生 API 记录](conversation-runtime.json)。此入口不模拟手机控件操作，
下列系统检查独立完成。

## 手机页面和系统交接

使用 [原生 UI 入口](../../manual_android_ui_qa.py) 建立一个合成回合，然后通过
真实手机截图、触摸、系统粘贴、系统选图和文件窗口操作完成：

- 点击回复复制按钮，在 Android 输入框粘贴；中英俄六行文本与原回复完全一致，
  包括空行和末尾换行。见 [粘贴后](clipboard-pasted.png) 与
  [实际草稿](clipboard-draft.json)。
- 回复后在实际模型面板从 QA Text 切换为 QA Image，保留原草稿。见
  [已切换](model-selected.png) 与 [保存后的会话](selected-session.json)。
- 在系统选图器搜索 `pixel`，选择唯一的合成 xge PNG，再确认添加。见
  [筛选结果](picker-results.png)。按 Home 后返回同一 Activity，原文字及图片
  保持不变，见 [回到前台](foreground-return.png)、[返回前草稿](image-draft.json)
  与 [返回后草稿](foreground-draft.json)。本项不代表长时间后台执行验收。
- 点击实际发送按钮，模型仅收到一次图片请求并成功完成，草稿清空。图片摘要
  `c44ad1ad286fbbce7b20e7c9e2941b077594eab5acf96d6c5ca116ced2e25047`
  与 [xge 测试图片](../xge-pixel-editor/smoke_window.png) 完全一致。
  见 [执行事件](image-sent-events.json) 和 [模型记录](native-runtime.json)。
- 在 Android 文件保存窗口创建仅供本次验收的目录
  `/sdcard/Download/mdo-stability-qa-20261011-current`，实际保存 Markdown 与 JSON。
  保存完成后的文件通过 ADB 读回，Markdown 为 21,258 字节，JSON 为 68,554 字节；
  两者保留多语文本和同一张图片，见 [系统目录](backup-saved.png) 及
  [原始导出](exports/)。JSON 文件名被系统提供器按 50 个字符截短为
  `mdo-session-V-OSVBQljYSO_k_36nlBTNuVaH1VyAAP.backu.json`，内容及扩展名完整。
- 从实际“导入会话备份”菜单调用系统文件选择器，选取刚保存的 JSON。
  [预览](import-preview.png) 显示 9 个文件、10 条历史和 1 张图片；核对目标后
  [确认恢复](import-restored.png)，得到独立会话
  `28893f1aff8c82bf4a31f5a186d1312f`。全部 9 个文件的长度和 SHA-256 正确；
  恢复后的原 10 条记录只变更会话编号，其余字段逐项一致。附件原字节完整。
- 打开恢复会话，在真实输入框输入 `after-native-restore-current` 并点击发送。
  模型仅收到一次请求，完成后共有 15 条记录，无错误，见
  [手机续聊](restored-continued-final.png)、[执行事件](restored-events.json) 和
  [恢复后的原始历史](restored-ui-events.jsonl)。无需先更换模型或重启程序。

以上 UI 夹具共 3 次模型请求（初始基准、图片发送、恢复后继续）。
[核对摘要](proof.json) 包含安装、原始导出、各文件摘要及内容核对结果。
系统相册初始页面的个人缩略图没有收入仓库；本目录图片只包含合成会话与测试文件。

## 验收入口修补及范围

工作区新增的本地来源校验要求 Host 与 Origin 对应手机监听地址。ADB 的 PC 转发
端口不同，原夹具因此收到 403；两个入口现在显式使用手机的真实监听地址，
不修改产品校验规则。最初启动等待也未满足：已打开的 Activity 不会再写入新的
监听日志。入口现在先结束自己负责的 QA 进程，再等待新启动；保留应用数据。
从已运行 QA 包开始的 [一次短检查](startup-from-running.json) 退出码为 0，
基准模型仅调用一次。本次总计 16 次成功验收所需的模型请求，启动和来源校验的
早期失败尝试没有调用模型。

所有夹具已结束，自己的 ADB forward/reverse 已清除，QA 进程已停止。
这里不覆盖实体外接键盘、长时间后台运行、其他手机、Windows/Linux 系统剪贴板
及保存窗口，也不覆盖正式发布。较早的语言和组合键验证保留在
[上一批冻结包记录](../android-restore-locale/README.md)，没有算作本次新包重跑。
