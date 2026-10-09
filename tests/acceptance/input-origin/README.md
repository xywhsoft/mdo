# 对话输入来源记录

本阶段没有修改产品输入逻辑。使用上一阶段的真实 Windows 候选、独立 Home、正式 HTTP/WS、草稿 API 和一次回环模型请求，进一步排查此前的额外文字现象。

`manual_input_origin_qa.py` 仅在隔离代理中加入启动前观察器和已有 `prompt.value = …` 赋值标记。没有覆盖文本框访问器，没有注入 input/composition 事件，也没有替换草稿或模型响应逻辑。观察器记录事件可信标记、输入类型、长度、指纹、光标、焦点和赋值调用栈，不记录文本正文。第一次夹具因 CRLF 敏感的赋值标记导致导入失败，已停止并修正；该失败来自验收夹具，不作为产品缺陷。修正后的实例正常启动，代理提供的五处赋值标记及 JavaScript 语法均已核查。

得到的明确事实：

- 冷启动恢复 24 字符的已知草稿之后，在本轮任何 fill/type 操作之前，文本框收到可信的 `compositionstart`、`insertCompositionText` 的 beforeinput/input，长度依次为 25、26、25。期间没有任何产品文本赋值。组合结束事件的可信标记为 false，保留原始记录，不推断物理键盘或输入法的具体来源。
- 本轮明确执行的填入表现为不可信 paste/input，与上面的组合输入不同。英文已知草稿经过设置和计划任务往返保持不变；中文多行草稿通过真实保存、重新载入恢复，指纹相同。没有复现草稿重复拼接。
- 发送前核对正文，只发送已知的 `INPUT_ORIGIN_CONFIRMED`。原生记录和模型调用均只有一次该输入，回复成功，最终草稿和队列为空，零最终模型错误及当前实例错误提示。

`events.jsonl` 每行一个原始事件，document 0 为冷启动、document 1 为重新载入。`native.json` 来自实际服务，`browser.json` 保留长度观察与完成状态。日志按当前实例来源筛选；前一个已停止的错误夹具记录仅保留在 `.build` 中。`checks.json` 汇总校验，`continued.png` 展示成功续聊。

这把本次额外文字定位到浏览器组合输入通道，尚不能证明物理来源，更不能证明所有早先重复现象都同源。没有加入去重、删除用户文字或阻止可信组合事件的补丁。桌面原生 WebView、Android 输入法与真正持续组合过程中草稿返回的情形仍需后续验证。

复现：`python -X utf8 tests/manual_input_origin_qa.py --packed <mdo.exe> --directory <新的隔离目录>`，使用打印的只含测试数据的回环地址。正常或冷启动后先读取外层 `#input-events`，再编辑、往返和续聊；退出前保存事件并创建打印的 stop 文件。候选为 `.build/conversation-acceptance/locale-gate/package/mdo.exe`。实例寿命最多十分钟，没有压力/高负载测试，没有修改日常程序、手机或官网。
