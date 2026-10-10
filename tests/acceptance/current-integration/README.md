# 当前工作区的独立集成候选

2026-10-11。保留工作区另批发布工作的未提交修改，把队列校验拒绝修复与它们一起
复制到独立工作区构建。应用输入共 431 个文件，包含未提交源码；不把此候选标为
仅由某个干净提交构建。原工作区源码和日用 Home 未被替换。

## 实际打包页面

使用 [有界原生 API 夹具](../../manual_run_rejection_qa.py)，打开真实候选返回的正式
页面。首次图片发送被模型能力校验拒绝：队列保持 pending，准确提示持续可见，
没有执行模型。修正同一模型配置后，实际点击“发送下一条”，原图片仅发送一次；
再从输入框普通续聊一次成功。两个回合完成，队列与草稿清空，浏览器无警告或错误。

此过程没有更改队列项的固定模型，没有模拟浏览器请求，也没有使用生产账号。
原生状态和请求计数见 [记录](packed-ui.json)，最终页面见 [截图](continued.png)。

## 来源与安装前检查

- Windows 候选为 4,570,274 字节，SHA-256
  `224318e83b37c7099386a41288672e2fa36eaa06eb512bc7eb6358ab548fb3ed`。
- 独立 ARM64 精简 QA 包为 4,199,661 字节，包名 `org.xleaves.mdo.qa.stability`，
  版本码 30000064，SHA-256
  `2f74b8455953d48f9680bd43663ffa83a35c7411dca547acc8cb7549985f660d`。
- Windows 和 Android 宿主均来自锁定 xs `f05f7661062fdca04da8d571ad859eb4b87c0889`；
  Windows 宿主复用本轮从源码新构建、已核对回执的文件；ARM64 宿主重新编译。
- APK 的 CRC、重复项、实际内嵌库与应用归档、宿主回执、所需 API、ARM64 依赖、
  16 KiB ELF/ZIP 对齐、实际 DEX 中的 FileChooserResult 均通过。v2/v3 签名和
  与上一版已安装 QA 包相同的签名身份通过，允许升级保留测试数据。
- 从 Windows xs/TCC 的实际 HTTP/VFS 读取 Windows 候选和 APK 的应用归档，五个
  关键生产前端文件均与工作区原字节一致。读取 APK 归档时运行的仍是 Windows
  宿主，不能称为 Android 设备验收。

来源、归档与签名记录见 [source-verification.json](source-verification.json)、
[packed-module-bytes.json](packed-module-bytes.json)、
[android-verification.json](android-verification.json) 和
[qa-upgrade-signature.json](qa-upgrade-signature.json)。

## 复验准备

独立工作区不必与网站仓库相邻。测试使用明确路径；4ab30148 已补齐两个仍依赖
相邻目录的集成探针。网站和账号夹具只修改自己的隔离副本。

```powershell
$env:MDO_XADMIN_ROOT = 'D:\GIT\x-admin'
$env:MDO_TEST_WEBSITE_REPO = 'D:\GIT\home'
$env:MDO_ECOSYSTEM_SITE_ROOT = 'D:\GIT\home\host\xywhsoft_ai'
python tools/qa_release.py --skip-host-build --xserver-root <锁定xs目录>
```

`--skip-host-build` 此处只复用本轮新构建且回执相符的本地 QA 宿主，正式发布仍应
按 [发布门禁](../../../docs/release-gate.md) 重建。运行前还需要准备
`.build/releases/toolpacks/catalog.json` 及其常用/Python 两个 XRTPACK。
有本地运行工具素材时，可用 `python tools/build_toolpacks.py --packer .build/host/xsw.exe`
生成。本阶段独立工作区复用本地已生成归档，并逐一核对清单中的摘要。
未准备它们时，第一次完整门禁在分发探针处失败；该失败不是模型或产品运行故障。

没有执行压力、高负载或 soak 测试，没有发布官网或更新日用包。新版 QA 包的实际
升级、对话和原生系统接口仍待设备连接；较早 QA 包的真机结果不替代新版验收。
Windows 原生复制与文件保存也仍需实际系统交接确认。
