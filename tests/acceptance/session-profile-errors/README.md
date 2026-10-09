# 模型配置拒绝与修正后的续聊

2026-10-09，生产提交 `b66a734e`。干净候选
`.build/conversation-acceptance/profile-errors/clean/mdo.exe`，4,534,113 字节，
SHA-256 `0f16a405cf3645d28d3796d3af50a578f487ba42de362e08ae0dfe09d5c5c491`，
xs 锁定 `f47b0de5037f458f6289483d1323dee417d99434`。

## 行为和范围

原先模型缺失、协议或思考强度不支持、输出上限超限都归为
`session_profile_invalid`。校验处现在保留稳定的本地 `profile_*` 原因，
API 创建、切换、运行提交采用同一分类。另补上下文上限、本地模型密钥、
代理密码、CA 文件与接口地址的具体分类。公开提示只使用明确的操作建议；
技术诊断不反射到页面。中文、英文、俄文的最终提示和错误卡标题一致。

本地保留代码只在 HTTP 状态为零且模型错误为本地参数/凭据失败时生效；
实际上游 401、资源不足、取消、超时不会借用本地配置解释。xwork 已有事件
保留代码/状态/错误枚举，回放也能恢复分类，无扩展库 ABI 或记录格式变更。
永久配置拒绝立即结束，无退避或反复确认；可恢复网络/限流策略保持。

## 真实原生和页面证据

- `native.json`：七次实际 API 拒绝，分别覆盖创建时模型缺失、协议不支持、
  思考强度不支持、输出上限，修改时模型/思考拒绝，以及绑定队列模型缺失。
  每次有准确的 422 类别，未留下新会话、未改写原配置/历史、未生成运行回执。
  正式切换有效模型后续聊成功；两次回环请求的实际模型标识不同，零模型错误。
- `synchronized-native.json`：首次页面动作前已改变模型能力，真实页面及时
  同步并自动选用有效的低强度。它成功保存，无错误；这不是配置拒绝复现。
  对应 UI 观察记录在 `ui-observations.json` 的 `synchronized` 中。
- `browser-native.json`：第二个独立页面通过代理在用户的 PUT 已生成后，
  使用正式模型设置 API 更改其支持的强度，再转发原 PUT。原生真正返回
  `model_profile_reasoning_unsupported`，没有伪造错误响应或修改包内代码。
  页面仅一条“所选模型不支持此思考强度，请选择支持的强度。”，释放控件，
  返回原模型并保留完整中文输入；`rejected-native.json` 确认原修订/配置、
  一次已完成的种子对话和零模型错误事件。
- 恢复模型能力后，用户再次选择该模型；实际请求使用恢复后的默认“无思考”，
  与被拒绝的“中”不同。这是正式修正选项，不是自动重放永久拒绝。
  只有一个失败 PUT 和一个成功 PUT，修订 1→2；原输入发送一次，原消息仍在，
  两次正常完成事件，实际不同上游模型标识，两次模型请求、零最终错误。
  `rejected.jpg`、`continued.jpg` 为真实页面截图。

瞬时保存成功提示的观察未匹配到预期文本；随后读取当前页面和已确认元数据，
证明操作成功，没有因此重新提交、重启验收或改写记录。

三个最终干净包实例合计五次回环模型请求。拒绝阶段不改变 snapshot，
后续正常对话会追加历史，不能宣称续聊后的 snapshot 哈希仍不变。
所有所属进程和临时浏览器标签均已结束。未替换日用程序、安装 APK 或发布官网。
模型服务为回环夹具，没有消耗外部额度，没有压力或高负载测试。

## 验证与复现

119 项相关 Node、63 项会话/API/前端/构建契约、157 个模块图、严格
C11/Wall/Wextra/Werror 通过。模型目录检查实际触发无效协议、思考、输出、
CA、代理密码、缺失/损坏内建凭据及接口换行；Agent 检查补上下文上限拒绝；
独立会话、26 个退避/业务/取消/流恢复案例、结构化错误回放检查通过。
九种本地分类另核对无重试、无私有诊断，上游/资源/取消分类不受影响。
新增 422 的前端恢复检查证明只一次提交，不触发确认读取或重试。

完整旧 `test_api_runtime.py` 尚未通过：已修正发布版本号的过时断言，随后
迁移预览出现源变化/冲突；单独主组则发现旧“记忆列表为空”预期与内建
`MEMORY.md` 不同。这些独立差异仍待核查，本阶段不将定向验收等同完整回归。
所有九种提示没有逐一实际浏览器故障验证；页面实测为上述配置竞态和续聊。

```powershell
python tests/test_session_profile_errors_runtime.py --packed .build/conversation-acceptance/profile-errors/clean/mdo.exe
python tests/test_session_profile_errors_runtime.py --packed .build/conversation-acceptance/profile-errors/clean/mdo.exe --directory .build/profile-errors-new --interactive
python tests/acceptance/session-profile-errors/check_evidence.py --exe .build/conversation-acceptance/profile-errors/clean/mdo.exe
```

交互模式打印实际 URL。页面打开模型面板后创建 `reject-next`，下一次 PUT
在转发前触发正式配置变化；`healthy` 恢复能力，`stop` 写记录并结束所属进程。
`candidate.json` 核对干净回执、候选哈希和十二个包内生产文件逐字节一致。
