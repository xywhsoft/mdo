# 项目写入与恢复发布前的目标复核

2026-10-02：项目定义的进程内串行写入和明确 workspace 绑定已实现。
这是恢复 coordinator 的前置边界；尚未接入会话 ID 预留、catalog 发布、
恢复 worker 或正式导入菜单，`restore_ready:false` 保持。

## 为什么需要进程内门闩

当前锁定的 xrt 在 POSIX 上用 `fcntl` 实现文件范围锁。该锁按进程计，
不能靠同一进程的两个线程分别打开 `.writer.lock` 实现互斥。Windows
文件锁也不能替代明确的应用内写入协议。原来项目创建/修改/注销只有
共享项目 lifecycle lease 和 native 文件锁；共享 lease 仅防项目清除，
不会阻止另一个共享 writer。因此两个请求可能同时读到相同 revision。

新增 `MdoProjectDefinitionAcquire/Release`，当前 lifecycle registry 在全部
项目定义上只允许一个 writer，非阻塞。Guard pin 调用者的共享 owner；
创建、修改和注销在打开 `.writer.lock` 前先取得 guard。跨进程 native
文件锁仍保留，锁顺序是 lifecycle owner → definition guard → native
writer file → session/storage manager。完成及失败都先关闭文件，再释放
guard 和 owner。Busy 不创建 Home；HTTP 写入统一返回 `409 project_busy`。
只读项目和会话操作仍可使用，另一个项目的定义写入也暂时返回 busy。

Init/Unit 的原合同保持：宿主先停止新调用并排空执行中的操作。Unit 关闭
旧 registry；已经持有的 guard 保持 registry/owner 存活，能安全释放。
新 Init 的 writer 标志独立，释放旧 guard 不会解锁新 registry。

## 目标绑定与短发布回调

`MdoProjectBindingGet` 是不产生 Home 的 owning snapshot，记录项目 ID、
revision、created time、已存在的物理 workspace 绝对路径及 directory
device/identity。配置相对路径基于 `xsAppPath()`（程序所在目录），与现有
会话/workspace API 相同。缺失的 `default` 使用 cwd，revision/created 为
零；其他缺失项目不推断目标。不存在的 workspace、常规文件或无法获取
稳定 directory identity 明确失败。备份内的旧 workspace 不提供目标授权。
错误的 output Size 原样保留；其他失败清空输出并保留 Size。

`MdoProjectWithBinding` 只做短同步发布边界。检查当前共享 owner，取得
definition guard/native writer lock，重新读取完整 binding，只有项目版本、
incarnation、物理路径和目录身份都匹配才调用 callback。匹配失败不执行
callback；回调成功或失败都释放锁。项目注销后重建、缺省项目后来注册、
同一路径目录替换均要求重新复核。callback 不能执行 staging、像素检查、
网络等待、模型运行或项目 mutation，也不能反向取得 lifecycle lease；
应仅进入已准备好的 session/storage 发布边界。

Native lock close 失败可能发生在 callback 已完成以后。返回 false 不表示
callback 的提交已撤销，上层必须保留独立 Committed 事实，按需冻结 Home
并查询同一请求结果，不能直接以新 ID 重做。Binding 不自行发布会话或
推进 catalog，也不替代用户的确认、profile 验证及输入待确认转换。

此协议协调已登记的应用写入，不宣称排除其他软件在 callback 执行期间
改动物理 workspace。Directory identity 是同机的当前目标检查，不作为
跨机器可移植身份或备份真实性认证。本阶段未修改 xrt/SDK/library pins。

## 有界验证

`test_project_binding_runtime.py` 使用复制的真实 standalone xs host 和
TCC，不改生产源码的 hook；两个运行分别验证只读零 Home 和写入。全部
workspace/文件位于临时目录。59 项检查、六次发布回调覆盖：

- 当前 owner/guard、全项目 busy、Error=NULL、错误 Size/项目/owner；
- 一个实际 manager writer 在 native lock 后暂停，另一个线程创建/修改/
  注销与发布均拒绝，原定义保持，恢复后 writer 完成、其他写入可重试；
- 相对/绝对及 Unicode workspace、不存在或文件类型 workspace；
- revision 变化、注销重建、虚拟 default 后来注册、同路径物理目录替换；
- callback 失败和重试、真实 close 后的合成 false 不撤销已执行回调；
- 旧 registry guard/owner 的 pin 与释放、新 registry 的独立 writer 标志；
- Home 冻结时拒绝发布，失败后 definition guard 不泄漏。

`test_api_runtime.py` 另使用真正 HTTP/TLS handler，持有定义 guard 后核对
创建/修改/注销为 `409 project_busy`、GET 仍可读、定义字节保持、拒绝
创建没有新文件。Guard 释放后既有项目 mutation round-trip 继续通过。

初始探针漏传 `MdoHomeRequireRestart` message，随后修正；首轮相对路径
夹具错把 dev site 当作 `xsAppPath`，改为在临时 site 内复制已验证 native
host，保持生产路径合同。日志保留于 `.build/qa-project-binding-runtime-
initial.log` 和 `...-debug.log`，最终定向日志 `...-final.log`。
这些用例不涉及压力/高负载或外部模型，也不是新增 DOM/设备验收。

首轮 Linux 完整门禁中，已有备份下载探针的首次大响应断言失败；旧
断言没有输出状态/长度，不能从该日志推断原因。仅为这个断言补充
状态、长度、header 与有界响应片段，未放宽成功条件或修改生产传输。
同一 staging 的定向 HTTP/TLS 复查通过，首轮日志保存为
`.build/qa-project-binding-linux-initial.log`，复查日志为
`.build/qa-project-binding-linux-download-focused.log`。该间歇失败的原因
仍未确认，不能用后续通过结果声明已修复。

最终 Windows/Linux 完整门禁通过 115 Python、252 Node、90 JS 模块、
严格 C11、42 runtime、三项 packed 及独立 A/B；Windows 另通过便携
WebView2 Home 和 20 秒启动。Linux 使用新的 ext4 staging
`/home/ubuntu/.cache/mdo-linux-project-binding-afvca_5j`，十三份改动代码/
清单/探针与 Windows 按 LF 核对。SDK/library pins 不变，复用已验证
native host，没有声称本阶段重建宿主。最终日志
`.build/qa-project-binding-{windows,linux}-final.log`，源码核对
`.build/qa-project-binding-source-equivalence.log`。

根目录 `mdo.exe` 已更新为 6,425,304 字节、SHA-256
`4607733ea1e995853274519491dcb44f32ac25ccd26545c09a88194620e2ab43`；
Linux A/B 6,475,464 字节、SHA-256
`eb76fa864e18e86e4b60cd2f20dc304868b96bee63f499e802e141e541169c9c`。
更新时没有根目录运行窗口。本阶段没有压力/高负载或新增设备验收。
