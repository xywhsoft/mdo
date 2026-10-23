import { element as make, iconButton, popupMenu } from "./controls.js";
import { createTerminalInput, terminalBytes } from "./terminal-io.js";
import { loadTerminalRenderer } from "./terminal-renderer.js";
import { toast } from "../../utils/dom.js";

const encoder = new TextEncoder(), sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
const running = row => row && ["starting", "running", "closing"].includes(row.state);
const stateName = row => ({ starting: "正在启动", running: "运行中", closing: "正在结束", ended: "已结束" })[row?.state] || "未连接";
const reasonName = row => ({ service_stopped: "本地服务已停止，终端未重启。", service_restarted: "服务已重启，旧终端未重启，输入未重放。", process_exited: `进程已退出${row?.exit_code !== undefined ? `（${row.exit_code}）` : ""}。`, closed: "终端已结束。", not_started: "程序未能启动。", cancelled_before_start: "已取消启动。" })[row?.reason] || "";

export async function mountTerminalView(root, { sdk, context, params = {}, viewState = {}, update, signal, addContext }) {
  const { Terminal, FitAddon, SearchAddon, Unicode11Addon } = await loadTerminalRenderer();
  if (signal.aborted) return;
  const capabilities = await sdk.call("terminal.capabilities", {}, { signal });
  if (!capabilities.supported) { root.append(make("p", "wb-error", "当前设备的运行时不支持交互终端。")); return; }
  root.classList.add("wb-terminal-view");
  let terminal, fit, searchAddon, observer, themeObserver, selected, lease, input, reading, renewing = false, attaching = false, resizing = false, wantedSize, creation = false, blocked = false, connected = false, cursor = 0, generation = 0, renewAt = 0, disposed = false, fontSize = Math.max(11, Math.min(22, Number(viewState.terminal?.fontSize) || 13)), lastSize = "", notice = "", autoControl = true, pendingBytes = 0;
  let reviewRequired = false;
  const bar = make("div", "wb-terminal-toolbar"), sessions = make("select", "wb-terminal-sessions"), path = make("div", "wb-terminal-path"), surface = make("div", "wb-terminal-surface"), footer = make("div", "wb-terminal-status"), status = make("span"), message = make("span", "wb-terminal-message"), placeholder = make("div", "wb-terminal-empty");
  const onError = error => toast(error.message || String(error), "error");
  const action = (glyph, label, execute) => iconButton(glyph, label, execute, onError);
  const plus = action("plus", "新建终端", () => create()), control = action("refresh", "连接终端输入", () => acquire(true)), end = action("stop", "结束终端", () => close()), more = action("more", "终端更多操作", () => {});
  sessions.setAttribute("aria-label", "终端会话"); footer.setAttribute("role", "status");
  bar.append(sessions, plus, control, end, more); footer.append(status, message); root.append(bar, path, surface, placeholder, footer);
  const findbar = make("div", "wb-terminal-find"), find = make("input"), matches = make("span"); find.placeholder = "查找终端内容"; find.setAttribute("aria-label", find.placeholder); findbar.hidden = true;
  const search = (previous = false) => { if (find.value) searchAddon?.[previous ? "findPrevious" : "findNext"](find.value, { caseSensitive: false, regex: false, decorations: { matchBackground: "#b29b4444", matchBorder: "#9a8b55", activeMatchBackground: "#3478d4", activeMatchColor: "#ffffff" } }); else { searchAddon?.clearDecorations(); matches.textContent = ""; } };
  find.oninput = () => search(); find.onkeydown = event => { if (event.key === "Enter") { event.preventDefault(); search(event.shiftKey); } if (event.key === "Escape") { event.preventDefault(); hideFind(); } };
  const hideFind = () => { findbar.hidden = true; searchAddon?.clearDecorations(); terminal?.focus(); };
  const showFind = () => { findbar.hidden = false; find.focus(); find.select(); };
  findbar.append(find, matches, action("arrow-left", "上一个匹配", () => search(true)), action("arrow-right", "下一个匹配", () => search()), action("close", "关闭终端查找", hideFind)); root.insertBefore(findbar, surface);
  const copy = async () => { const text = terminal?.getSelection(); if (text) await navigator.clipboard.writeText(text); };
  const quote = () => {
    const text = terminal?.getSelection(); if (!text) return;
    if (encoder.encode(text).length > 32768) throw new Error("选中内容超过 32 KiB，请缩小终端选择范围。");
    addContext({ title: "终端选择内容", source: `terminal:${selected.id}`, context,
      content: `终端：${selected.title || "终端"}\n会话：${selected.id}\n工作目录：${selected.cwd}\n设备：${context.target || context.device || "本机"}\n\n${text}` });
  };
  const paste = async () => { if (!input || blocked) throw new Error("请先连接终端输入。"); terminal.paste(await navigator.clipboard.readText()); terminal.focus(); };
  const changeFont = value => { fontSize = Math.max(11, Math.min(22, value)); if (terminal) terminal.options.fontSize = fontSize; measure(); remember(); };
  const menu = popupMenu(more, () => [
    { label: "复制选择内容", disabled: !terminal?.hasSelection(), execute: copy },
    { label: "粘贴", disabled: !input || blocked, execute: paste },
    { label: "引用选择内容", disabled: !terminal?.hasSelection(), execute: quote },
    { label: "查找…", disabled: !terminal, execute: showFind },
    { label: "刷新终端列表", execute: async () => { const rows = await refresh(); if (selected && !rows.some(row => row.id === selected.id)) await select(""); } },
    { label: "清空显示", disabled: !terminal, execute: () => terminal.clear() },
    { label: "增大字体", disabled: !terminal || fontSize >= 22, execute: () => changeFont(fontSize + 1) },
    { label: "减小字体", disabled: !terminal || fontSize <= 11, execute: () => changeFont(fontSize - 1) },
    { label: "释放已结束的终端", disabled: !selected?.drained, execute: async () => { const id = selected.id; if (!confirm("移除这个已结束终端的记录和内存输出？")) return; await sdk.call("terminal.release", { id }, { signal }); await select(""); await refresh(); } },
  ], { signal, onError });
  const visible = () => !disposed && !signal.aborted && !root.hidden && !document.hidden && surface.clientWidth > 0 && surface.clientHeight > 0;
  const remember = () => { if (!disposed) void update({ title: selected?.title || "终端", view_state: { ...viewState, terminal: { id: selected?.id || "", fontSize } } }).catch(onError); };
  const renderStatus = () => {
    if (disposed) return;
    root.dataset.terminalState = reviewRequired ? "unknown" : selected?.state || "empty"; root.dataset.terminalInput = input && !blocked ? "connected" : "readonly";
    status.textContent = reviewRequired ? "状态待确认 · 只读" : !selected ? "终端" : `${stateName(selected)}${running(selected) ? input && !blocked ? " · 可输入" : " · 只读" : ""}${pendingBytes ? ` · 待确认 ${pendingBytes} B` : ""}`;
    message.textContent = notice || selected?.error || (!running(selected) ? reasonName(selected) : "关闭标签页会保留终端"); message.title = message.textContent;
    path.textContent = selected ? `${selected.cwd} · ${selected.program}` : ""; path.title = path.textContent;
    plus.disabled = creation || reviewRequired; end.disabled = reviewRequired || !running(selected) || selected.state === "closing";
    const option = [...sessions.options].find(option => option.value === selected?.id); if (option) option.textContent = `${selected.title} ${selected.id.slice(-4)} · ${reviewRequired ? "状态待确认" : stateName(selected)}`;
    control.disabled = reviewRequired || !selected || selected.state !== "running" || attaching; control.title = control.disabled ? "连接终端输入" : input && !blocked ? "重新连接终端输入" : "连接终端输入";
    terminal && (terminal.options.disableStdin = !input || blocked || !visible() || !connected);
  };
  const clearInput = (reason = "") => { input?.stop(); input = null; pendingBytes = 0; if (reason) notice = reason; renderStatus(); };
  const detach = () => { const old = lease; lease = null; clearInput(); if (old) void sdk.call("terminal.detach", old).catch(() => {}); };
  const inputFault = error => {
    blocked = true; autoControl = false;
    const unknown = !error.status || error.status >= 500 || error.name === "AbortError";
    notice = unknown ? "连接中断，最近输入是否收到尚不确定；请检查输出后重新连接，输入不会重发。" : `输入已暂停：${error.message}。请检查后重新连接。`;
    detach(); renderStatus();
  };
  async function acquire(explicit = false) {
    if (!selected || selected.state !== "running" || !visible() || attaching || (blocked && !explicit)) return;
    if (explicit) { detach(); blocked = false; autoControl = true; }
    const version = generation, id = selected.id, client = `tv-${crypto.randomUUID()}`; attaching = true; renderStatus();
    try {
      const attached = await sdk.call("terminal.attach", { id, client, control: true }, { signal });
      const newLease = { id, epoch: attached.epoch, client, control_token: attached.control_token, input_epoch: attached.input_epoch, next_sequence: attached.next_sequence };
      if (disposed || reviewRequired || version !== generation || !visible() || !attached.control_token) { if (attached.control_token) void sdk.call("terminal.detach", newLease).catch(() => {}); return; }
      selected = attached; lease = newLease; renewAt = Date.now() + 5000; blocked = false; connected = true; notice = "";
      input = createTerminalInput({ lease, write: (params, options) => timedCall("terminal.write", params, options.signal), fault: inputFault, changed: count => { pendingBytes = count; renderStatus(); } });
      lastSize = ""; measure(); terminal?.focus();
    } catch (error) { if (!signal.aborted && version === generation) { blocked = true; autoControl = false; notice = error.code === "terminal_control_busy" ? "另一个页面正在输入；这里可以继续只读查看。" : `暂时无法连接输入：${error.message}`; } }
    finally { attaching = false; renderStatus(); }
  }
  async function timedCall(method, params, parent = signal) {
    const request = new AbortController(), cancel = () => request.abort();
    if (parent.aborted) request.abort(); else parent.addEventListener("abort", cancel, { once: true });
    const timer = setTimeout(cancel, 7000);
    try { return await sdk.call(method, params, { signal: request.signal }); }
    finally { clearTimeout(timer); parent.removeEventListener("abort", cancel); }
  }
  async function renew() {
    if (!lease || renewing || !visible() || Date.now() < renewAt) return;
    const current = lease, version = generation; renewing = true;
    try {
      const result = await timedCall("terminal.attach", { id: current.id, client: current.client, control: true });
      if (version !== generation || lease !== current) return;
      if (result.epoch !== current.epoch || result.input_epoch !== current.input_epoch || result.control_token !== current.control_token) {
        if (result.control_token) void sdk.call("terminal.detach", { ...current, epoch: result.epoch, control_token: result.control_token, input_epoch: result.input_epoch }).catch(() => {});
        throw new Error("输入租约已改变");
      }
      renewAt = Date.now() + 5000;
    } catch (error) { if (version === generation && lease === current && !signal.aborted) inputFault(error); }
    finally { renewing = false; }
  }
  function measure() {
    if (!terminal || !visible()) return;
    const proposed = fit.proposeDimensions(); if (!proposed) return;
    const columns = Math.max(2, Math.min(capabilities.max_columns, proposed.cols)), rows = Math.max(2, Math.min(capabilities.max_rows, proposed.rows));
    if (terminal.cols !== columns || terminal.rows !== rows) terminal.resize(columns, rows);
    wantedSize = { columns, rows }; void resize();
  }
  async function resize() {
    if (resizing) return; resizing = true; let owner;
    try {
      while (lease && !blocked && visible() && wantedSize) {
        const current = lease, size = { ...wantedSize }, key = `${size.columns}:${size.rows}`; owner = current; if (key === lastSize) break;
        await timedCall("terminal.resize", { ...current, ...size }); if (current === lease) lastSize = key;
      }
    } catch (error) { if (lease === owner && !signal.aborted) inputFault(error); }
    finally { resizing = false; }
  }
  function theme() {
    if (!terminal) return; const style = getComputedStyle(root), dark = style.colorScheme === "dark";
    terminal.options.theme = { background: style.getPropertyValue("--surface").trim() || "#171716", foreground: style.getPropertyValue("--ink").trim() || "#eeeeea", cursor: style.getPropertyValue("--ink").trim(), selectionBackground: dark ? "#608cb566" : "#7ca8d066", black: dark ? "#252525" : "#202020", red: dark ? "#ef7777" : "#b52f37", green: dark ? "#87bd7e" : "#287c39", yellow: dark ? "#dfc681" : "#996c19", blue: dark ? "#7aa9df" : "#286fbd", magenta: dark ? "#c49ae0" : "#9651ac", cyan: dark ? "#74c2c6" : "#247d88", white: dark ? "#dddddd" : "#74746f", brightBlack: dark ? "#999999" : "#555555", brightRed: dark ? "#ff9999" : "#b52f37", brightGreen: dark ? "#a0d990" : "#287c39", brightYellow: dark ? "#f5dc95" : "#896414", brightBlue: dark ? "#91bfff" : "#286fbd", brightMagenta: dark ? "#ddb2ff" : "#9651ac", brightCyan: dark ? "#8de2e8" : "#247d88", brightWhite: dark ? "#ffffff" : "#30302e" };
  }
  function renderer() {
    terminal?.dispose();
    // xterm's screen-reader mode intentionally ignores insertText events and
    // breaks emoji/IME input without an active reader. Keep its normal mode.
    terminal = new Terminal({ cursorBlink: true, fontSize, fontFamily: 'Consolas, "Cascadia Mono", "Noto Sans Mono CJK SC", monospace', scrollback: 2000, allowProposedApi: true, disableStdin: true, screenReaderMode: false });
    fit = new FitAddon(); searchAddon = new SearchAddon(); terminal.loadAddon(fit); terminal.loadAddon(searchAddon); terminal.loadAddon(new Unicode11Addon()); terminal.unicode.activeVersion = "11";
    terminal.open(surface); terminal.textarea.setAttribute("aria-label", "交互终端输入"); theme();
    const admit = (text, binary = false) => {
      if (!input || blocked) return;
      try {
        // Refuse huge paste events before allocating their encoded copy.
        if (text.length > 65536) throw new Error("粘贴内容超出 64 KiB 输入队列，尚未发送；请分段粘贴。");
        input.enqueue(binary ? Uint8Array.from(text, char => char.charCodeAt(0) & 255) : encoder.encode(text));
        terminal.clearSelection();
        if (notice.startsWith("粘贴内容超出")) { notice = ""; renderStatus(); }
      } catch (error) { notice = error.message; renderStatus(); }
    };
    terminal.onData(text => admit(text)); terminal.onBinary(text => admit(text, true));
    searchAddon.onDidChangeResults(result => { matches.textContent = result.resultCount ? `${result.resultIndex + 1} / ${result.resultCount}` : "无匹配"; });
    terminal.attachCustomKeyEventHandler(event => {
      if (event.type !== "keydown") return true;
      if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "f") { event.preventDefault(); showFind(); return false; }
      if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "c" && terminal.hasSelection()) { event.preventDefault(); void copy().catch(onError); return false; }
      if ((event.ctrlKey || event.metaKey) && event.shiftKey && event.key.toLowerCase() === "v") { event.preventDefault(); void paste().catch(onError); return false; }
      return true;
    });
    // Shortcuts belong to the terminal while focused, including Ctrl+C/Ctrl+J.
    measure();
  }
  surface.addEventListener("keydown", event => event.stopPropagation(), { signal });
  async function select(id) {
    generation++; reading?.abort(); detach(); blocked = false; autoControl = true; connected = false; selected = null; cursor = 0; notice = ""; lastSize = ""; terminal?.dispose(); terminal = null; surface.replaceChildren(); placeholder.replaceChildren(); placeholder.hidden = false;
    if (!id) {
      const createButton = action("plus", "新建终端", create); placeholder.append(make("h3", "", "终端"), make("p", "", "在当前项目中运行命令。"), createButton); remember(); renderStatus(); return;
    }
    const version = generation, row = await sdk.call("terminal.get", { id }, { signal }); if (version !== generation || signal.aborted) return;
    selected = row; cursor = row.oldest; if (row.oldest) notice = "较早输出已超出缓存，仅显示保留内容；屏幕可能不完整。";
    renderer(); sessions.value = row.id; placeholder.hidden = true; remember(); renderStatus(); if (row.state === "running") await acquire();
  }
  async function refresh() {
    const rows = await sdk.call("terminal.list", {}, { signal }); if (signal.aborted) return;
    sessions.replaceChildren();
    if (!selected && rows.length) { const option = make("option", "", "选择终端…"); option.value = ""; sessions.append(option); }
    if (!rows.length) { const option = make("option", "", "没有终端"); option.value = ""; sessions.append(option); }
    for (const row of rows) { const option = make("option", "", `${row.title} ${row.id.slice(-4)} · ${stateName(row)}`); option.value = row.id; sessions.append(option); }
    if (selected && rows.some(row => row.id === selected.id)) sessions.value = selected.id;
    return rows;
  }
  async function create() {
    if (creation) return; creation = true; renderStatus();
    try {
      // This key belongs to one explicit click only; reload/list never creates.
      const row = await timedCall("terminal.create", { key: `term-${crypto.randomUUID()}`, columns: terminal?.cols || 80, rows: terminal?.rows || 24 });
      if (signal.aborted) return; await refresh(); await select(row.id); sessions.value = row.id;
    } catch (error) { if (!signal.aborted) { notice = `启动结果尚需核对：${error.message}；请从会话列表查看。不会自动重复启动。`; await refresh().catch(() => {}); renderStatus(); } }
    finally { creation = false; renderStatus(); }
  }
  async function close() {
    if (!running(selected) || !confirm("结束这个终端及它启动的子进程？关闭标签页可保留运行。")) return;
    const current = selected, version = generation; detach(); blocked = true;
    try { const ended = await timedCall("terminal.close", { id: current.id, epoch: current.epoch }); if (version === generation) { selected = ended; notice = ""; renderStatus(); } }
    catch (error) { if (version === generation) { notice = `结束结果尚未确认：${error.message}。请重新查看状态。`; renderStatus(); } }
    await refresh();
  }
  sessions.onchange = () => { void select(sessions.value).catch(onError); };
  async function pump() {
    while (!disposed && !signal.aborted) {
      if (reviewRequired) { await sleep(300); continue; }
      if (!visible()) { if (lease) detach(); await sleep(150); continue; }
      if (!selected || !terminal) { await sleep(200); continue; }
      if (!lease && autoControl && !blocked && selected.state === "running") void acquire();
      void renew();
      const version = generation, current = selected, request = new AbortController(); reading = request;
      const abort = () => request.abort(); signal.addEventListener("abort", abort, { once: true });
      try {
        const result = await timedCall("terminal.read", { id: current.id, epoch: current.epoch, cursor, limit: 32768, wait_ms: 500 }, request.signal);
        if (version !== generation || signal.aborted) continue;
        const data = terminalBytes(result, cursor); connected = true; selected = result;
        if (result.gap) { terminal.reset(); notice = "输出超出 1 MiB 缓存，较早部分已丢弃；屏幕可能不完整。"; }
        if (data.length) await new Promise(resolve => {
          const done = () => { request.signal.removeEventListener("abort", done); resolve(); };
          if (request.signal.aborted) { done(); return; }
          request.signal.addEventListener("abort", done, { once: true }); terminal.write(data, done);
        });
        if (version !== generation || signal.aborted || request.signal.reason === "hidden") continue;
        cursor = result.next;
        if (!running(selected)) { detach(); autoControl = false; }
        renderStatus();
        if (result.eof && result.next >= result.cursor) await sleep(800);
      } catch (error) {
        if (version !== generation || signal.aborted || request.signal.reason === "hidden") continue;
        connected = false;
        if (["write_token_conflict", "purge_review_required"].includes(error.code)) {
          reviewRequired = true; generation++; blocked = true; autoControl = false; detach();
          notice = "终端状态尚未确认。请保留草稿并重新载入；进程和输入不会重放。";
        } else if (error.code === "terminal_epoch_changed") {
          blocked = true; autoControl = false; detach();
          try { selected = await sdk.call("terminal.get", { id: current.id }, { signal }); cursor = selected.oldest; terminal.reset(); } catch { /* keep old identity, never recreate */ }
          notice = "服务已重启，旧终端未重启，输入未重放。";
        } else { if (lease) inputFault(error); notice ||= `暂时无法读取终端：${error.message}`; }
        renderStatus(); await sleep(1000);
      } finally { signal.removeEventListener("abort", abort); if (reading === request) reading = null; }
    }
  }
  observer = new ResizeObserver(measure); observer.observe(surface);
  themeObserver = new MutationObserver(theme); themeObserver.observe(document.documentElement, { attributes: true, attributeFilter: ["data-theme"] });
  const scheme = matchMedia("(prefers-color-scheme: dark)"); scheme.addEventListener("change", theme);
  const dispose = () => { if (disposed) return; disposed = true; generation++; reading?.abort(); detach(); observer.disconnect(); themeObserver.disconnect(); scheme.removeEventListener("change", theme); menu.dispose(); terminal?.dispose(); };
  signal.addEventListener("abort", dispose, { once: true });
  try {
    const rows = await refresh(), initial = viewState.terminal?.id || params.terminal;
    await select(rows.some(row => row.id === initial) ? initial : rows.find(row => running(row))?.id || rows.at(-1)?.id || "");
  } catch (error) { dispose(); throw error; }
  void pump();
  return { dispose, visible(shown) { if (!shown) {
    reading?.abort("hidden");
    if (input?.pending) { blocked = true; autoControl = false; notice = "已停止尚未确认的输入，请检查输出后重新连接；输入不会重发。"; }
    detach();
  } else { measure(); renderStatus(); } } };
}
