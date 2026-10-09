# 计划任务读取与聊天衔接验收

2026-10-10，生产改动提交 `471f21e90de0d552e1b86747da9edf8a1be82fdc`。
使用独立 Home、实际打包 mdo、真实原生 API 与浏览器页面；模型 HTTP 为本地
可控夹具，没有外部模型请求。原日用程序、手机、网站及用户数据未更新。

## 复现和改动

基线实际列表遇到一次 503 立即报错，并同时显示“还没有计划任务”；编辑
遇到一次 503 立即关闭窗口。六项新行为检查基线五项失败，一项原本通过。

列表采用已有资源读取的八秒单次期限、一分钟总期限、最多六次指数退避，
保留原卡片；自动轮询共享正在恢复的读取，不不断重开恢复预算。最终临时
失败保留一条状态，后台最多每三十秒重新核对一次。明确拒绝不自动重试。
关闭计划页面取消列表读取及退避，重新进入再读取。

编辑和历史复用从扩展管理提取的公共读取控制器，没有增加另一套重试算法。
读取绑定当前页面、设备、运行代及弹窗意图；关闭弹窗、离开页面、替代旧
编辑或页面退出时取消。新建、保存、暂停、删除、立即运行未加入自动重放。

## 实际页面结果

- 列表、编辑、执行历史分别两次 503 后自动成功，等待期间没有错误。
  编辑恢复原名称与任务内容，成功后保存按钮可用。
- 已由原生生成的三个成功正文分别暂扣：编辑取消后新建草稿未被旧内容
  覆盖；历史关闭返回聊天无迟到弹窗；离开计划页面取消列表等待。释放正文
  后三条连接均已关闭，各只有一个该类 GET，没有后续重试。
- 连续六次历史读取失败，约 16.7 秒后只有一条最终提示；正式“重试读取”
  读取到同一条记录，没有重复执行任务。原文采用夹具的未知错误代码，
  保留其诊断文字；本项不宣称新增了所有计划任务错误的三语分类。
- 正式“立即运行”只有一次 POST、一次原生计划执行及一条成功历史。
  所有页面往返保留 `SCHEDULE_READ_FOLLOWUP`；返回聊天正式发送一次、
  得到一个完整回复和一个成功完成事件，零最终模型错误。

浏览器日志没有警告或错误。原生计划与续聊分别一次回环模型请求，总计
两次。Node 时钟检查另证明无响应正文在六次以内、整次一分钟内结束；
实际页面暂扣用于验证取消，不混称为等待整分钟的真机检查。

## 验证与候选

53 项相关 Node、965 项完整前端、163 个可达模块图、124 项契约检查通过。
计划持久化与计划实际执行的两个 xs/TCC 门禁也通过；没有压力或高负载测试。
未重新运行与本次纯前端改动无关的完整原生套件。

候选 `.build/conversation-acceptance/schedule-reads/final/mdo.exe`，
4,554,148 字节，比上一阶段增加 677 字节；SHA-256
`ec1aa98fdff3ab21129a58171de18b38bc6ed1b2d1d9c856d0d6b65641adc7c1`。
构建回执标明生产工作区干净，宿主仍为锁定的 xs `93ef1f61`。
候选与实际运行复制件相同，四份变动的 VFS 生产文件逐字节核对。
`candidate.json` 保存回执必要字段和原回执哈希，完整回执保留在构建目录。
实际 HTTP、原生历史、事件、模型次数、界面、截图由独立检查器交叉核对，
结果见 `report.json`。截图为 JPEG。两个隔离服务正常退出，临时页面已关闭。

```powershell
node --test tests/test_schedule_reads.mjs tests/test_extension_reads.mjs tests/test_automatic_recovery.mjs tests/test_recovery_action.mjs tests/test_run_stop_controller.mjs
node --test tests/test_*.mjs
node --experimental-vm-modules tools/check_web_modules.mjs
python -m unittest discover -s tests -p 'test_*contract.py'
python tests/test_schedule_runtime.py --host .build/conversation-acceptance/mcp-http-status/host/xs.exe
python tests/test_schedule_executor_runtime.py --host .build/conversation-acceptance/mcp-http-status/host/xs.exe
python tests/acceptance/schedule-reads/check_evidence.py --exe .build/conversation-acceptance/schedule-reads/final/mdo.exe --running-copy .build/conversation-acceptance/schedule-reads/final-ui/upstream/mdo.exe
```

页面复现入口使用已有 `tests/manual_extension_read_qa.py --packed <candidate>
--directory <fresh> --duration 1500`，再运行本目录 `setup_fixture.py --directory
<fresh>`。`control.json` 选择一个准确 GET 路径和独立 `phase`，`failures`
最大六次，或 `hold: true` 暂扣首条真实响应；`release-<phase>` 释放。
代理不重放写请求。下一步仍需实际 Android WebView、输入法、文件选择器与
后台恢复验证，当前 adb 没有设备。
