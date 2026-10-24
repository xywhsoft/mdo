import { browserLocation, browserPhase, safeBrowserUrl } from "./model.js";
import { element as make, icon, iconButton, popupMenu } from "./controls.js";
import { toast } from "../../utils/dom.js";
import { isRemoteTarget } from "../../api/target.js";

const rect = node => { const r = node.getBoundingClientRect(); return { x: Math.round(r.x), y: Math.round(r.y), width: Math.round(r.width), height: Math.round(r.height), scale: devicePixelRatio || 1 }; };
const errorToast = error => toast(error.message || String(error), "error");
function assertDisplayUrl(url) {
  if (url && isRemoteTarget() && ["localhost", "127.0.0.1", "[::1]"].includes(new URL(url).hostname))
    throw new Error("此地址指向目标设备的本地服务。请先映射预览服务，或提供可访问的网址。");
}

export async function mountBrowserView(root, { row, sdk, signal, mount, bridge, clientId, shown, updateTitle, openExternal, onEnd, addContext }) {
  let browser = await sdk.call("browser.get", { id: row.resource.id }, { signal });
  if (browser.client !== clientId) browser = await sdk.call("browser.attach", { id: browser.id, revision: browser.revision, client: clientId }, { signal });
  assertDisplayUrl(browser.url);
  // The browser outlives this panel. Its command dispatcher must not retain
  // the panel's AbortSignal; closing a view only stops UI polling and bounds.
  const execute = (command, params = {}) => bridge.call(command, { browser: browser.id, ...params });
  mount.browserId = browser.id;
  let state = { url: browser.url || "about:blank", loading: Boolean(browser.url) }, timer, lastBounds = "", lastReport = "", lastTitle = row.title || "", polling = false, connecting = false;
  const toolbar = make("form", "wb-browser-toolbar"), address = make("input", "wb-address"), region = make("div", "wb-browser-region"), surface = make("div", "wb-browser-surface"), message = make("div", "wb-browser-message"), status = make("span", "wb-browser-status");
  address.setAttribute("aria-label", "搜索或输入网址"); address.placeholder = "搜索或输入网址"; address.spellcheck = false; address.autocomplete = "off";
  address.value = browser.url || ""; status.setAttribute("role", "status");
  const action = (name, label, run) => iconButton(name, label, run, errorToast);
  const back = action("arrow-left", "后退", () => execute("back")), forward = action("arrow-right", "前进", () => execute("forward"));
  const reload = action("refresh", "刷新", () => state.loading ? execute("stop") : recover());
  const manual = action("hand", "接管浏览器", async () => { const value = await execute(state.manual ? "release" : "takeover"); state = { ...state, ...value }; paint(); });
  const viewport = make("div", "wb-viewport-toolbar"); viewport.hidden = row.type !== "mdo.preview";
  const size = make("select", "wb-viewport-size"); size.setAttribute("aria-label", "预览宽度");
  for (const [value, label] of [["", "自适应"], ["390", "手机 · 390"], ["768", "平板 · 768"], ["1280", "桌面 · 1280"]]) { const option = make("option", "", label); option.value = value; size.append(option); }
  const resize = () => { surface.style.width = size.value ? `${size.value}px` : "100%"; surface.style.maxWidth = "100%"; void mount.visible?.(shown()); };
  size.onchange = resize;
  viewport.append(size, action("monitor", "横竖屏", () => { const width = surface.getBoundingClientRect().height; surface.style.width = `${Math.round(width)}px`; void mount.visible?.(shown()); }));
  const preview = action("monitor", "设备预览", () => { viewport.hidden = !viewport.hidden; preview.setAttribute("aria-pressed", String(!viewport.hidden)); });
  preview.setAttribute("aria-pressed", String(!viewport.hidden));
  const more = action("more", "浏览器更多操作", () => {});
  forward.classList.add("wb-browser-forward"); manual.classList.add("wb-browser-manual"); preview.classList.add("wb-browser-preview-toggle");
  toolbar.append(back, forward, reload, address, manual, preview, more);
  region.append(surface, message); root.append(toolbar, viewport, status, region); root.classList.add("wb-browser-view");
  const openSystem = () => openExternal(safeBrowserUrl(state.url === "about:blank" ? browserLocation(address.value) : state.url));
  const menu = popupMenu(more, () => [
    { label: "前进", execute: () => execute("forward"), disabled: !state.can_go_forward },
    { label: state.manual ? "交还给 Agent" : "接管浏览器", checked: Boolean(state.manual), execute: async () => { state = { ...state, ...await execute(state.manual ? "release" : "takeover") }; paint(); } },
    { label: "设备预览", checked: !viewport.hidden, execute: () => preview.click() },
    { label: "在系统浏览器打开", execute: openSystem, disabled: !state.url || state.url === "about:blank" },
    { label: "复制网址", execute: () => navigator.clipboard.writeText(state.url), disabled: !state.url || state.url === "about:blank" },
    { label: "引用页面文字到对话", execute: async () => {
      const result = await execute("snapshot");
      if (!result.ok) throw new Error(result.error || "读取页面失败");
      addContext({ title: state.title || state.url, content: result.value.text, source: state.url, context: row.context });
    }, disabled: browserPhase(state) !== "ready" },
    { label: "复制诊断信息", execute: () => navigator.clipboard.writeText(JSON.stringify({ platform: state.platform, phase: browserPhase(state), loading: state.loading, visible: state.visible, bounds: state.bounds, viewport_attached: state.viewport_attached, viewport_topmost: state.viewport_topmost, controller_bounds: state.controller_bounds, error: state.error, error_code: state.error_code }, null, 2)) },
    { label: "结束浏览", execute: async () => {
      await sdk.call("browser.close", { id: browser.id }, { signal });
      await execute("close"); await onEnd(browser.id);
    } },
  ], { signal, onError: errorToast });
  const title = make("h3"), detail = make("p"), retry = action("refresh", "重新加载", () => recover()), system = action("globe", "在系统浏览器打开", openSystem);
  message.append(icon("globe"), title, detail, retry, system);
  const paint = () => {
    if (signal.aborted) return;
    const phase = browserPhase(state); root.dataset.browserPhase = phase;
    mount.status = { status: phase === "error" ? "error" : phase === "loading" ? "loading" : "ready", message: state.error || "" };
    const empty = phase === "empty", failed = phase === "error";
    message.hidden = !empty && !failed; message.classList.toggle("wb-error", failed);
    title.textContent = failed ? "网页未能打开" : "浏览器";
    detail.textContent = failed ? `${state.error}${state.error_code === undefined ? "" : `（${state.platform || "浏览器"} ${state.error_code}）`}` : "粘贴或输入网址，按回车打开网页。";
    retry.hidden = !failed || !bridge.available(); system.hidden = !failed || !state.url || state.url === "about:blank";
    status.textContent = phase === "loading" ? "正在加载…" : state.manual ? "已由你接管 · Agent 暂停操作" : "";
    status.hidden = !status.textContent;
    if (document.activeElement !== address) address.value = state.url === "about:blank" || empty ? "" : state.url || address.value;
    back.disabled = !state.can_go_back; forward.disabled = !state.can_go_forward;
    reload.setAttribute("aria-label", state.loading ? "停止加载" : "刷新"); reload.title = state.loading ? "停止加载" : "刷新";
    reload.replaceChildren(icon(state.loading ? "stop" : "refresh"));
    manual.setAttribute("aria-pressed", String(Boolean(state.manual))); manual.title = state.manual ? "交还给 Agent" : "接管浏览器"; manual.setAttribute("aria-label", manual.title);
    void mount.visible?.(shown());
  };
  mount.visible = value => {
    const bounds = rect(surface), overlay = [...document.querySelectorAll("dialog[open], [role=menu]:not([hidden])")].some(node => node.getClientRects().length);
    const params = { bounds, visible: value && bounds.width > 0 && bounds.height > 0 && !overlay && browserPhase(state) !== "empty" && !state.error };
    const identity = JSON.stringify(params); if (identity === lastBounds || !bridge.available()) return Promise.resolve(); lastBounds = identity;
    return execute("bounds", params).catch(() => { lastBounds = ""; });
  };
  mount.dispose = () => {
    clearTimeout(timer); menu.dispose();
    if (bridge.available()) void bridge.call("bounds", { browser: browser.id, bounds: rect(surface), visible: false }).catch(() => {});
  };
  mount.observer = new ResizeObserver(() => { if (!signal.aborted) void mount.visible(shown()); }); mount.observer.observe(surface);
  const initialize = async () => {
    await execute("create", { url: browser.url || "about:blank", bounds: rect(surface), visible: false });
    if (signal.aborted) { mount.dispose(); return; }
    lastBounds = ""; await mount.visible(shown());
  };
  const recover = async () => {
    if (connecting || signal.aborted) return;
    connecting = true;
    try {
      if (state.process_failed || state.bridge_error) { await execute("close"); await initialize(); }
      if (state.url && state.url !== "about:blank") await execute("navigate", { url: safeBrowserUrl(state.url) });
      else if (address.value.trim()) await execute("navigate", { url: browserLocation(address.value) });
      state = { ...state, error: "", error_code: undefined, bridge_error: false, process_failed: false, loading: true }; paint(); void poll();
    } catch (error) { state = { ...state, loading: false, error: error.message, bridge_error: true }; paint(); }
    finally { connecting = false; }
  };
  const poll = async () => {
    clearTimeout(timer);
    if (polling || signal.aborted) return;
    polling = true;
    try {
      state = await execute("state"); if (signal.aborted) return; paint();
      const location = /^https?:\/\//.test(state.url || "") ? state.url : "";
      const label = state.title || (location && new URL(location).hostname) || "浏览器";
      if (location && (location !== browser.url || label !== browser.title)) {
        try { browser = await sdk.call("browser.update", { id: browser.id, revision: browser.revision, url: location, title: label.slice(0, 256) }, { signal }); }
        catch (error) { if (error.status === 409) browser = await sdk.call("browser.get", { id: browser.id }, { signal }); else throw error; }
      }
      if (lastTitle !== label) { lastTitle = label; await updateTitle(label.slice(0, 256)); }
      const report = { state: browserPhase(state), url: location.slice(0, 1024), title: label.slice(0, 256), loading: Boolean(state.loading), error: (state.error || "").slice(0, 512) };
      const identity = JSON.stringify(report);
      if (identity !== lastReport) { await sdk.call("browser.report", { id: browser.id, client: clientId, value: report }, { signal }); lastReport = identity; }
      mount.status = { status: state.error ? "error" : state.loading ? "loading" : "ready", message: state.error || "" };
    } catch (error) {
      if (!signal.aborted) { state = { ...state, loading: false, error: `浏览器状态暂不可用：${error.message}`, bridge_error: true }; mount.status = { status: "error", message: state.error }; paint(); }
    } finally {
      polling = false;
      if (!signal.aborted) timer = setTimeout(() => { if (!root.hidden && !document.hidden) void poll(); else timer = setTimeout(poll, 1500); }, state.loading ? 400 : 1500);
    }
  };
  toolbar.onsubmit = event => {
    event.preventDefault(); if (event.isComposing || signal.aborted) return;
    void (async () => {
      const url = browserLocation(address.value); assertDisplayUrl(url);
      await execute("navigate", { url }); state = { ...state, url, error: "", loading: true }; paint(); void poll();
    })().catch(errorToast);
  };
  paint();
  if (!bridge.available()) { state = { ...state, error: "完整浏览需要在 mdo 客户端打开，当前窗口没有原生浏览器后端。", loading: false }; paint(); return { id: browser.id, execute }; }
  await initialize();
  if (!signal.aborted) void poll();
  return { id: browser.id, execute };
}
