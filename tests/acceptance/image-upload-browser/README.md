# 实际打包页面的图片恢复与续聊

基线 `7d03111`。本阶段验证上一阶段的图片/API 修复在完整工作台中的实际效果，没有修改产品代码。候选使用隔离构建，摘要 `848689fa64bfc6189f7115d3e6bbcaa3f1ddbec127d69e1bfda86c3f170d5071`；两项实际 HTTP 服务模块与候选构建时的源码摘要一致，见 `source-parity.json`。

新增 `manual_image_upload_deadline_qa.py`：真实单文件程序、独立便携 Home、正式设置 API 配置的回环图片模型、真实 WebSocket 隧道和响应延迟代理。选择已有公开测试图片 `tests/acceptance/xge-pixel-editor/smoke_window.png`，不使用个人文件、线上账号或外部模型。

```powershell
python tests/manual_image_upload_deadline_qa.py --packed .build/conversation-acceptance/image-upload-deadline-package/mdo.exe --directory .build/conversation-acceptance/image-upload-browser-native-v2
# 完成首轮后，只恢复此夹具已经完成且程序摘要匹配的原 Home
python tests/manual_image_upload_deadline_qa.py --packed .build/conversation-acceptance/image-upload-deadline-package/mdo.exe --directory .build/conversation-acceptance/image-upload-browser-native-v2 --resume
```

## 已验证

通过实际文件选择器添加 PNG，真实 PUT 已保存原件，代理交付 201 响应头后将正文保留 43 秒。发送栏显示正在保存，输入框仍可输入；首次确认在保存后 31.125 秒发生，两次 GET 503 按退避恢复，在 37.766 秒确认成功，早于旧正文交付。实际只有一次上传、一份原件和元数据。

发送修正并核对后的单份多行文字与图片，页面正常收到回复；立即续聊再次成功。第一轮模型实际收到一张图片，其摘要与上传原件完全相同，后续文字回合没有重新附加图片。原生事件各有一条对应用户输入，两个正常完成、零最终模型错误、空队列。随后保存包含中文和换行的未发送草稿。

重载时首实例恰好达到预定十分钟上限并正常退出；历史和草稿虽已载入，连接及发送状态没有作为通过证据。随后用原 Home 显式重启、更新回环模型端点，实际页面重新连接：图片历史和原草稿完整，焦点在输入框、发送可用。点击发送原草稿，第三轮成功。首轮 `proof.json` 与重启后的 `proof-resumed.json` 共同证明三次模型调用、三条准确输入、零最终错误；附件字节不变，最终草稿/队列清空。`restarted-draft.png`、`restarted-followup.png` 与两份 browser JSON 记录真实页面状态。最后重启页面的警告/错误日志为空。

夹具初始化曾把配置里的 attachments 写成整数（目录 DTO 用位图，但配置需要字符串列表），正式 schema 拒绝；改为 `["image"]` 后按正式配置流程启动成功。这是夹具错误，没有放宽产品校验。

## 仍需定位的输入问题

第一次自动化填入多行测试文字时出现两份文字及额外组合输入，`upload-typing.png` 保存了现场。这是在附加事件记录之前发生的，不能据此确定是产品草稿同步、浏览器输入驱动还是实际输入法干扰。该重复输入没有发送。

之后临时记录 input/组合事件及 prompt 的程序赋值，英文、中文和相同多行文本复测均只有一次 input；发送前的值和最终服务端记录都正确。记录中的输入为自动化合成事件（trusted=false），未覆盖真实输入法组合过程。程序赋值记录只显示成功提交后的正常清空。详见 `input-observations.json`。没有足够证据修复或排除最初问题，后续优先验证可信组合输入与上传/草稿更新同时发生的路径。

临时浏览器日志随重载和关闭移除，没有给产品写入测试代码。两个原生实例均已确认退出，临时页已关闭；未进行压力或高负载测试，未更新日用程序、手机或官网。Android 文件选择/输入法、系统剪贴板和实际导出落盘仍待独立验收，长期任务继续。
