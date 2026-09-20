# mdo UI/UX 走查日志（4 小时专项）

时间起：$(date)
方法：真实浏览器逐界面走查；小问题当场修复（记录【已修】）；需要产品决策的记录【待决议】。

## 发现清单

（走查中填充）


## P1 主流程
### 【已修-严重】F1 渲染管线冻结
- 现象：hero 四问消失、UI 停在最后一次成功渲染
- 根因：ui.js render() 残留块调用 mountComposer 作用域内的 syncProjPill → ReferenceError，每次 render 抛异常，notifier flush 链中断
- 修复：删除 render 中残留块（pill 同步已由 _sync 钩子负责）；v23
- 教训：删代码要查全引用；无 unhandledrejection 监控导致静默冻结数日

## P2 对话中
- ✓ 思考节点展开/收起（初判失效为测试方法误判：点击前状态非预期）
- ✓ 工具卡展开、参数 JSON 可见
- ✓ 反馈点赞/取消 + feedback/set 写穿服务端 UI 日志
- ✓ 轨迹页 200 条事件倒序
- 【已修】F2 tok/s 浮点未格式化（111.3490364025696 → 111.3）
- 【发现-漏网】F3 renderStatsSpan 两处仍用中文字面量键 t('工具 {sec}')/t('{turns} 轮…')，en/ru 下露中文 → 待修
- 教训：DOM class 断言易被渲染时序骗，改用数据链（store/服务端日志）做真值

## P3 侧栏
- ✓ 置顶/取消（置顶区出现+计数、跨项目标签）
- ✓ 搜索过滤（10→1→10）
- ✓ 分叉（标题带 "(分叉)"、事件重放、选中切换）——此前"标题丢失"为误判（分叉未实际执行：懒会话 hero 态无按钮）
- ✓ 删除走确认框+服务端+本地移除（此前 afterDeleteN 未减为测试直调 API 漏了 store.removeSession）
- 【已修】F5/F6：删除确认框、更多菜单（置顶切换/分叉链路 5 处）、项目注销/彻底删除确认框共 9 处字面量中文 → i18n 键（补 act.forking/forked/forkFail/pinToggle/forkMenu 五键×3 语言）
- 【决议候选】D1：懒会话（hero 态）下"重命名/分叉"入口可见性——hero 无消息操作行，入口自然隐藏，暂无需处理；若未来加"复制会话"再议

## P4 设置
- ✓ 七分区全部渲染正常（模型/项目页需异步 refresh，~1s 内就绪）
- ✓ 网络设置持久化（proxyHost 写入+读回）
- ✓ 模型添加表单 10 字段完整、项目添加表单正常

## P5 对话质量
- ✓ 记忆场景：模型自主 write 记忆文件（user-preferences.md）+ 回答正确
- ✓ 上下文追问：42 回答正确；118.2 tok/s 格式化生效
- ✓ 思考节点显示英文推理（模型行为，非 bug）

## P6 边界态
- ✓ 断连恢复：后端重启后 foot-text 回到「已连接 · mdo」
- 【决议候选】D2：运行中刷新页面——会话保留在侧栏、事件可回放、但 running 状态标记丢失且不自动选中（默认落懒会话）。可做「水合时若有 running 会话自动选中+恢复轮询」，工作量中。请决策是否处理。
- 观察选中运行中会话后 selRunning=false 但事件完整（5 nodes），turn 后端已完成，无实际损害

## P7 双主题×三语言×字号
- ✓ 俄语+大字号全界面（侧栏/hero 四问/设置导航/tabs）截图核验
- ✓ 浅色主题此前已验（模型/项目页卡片对比正常）；本轮深色复核通过
- ✓ 语言→中文、字号→中 还原到位

## 现场清理
- 删除走查产生的 4 个测试会话；活动项目 xserver；主题跟随系统；语言中文；字号中

## 决议项落地（用户裁决：全部解决）

### 【已落地】D2 运行中刷新恢复
- 后端：/api/sessions 为运行中会话输出 `turnId`（pRun->uTurnId）
- 前端：wire.js 新增 `resumeTurn(sessionId, turnId, since)`（接管既有 250ms 轮询引擎，
  since=水合时已见最大 seq，避免重复回放）；
  hydrateMdo/hydrateProject 水合循环记录 running+turnId+maxSeq → 自动选中运行中会话并接管轮询
