# 输入法状态在窗口失焦后的恢复

本阶段确认了四个入口的同类残留状态：主输入框 Enter、斜杠命令、文件引用，以及会话搜索的 Escape。原代码只在输入框 blur 或 compositionend 时清除各自的布尔值；窗口失焦而输入框仍保有焦点、组合结束事件未到达时，后续正常按键或输入仍被当作选词操作。

生产代码复用已有 `createCompositionTracker`，按编辑器跟踪组合状态，在输入框或窗口失焦时清除。会话搜索明确关闭时也复位自己的状态；候选菜单在窗口失焦时隐藏。保留 `isComposing`、229 候选键及活动组合的保护；输入事件仍标记为组合中时不展开候选菜单。没有加入文本去重、任意等待时间或新的配置项。

## 基线与修复验证

- 真实旧打包页面先收到合成的 compositionstart、窗口 blur/focus，再通过浏览器键盘按 Enter。输入仅增加换行，发送仍可点击，零模型请求，见 `baseline-browser.json`、`baseline.png`、`baseline-proof.json`。
- 同一组件浏览器夹具在旧代码中，失焦后斜杠和文件引用候选都没有恢复；原有选词、引用插入和 blur 检查通过，见 `baseline-menus.json`。新会话搜索检查在旧代码中得到 4 通过、1 失败，见 `baseline-search.log`。
- 修复后的真实打包页面在相同事件顺序下，通过 Enter 成功完成第一轮。第二轮的活动组合、`isComposing: true` 和 229 候选键都没有额外发送；结束组合后通过 Enter 完成一轮。合计两次实际回环模型请求，两条输入逐字相符，两轮均 succeeded，零最终模型错误，草稿和队列为空，见 `composition-protected.json`、`candidate-keys-after-blur.json`、`proof.json`。
- 该打包页面还通过真实搜索按钮打开搜索，合成未完成组合及窗口失焦后用 Escape 关闭，并将焦点返回主输入框，见 `continued-browser.json`、`continued.png`。
- 修复后的原组件浏览器夹具全部条件通过，失焦后斜杠和文件引用恢复、候选键仍受保护，见 `fixed-menus.json`。这个独立夹具模拟工作区检索返回值；它证明前端状态和操作配合，不代替真实工作区检索服务的验收。

32 项相关 Node 检查和四项干净前端合约通过，见 `checks.json` 及检查日志。五个实际 HTTP 返回的生产文件与独立源码相同，见 `source-parity.json`。候选为 `.build/conversation-acceptance/composer-window-focus-package/mdo.exe`，基线 `b784d4da` 加本阶段生产改动，xs 锁定 `3232f7b8`；SHA-256 `429ef5742e530285c4f7165394db11f129b408cff6212584736ffa9066dff0d9`，4,461,540 字节。根目录其他未提交工作未混入。

上述 composition 和窗口事件均由夹具或调试协议合成，真实键盘操作、产品页面、HTTP/TCC、队列和回环模型链路未替换。这些证据确认残留状态路径修复，不能当成 Windows 原生输入法或 Android 输入法已经实测，也不能证明此前偶发重复输入的根因已查明。没有压力或高负载测试，没有更新日常程序、手机或官网。