- 同时移除旧的「服务端仍在运行此会话（请刷新或稍后）」错误横幅 hack
- 验证：发多工具任务 → 4s 后刷新 → 自动选中该会话、流式继续、最终回答完整（git log 总结表格）、无横幅

### 【已落地】D1 懒会话顶栏操作
- hero 态（懒会话/无会话）下顶栏「导出/更多」整组隐藏（会话级操作无对象）；选中有内容会话后恢复

### 【已落地】D3a @ 补全空查询
- 裸 `@` 不再拉 400 文件全量列表；输入 ≥1 字符才搜索（服务端遍历时实时过滤，浅层优先语义不变）

### 【已落地】D3b 反馈数据视图
- 后端：GET /api/feedback 扫当前项目桶全部会话 UI 日志聚合 feedback/set（sessionId/title/nodeId/value/time）
- 前端：设置新增「反馈」分区（数据组，👍/👎 徽章 + 会话标题 + 时间；空态文案）
- 修复过程中连带：async 分区函数导致 stSection 同步 spread 抛错的骨架化改造；一处花括号错配

### 验证外发现（如实记录）
- D2 首测出现的 "failed to record a tool result" 错误横幅为**并发遗留任务审批超时链**的真实服务端错误，
  与恢复机制无关；错误透传展示是正确行为。清场后干净场景无此问题。

## 现场清理（终）
- 决议项验证会话全部删除；会话恢复至原始 6 条；活动项目 xserver



## ask_user 工具（人一件）落地
- 后端：mdo_ask.h（工具+等待缝：MdoRun.aAskId/aAskAnswer/iAskState + pALock/pACond）
  - ask_user/requested {id,question,options[]} → 阻塞等 → ask_user/resolved {id,answer}
  - 无超时（4h 兜底）；回合取消立即返回；MdoOnPermission 对 ask_user 直接放行（防二次弹窗）
  - POST /api/ask {id,answer}（全局 askId 查 run，刷新后仍可作答）
- 前端：assembler 两事件 → s.asks；docks 渲染问题卡（选项按钮+自由输入）；wire.respondAsk(id,answer)
- E2E：模型自主调 ask_user → 卡片弹出 → 选项/自由文本回答 → 答案回模型 → 模型复述 ✓
- 过程坑：探针 \n 被 heredoc 展开致多次编译错（已修）；崩溃为上一测试的残损 journal recover 路径，非 ask_user 本身

## 工具集扩展落地（ls/glob/grep + python 三态）
### xwork 库（xrt/extlibs/xwork，已同步 xserver/lib/xwork 并重编 xs.exe）
- 新增探索三件：ls / glob / grep，进程内实现（xrtDirOpen + 自研 glob 匹配 + xrtRegex），零外部依赖，
  输出结构化无区域差异；agent 配置 bRegisterExploreTools（默认 false，host 决定）+
  eExploreMode（INTERNAL/EXTERNAL，EXTERNAL 委托 ls/fd/rg 程序、缺失自动回落内置）
- 新增 python 三态工具：同步持久 REPL（base64 包裹 + 哨兵协议 + 读线程 + 超时复位重生）、
  reset、background（复用 spawn 任务表）；sPythonPath 可指定解释器路径/版本
- mdo 接线：bRegisterExploreTools=true + bRegisterPythonTool=true + sPythonPath 探测
  runtime/python/python.exe（存在则用，否则回退 PATH）
- 修复：探索工具 pUserData 未传 agent（NULL 解引用崩溃）、\n 被 heredoc 展开多次、
  注释含 **/ 提前闭合块注释、XRT_MODULE_REGEX 需加入 xwork-xrt.h、注册函数需非 static
### 事故与恢复
- 清理脚本按 ID 批量删除时因活动项目指向错误桶 + 删除路由按活动桶解析路径，
  曾波及原始会话；已从 data/.trash 全量恢复 6 个原始会话（审计日志逐条核对）。
  暴露设计缺陷【待决议】：会话删除按"当前活动项目"解析桶，应改为按会话自身所属桶
  （meta 里记 slug）解析，杜绝跨桶误删。
