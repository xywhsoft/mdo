import { ensureNativeBrowser, createNativeBrowserExecutor } from "./native-view.js";
import { browserScopeKey } from "./browser-ownership.js";
import { createBrowserDownloads } from "./download-ui.js";
import { createBrowserChoosers } from "./chooser-ui.js";
import { createBrowserDialogs } from "./dialog-ui.js";
import { setUiAttribute as writeUiAttribute, setUiText as writeUiText, t as uiText, uiMessage } from "../../i18n.js";

import { browserLocation, browserPhase, safeBrowserUrl } from "./model.js";
import { element as make, icon, iconButton, popupMenu } from "./controls.js";
import { toast } from "../../utils/dom.js";
import { isRemoteTarget } from "../../api/target.js";

const rect = node => { const r = node.getBoundingClientRect(); return { x: Math.round(r.x), y: Math.round(r.y), width: Math.round(r.width), height: Math.round(r.height), scale: devicePixelRatio || 1 }; };
const usableBounds = bounds => [bounds.x, bounds.y, bounds.width, bounds.height, bounds.scale].every(Number.isFinite) && bounds.width > 0 && bounds.height > 0 && bounds.scale > 0;
function assertDisplayUrl(url) {
  if (url && isRemoteTarget() && ["localhost", "127.0.0.1", "[::1]"].includes(new URL(url).hostname))
    throw new Error(uiText("ui.browserView.thisUrlPointsToALocalServiceOnThe", {}, uiText("ui.browserView.thisUrlPointsToALocalServiceOnThe", {}, "此地址指向目标设备的本地服务。请先映射预览服务，或提供可访问的网址。")));
}

function controlError(command, failure, cleanup) {
  const reason = failure?.code || failure?.message || failure;
  const code = typeof reason === "string" && /^[a-z][a-z0-9_]{1,79}$/.test(reason) ? reason : "browser_control_unconfirmed";
  const messages = {
    network_retiring: "上一轮浏览器操作仍在清理，请等待后再操作。",
    network_starting: "浏览器正在确认控制权，请稍后再操作。",
    network_rearm_page_loading: "请等待网页加载结束后再交还控制。",
    network_return_blocked: "请先结束当前输入、文件选择或截图，再交还控制。",
    network_owner_changed: "浏览器状态已变化，请检查原标签。",
    network_owner_unavailable: "浏览器控制通道暂不可用，请检查原标签。",
    network_start_timeout: "控制权确认超时，请检查原标签。",
    network_start_cancelled: "已停止交还控制。",
    network_control_timeout: "接管确认超时，请检查原标签和清理状态。",
    network_control_cancelled: "本次接管已停止，请检查原标签的实际控制状态。",
    network_control_cleanup_unconfirmed: "旧网络资源的清理尚未确认，请检查原标签。",
    network_control_scheduler_unavailable: "暂时无法开始接管，请稍后重试。",
    network_control_unconfirmed: "接管结果尚未确认，请检查原标签。",
    cdp_popup_human_control_expired: "本次接管已超时，请检查原标签。",
    cdp_popup_human_control_cancelled: "本次接管已取消。",
    cdp_popup_human_owner_invalid: "原标签或控制通道已变化，请检查当前页面。",
    cdp_popup_human_resource_cancel_failed: "原页面的操作尚未完成清理，请检查原标签。",
    cdp_popup_human_timer_unavailable: "暂时无法开始接管，请稍后重试。",
    popup_takeover_unqualified: "当前客户端暂不支持接管此弹出页。",
    browser_control_result_invalid: "客户端未返回有效的控制权确认，请检查原标签。",
    browser_control_pending: "控制权操作正在进行，请等待本次结果。",
  };
  const message = messages[code] || "控制结果未确认，请检查原标签，避免重复操作。";
  const receipt = cleanup === true ? "本次网络操作已清理。" : cleanup === false ? "原生网络资源清理尚未确认，请先检查原标签。" : "";
  return Object.assign(new Error(`${command === "release" ? "交还控制失败" : "接管失败"}：${message}${receipt}`),
    { code, command, ...(typeof cleanup === "boolean" ? { network_cleanup_confirmed: cleanup } : {}) });
}

export async function mountBrowserView(root, { row, sdk, signal, mount, bridge, clientId, shown, alive = () => !signal.aborted,
  canHide = () => false, onBind = () => true, onDispose, onRetired, updateTitle, openExternal, onEnd, addContext }) {
  const id = row.resource.id, scope = browserScopeKey(row);
  const execute = createNativeBrowserExecutor(bridge, id), view = { id, execute };
  let disposed = false, retired = false, brokerInvalid = false, initialized = false, initialBoundsWaiter, controlPending = "", controlEpoch = 0, controlNotice;
  const current = () => !disposed && !retired && !brokerInvalid && !signal.aborted && alive();
  const check = () => { if (!current()) throw new DOMException("浏览器所属窗口或设备已改变", "AbortError"); };
  const ready = () => { check(); if (!initialized) throw new DOMException("浏览器正在初始化", "AbortError"); };
  const errorToast = error => { if (current() && error.name !== "AbortError") toast(error.message || String(error), "error"); };
  const uiExecute = async (command, params) => {
    ready();
    const control = command === "takeover" || command === "release";
    if (control && controlPending) throw controlError(command, "browser_control_pending");
    if (control) { controlPending = command; controlEpoch++; controlNotice = undefined; paint(false); }
    try {
      const result = await execute(command, params); check();
      if (control) {
        if (result?.ok === false) throw controlError(command, result.error, result.network_cleanup_confirmed);
        if (!result || Array.isArray(result) || typeof result !== "object" || result.manual !== (command === "takeover") ||
            result.error || result.ok !== undefined && result.ok !== true) throw controlError(command, "browser_control_result_invalid");
        // Control receipts are not page state. In particular, never merge a
        // structured failure into state.error and hide the physical webpage.
        state = { ...state, manual: result.manual };
      }
      return result;
    } catch (error) {
      if (!control || error?.name === "AbortError") throw error;
      const failure = error?.command === command ? error : controlError(command, error);
      if (current()) controlNotice = failure;
      throw failure;
    } finally {
      if (control) { controlPending = ""; if (current()) paint(false); }
    }
  };
  const call = async (method, params) => { check(); const result = await sdk.call(method, params, { signal }); check(); return result; };
  const owned = (value, attached = true) => {
    if (!value || value.id !== id || browserScopeKey(value) !== scope || attached && value.client !== clientId)
      throw Object.assign(new Error("浏览器归属已改变，请重新打开原标签。"), { code: "browser_owner_changed" });
    return value;
  };
  let browser;
  try {
    browser = owned(await call("browser.get", { id }), false);
    if (browser.client !== clientId) browser = owned(await call("browser.attach", { id, revision: browser.revision, client: clientId }));
    assertDisplayUrl(browser.url); check();
  } catch (error) { if (!current()) return view; throw error; }
  mount.browserId = id;
  let state = { url: browser.url || "about:blank", loading: Boolean(browser.url) }, timer, lastBounds = "", retainedBounds, lastReport = "", lastTitle = row.title || "", polling = false, connecting = false;
  const toolbar = make("form", "wb-browser-toolbar"), address = make("input", "wb-address"), region = make("div", "wb-browser-region"), surface = make("div", "wb-browser-surface"), message = make("div", "wb-browser-message"), status = make("span", "wb-browser-status");
  writeUiAttribute(address, "aria-label", uiMessage("ui.browserView.searchOrEnterAUrl", {}, uiText("ui.browserView.searchOrEnterAUrl", {}, "搜索或输入网址"))); writeUiAttribute(address, "placeholder", uiMessage("ui.browserView.searchOrEnterAUrl", {}, uiText("ui.browserView.searchOrEnterAUrl", {}, "搜索或输入网址"))); address.spellcheck = false; address.autocomplete = "off";
  address.value = browser.url || ""; status.setAttribute("role", "status");
  const action = (name, label, run) => iconButton(name, label, () => { check(); return run(); }, errorToast);
  const back = action("arrow-left", uiText("ui.browserView.back", {}, "后退"), () => uiExecute("back")), forward = action("arrow-right", uiText("ui.browserView.forward", {}, "前进"), () => uiExecute("forward"));
  const reload = action("refresh", uiText("resource.refresh", {}, "刷新"), () => state.loading ? uiExecute("stop") : recover());
  const takeControl = () => uiExecute(state.manual ? "release" : "takeover");
  const manual = action("hand", uiText("ui.browserView.takeOverBrowser", {}, "接管浏览器"), takeControl);
  const viewport = make("div", "wb-viewport-toolbar"); viewport.hidden = row.type !== "mdo.preview";
  const size = make("select", "wb-viewport-size"); size.setAttribute("aria-label", uiText("ui.browserView.previewWidth", {}, "预览宽度"));
  for (const [value, label] of [["", uiText("ui.panelManager.responsive", {}, "自适应")], ["390", uiText("ui.browserView.phone390", {}, "手机 · 390")], ["768", uiText("ui.browserView.tablet768", {}, "平板 · 768")], ["1280", uiText("ui.browserView.desktop1280", {}, "桌面 · 1280")]]) { const option = make("option", "", label); option.value = value; size.append(option); }
  const resize = () => { if (!current()) return; surface.style.width = size.value ? `${size.value}px` : "100%"; surface.style.maxWidth = "100%"; void syncVisible(shown()); };
  size.onchange = resize;
  viewport.append(size, action("monitor", uiText("ui.panelManager.orientation", {}, "横竖屏"), () => { const width = surface.getBoundingClientRect().height; surface.style.width = `${Math.round(width)}px`; void syncVisible(shown()); }));
  const preview = action("monitor", uiText("ui.browserView.devicePreview", {}, "设备预览"), () => { viewport.hidden = !viewport.hidden; preview.setAttribute("aria-pressed", String(!viewport.hidden)); });
  preview.setAttribute("aria-pressed", String(!viewport.hidden));
  const more = action("more", uiMessage("ui.browserView.moreBrowserActions", {}, uiText("ui.browserView.moreBrowserActions", {}, "浏览器更多操作")), () => {});
  forward.classList.add("wb-browser-forward"); manual.classList.add("wb-browser-manual"); preview.classList.add("wb-browser-preview-toggle");
  const downloadButton=action("download","下载",()=>{downloadButton.setAttribute("aria-expanded",String(downloads.toggle()));});
  downloadButton.classList.add("wb-browser-download-toggle");downloadButton.setAttribute("aria-expanded","false");
  toolbar.append(back, forward, reload, address, downloadButton, manual, preview, more);
  const control = () => { if (current()) { state = { ...state, manual: true }; paint(); } }, changed = () => { void syncVisible(shown()); };
  const dialogs = createBrowserDialogs({ execute:uiExecute, onControl:control, onChange:changed, onError:errorToast });
  const choosers = createBrowserChoosers({execute:uiExecute,onControl:control,onChange:changed,onError:errorToast});
  const downloads=createBrowserDownloads({browser,sdk,execute:uiExecute,signal,alive:current,onChange:changed,
    onBadge:(native,recovery)=>{if(!current())return;const count=native+recovery;downloadButton.dataset.count=String(count);downloadButton.title=count?`下载与待处理记录（${count}）`:"下载";downloadButton.setAttribute("aria-label",downloadButton.title);}});
  region.append(surface, message); root.append(toolbar, dialogs.element, choosers.element, downloads.element, viewport, status, region); root.classList.add("wb-browser-view");
  const openSystem = () => { check(); return openExternal(safeBrowserUrl(state.url === "about:blank" ? browserLocation(address.value) : state.url)); };
  const menu = popupMenu(more, () => [
    { label: uiText("ui.browserView.forward", {}, "前进"), execute: () => uiExecute("forward"), disabled: !state.can_go_forward },
    { label: state.manual ? uiText("ui.browserView.returnControlToAgent", {}, "交还给 Agent") : uiText("ui.browserView.takeOverBrowser", {}, "接管浏览器"), checked: Boolean(state.manual), disabled: Boolean(controlPending), execute: takeControl },
    { label: uiText("ui.browserView.devicePreview", {}, "设备预览"), checked: !viewport.hidden, execute: () => preview.click() },
    { label: uiText("ui.browserView.openInSystemBrowser", {}, "在系统浏览器打开"), execute: openSystem, disabled: !state.url || state.url === "about:blank" },
    { label: uiText("ui.browserView.copyUrl", {}, "复制网址"), execute: () => { check(); return navigator.clipboard.writeText(state.url); }, disabled: !state.url || state.url === "about:blank" },
    { label: uiText("ui.browserView.quotePageTextInConversation", {}, "引用页面文字到对话"), execute: async () => {
      const result = await uiExecute("snapshot");
      if (!result.ok) throw new Error(result.error || uiText("ui.browserView.couldNotReadPage", {}, "读取页面失败"));
      addContext({ title: state.title || state.url, content: result.value.text, source: state.url, context: row.context });
    }, disabled: browserPhase(state) !== "ready" },
    { label: uiText("ui.browserView.copyDiagnosticInformation", {}, "复制诊断信息"), execute: () => { check(); return navigator.clipboard.writeText(JSON.stringify({ platform: state.platform, phase: browserPhase(state), loading: state.loading, visible: state.visible, bounds: state.bounds, viewport_attached: state.viewport_attached, viewport_topmost: state.viewport_topmost, controller_bounds: state.controller_bounds, error: state.error, error_code: state.error_code,
      control: controlNotice ? { command: controlNotice.command, code: controlNotice.code, network_cleanup_confirmed: controlNotice.network_cleanup_confirmed } : undefined }, null, 2)); } },
    { label: uiText("ui.browserView.endBrowsing", {}, "结束浏览"), execute: async () => {
      ready();
      await call("browser.close", { id });
      await uiExecute("close"); await onEnd(id);
    } },
  ], { signal, onError: errorToast });
  const title = make("h3"), detail = make("p"), retry = action("refresh", uiMessage("ui.browserView.reload", {}, uiText("ui.browserView.reload", {}, "重新加载")), () => recover()), system = action("globe", uiMessage("ui.browserView.openInSystemBrowser", {}, uiText("ui.browserView.openInSystemBrowser", {}, "在系统浏览器打开")), openSystem);
  message.append(icon("globe"), title, detail, retry, system);
  const paint = (sync = true) => {
    if (!current()) return;
    const phase = browserPhase(state); root.dataset.browserPhase = phase;
    mount.status = { status: phase === "error" ? "error" : phase === "loading" ? "loading" : "ready", message: controlNotice?.message || state.error || "" };
    const empty = phase === "empty", failed = phase === "error";
    message.hidden = !empty && !failed; message.classList.toggle("wb-error", failed);
    writeUiText(title, failed ? uiMessage("ui.browserView.couldNotOpenPage", {}, uiText("ui.browserView.couldNotOpenPage", {}, "网页未能打开")) : uiMessage("ui.startView.browser", {}, uiText("ui.startView.browser", {}, "浏览器")));
    writeUiText(detail, failed ? `${state.error}${state.error_code === undefined ? "" : `（${state.platform || uiMessage("ui.startView.browser", {}, uiText("ui.startView.browser", {}, "浏览器"))} ${state.error_code}）`}` : uiMessage("ui.browserView.pasteOrEnterAUrlAndPressEnterTo", {}, uiText("ui.browserView.pasteOrEnterAUrlAndPressEnterTo", {}, "粘贴或输入网址，按回车打开网页。")));
    retry.hidden = !failed || !bridge.available(); system.hidden = !failed || !state.url || state.url === "about:blank";
    status.textContent = controlPending ? controlPending === "release" ? "正在交还控制…" : "正在接管浏览器…" :
      controlNotice?.message || (phase === "loading" ? uiText("ui.browserView.loading", {}, "正在加载…") : state.manual ? uiText("ui.browserView.youHaveControlAgentActionsPaused", {}, "已由你接管 · Agent 暂停操作") : "");
    status.hidden = !status.textContent;
    // Routine loading is already shown by the tab and stop button. Keep its
    // live announcement without changing the native viewport during navigation.
    // Control/recovery notices remain visible so their cleanup guidance is usable.
    status.classList.toggle("sr-only", phase === "loading" && !controlPending && !controlNotice?.message && !state.manual);
    if (document.activeElement !== address && !state.initial_navigation_pending) address.value = state.url === "about:blank" || empty ? "" : state.url || address.value;
    back.disabled = !state.can_go_back; forward.disabled = !state.can_go_forward;
    writeUiAttribute(reload, "aria-label", state.loading ? uiMessage("ui.browserView.stopLoading", {}, uiText("ui.browserView.stopLoading", {}, "停止加载")) : uiMessage("resource.refresh", {}, uiText("resource.refresh", {}, "刷新"))); writeUiAttribute(reload, "title", state.loading ? uiMessage("ui.browserView.stopLoading", {}, uiText("ui.browserView.stopLoading", {}, "停止加载")) : uiMessage("resource.refresh", {}, uiText("resource.refresh", {}, "刷新")));
    reload.replaceChildren(icon(state.loading ? "stop" : "refresh"));
    manual.setAttribute("aria-pressed", String(Boolean(state.manual))); manual.title = state.manual ? uiText("ui.browserView.returnControlToAgent", {}, "交还给 Agent") : uiText("ui.browserView.takeOverBrowser", {}, "接管浏览器"); manual.setAttribute("aria-label", manual.title);
    manual.disabled = Boolean(controlPending); manual.setAttribute("aria-busy", String(Boolean(controlPending)));
    if (sync) void syncVisible(shown());
  };
  const syncVisible = value => {
    if (!current() || !initialized) return Promise.resolve();
    const bounds = rect(surface), overlay = [...document.querySelectorAll("dialog[open], [role=menu]:not([hidden])")].some(node => node.getClientRects().length);
    const visible = value && bounds.width > 0 && bounds.height > 0 && !overlay && browserPhase(state) !== "empty" && !state.error;
    // display:none collapses the DOM surface. Hiding must not resize the native
    // document and trigger responsive-page changes while a decision is pending.
    if (visible || !retainedBounds && bounds.width > 0 && bounds.height > 0) retainedBounds = { ...bounds };
    const params = { bounds: visible ? bounds : retainedBounds || bounds, visible };
    const identity = JSON.stringify(params); if (identity === lastBounds || !bridge.available()) return Promise.resolve(); lastBounds = identity;
    return uiExecute("bounds", params).catch(() => { if (current() && lastBounds === identity) lastBounds = ""; });
  };
  const initialBounds = () => {
    check(); const bounds = rect(surface);
    if (usableBounds(bounds)) return Promise.resolve(bounds);
    // A hidden pane has no viewport yet. A 1px native fallback changes Android
    // page layout/zoom before reveal. Retain only this pane's layout wait; no
    // navigation or native allocation occurs until its actual surface exists.
    return new Promise((resolve, reject) => {
      const finish = (error, value) => {
        signal.removeEventListener("abort", sample); initialBoundsWaiter = undefined;
        if (error) reject(error); else resolve(value);
      };
      const sample = () => {
        if (!current()) { finish(new DOMException("浏览器所属窗口或设备已改变", "AbortError")); return; }
        const value = rect(surface); if (usableBounds(value)) finish(null, value);
      };
      initialBoundsWaiter = sample; signal.addEventListener("abort", sample, { once: true }); sample();
    });
  };
  mount.visible = value => {
    initialBoundsWaiter?.();
    if (!current()) { if (value && !disposed && !brokerInvalid && !signal.aborted) onRetired?.(); return Promise.resolve(); }
    return syncVisible(value);
  };
  mount.dispose = () => {
    if (disposed) return;
    const hide = !brokerInvalid && canHide(); disposed = true; onDispose?.();
    initialBoundsWaiter?.();
    clearTimeout(timer); downloads.dispose(); choosers.dispose(); dialogs.dispose(); menu.dispose();
    if (hide && bridge.available() && retainedBounds) void execute("bounds", { bounds: retainedBounds, visible: false }).catch(() => {});
  };
  mount.observer = new ResizeObserver(() => { initialBoundsWaiter?.(); void syncVisible(shown()); }); mount.observer.observe(surface);
  const initialize = async (refresh = false) => {
    const bounds = await initialBounds(); check();
    if (refresh) browser = owned(await call("browser.get", { id }));
    await ensureNativeBrowser({bridge,sdk,browser,clientId,bounds,alive:current,cancelOnDetach:false,
      requireAutomation:row.params?.automation_required===true,
      beforeCreate:()=>{check();if(!onBind(view)){retired=true;check();}retainedBounds={...bounds};}});
    check(); initialized = true; lastBounds = ""; await syncVisible(shown()); check();
  };
  const failure = (error, prefix = "") => {
    if (!current()) return;
    state = { ...state, loading: false, error: `${prefix}${error.message}`, bridge_error: true };
    mount.status = { status: "error", message: state.error };
    // A broker rejection is not permission to hide/recreate a foreign native view.
    paint(error.code !== "browser_owner_changed");
    if (error.code === "browser_owner_changed") { brokerInvalid = true; retry.disabled = true; clearTimeout(timer); }
  };
  const recover = async () => {
    if (connecting || !current() || !initialized) return;
    connecting = true;
    try {
      browser = owned(await call("browser.get", { id }));
      if (state.process_failed || state.bridge_error) { await uiExecute("close"); initialized = false; await initialize(true); }
      if (state.url && state.url !== "about:blank") await uiExecute("navigate", { url: safeBrowserUrl(state.url) });
      else if (address.value.trim()) await uiExecute("navigate", { url: browserLocation(address.value) });
      check();
      state = { ...state, error: "", error_code: undefined, bridge_error: false, process_failed: false, loading: true }; paint(); void poll();
    } catch (error) { failure(error); }
    finally { connecting = false; }
  };
  const poll = async () => {
    clearTimeout(timer);
    if (polling || !current()) return;
    polling = true;
    const epoch = controlEpoch;
    try {
      const next = await uiExecute("state");
      // A state read dispatched before a control receipt cannot undo it. No
      // polling response grants UI authority while that receipt is pending.
      if (controlPending || epoch !== controlEpoch) return;
      state = next; await dialogs.refresh(state); check(); await choosers.refresh(state); check(); await downloads.refresh(state); check(); paint();
      const location = /^https?:\/\//.test(state.url || "") ? state.url : "";
      const label = state.title || (location && new URL(location).hostname) || uiText("ui.startView.browser", {}, uiText("ui.startView.browser", {}, "浏览器"));
      if (location && (location !== browser.url || label !== browser.title)) {
        try { browser = owned(await call("browser.update", { id, revision: browser.revision, url: location, title: label.slice(0, 256) })); }
        catch (error) { if (error.status === 409) browser = owned(await call("browser.get", { id })); else throw error; }
      }
      if (lastTitle !== label) { await updateTitle(label.slice(0, 256)); check(); lastTitle = label; }
      const report = { state: browserPhase(state), url: location.slice(0, 1024), title: label.slice(0, 256), loading: Boolean(state.loading), error: (state.error || "").slice(0, 512) };
      const identity = JSON.stringify(report);
      if (identity !== lastReport) { await call("browser.report", { id, client: clientId, value: report }); lastReport = identity; }
      mount.status = { status: state.error ? "error" : state.loading ? "loading" : "ready", message: controlNotice?.message || state.error || "" };
    } catch (error) {
      if (!controlPending && epoch === controlEpoch) failure(error, "浏览器状态暂不可用：");
    } finally {
      polling = false;
      if (current()) timer = setTimeout(() => { if (!current()) return; if (!root.hidden && !document.hidden) void poll(); else timer = setTimeout(poll, 1500); }, state.loading ? 400 : 1500);
    }
  };
  toolbar.onsubmit = event => {
    event.preventDefault(); if (event.isComposing || !current() || !initialized) return;
    void (async () => {
      const url = browserLocation(address.value); assertDisplayUrl(url);
      await uiExecute("navigate", { url }); state = { ...state, url, error: "", loading: true }; paint(); void poll();
    })().catch(errorToast);
  };
  paint();
  if (!bridge.available()) { state = { ...state, error: uiText("ui.browserView.fullBrowsingRequiresTheMdoClientThisWindowHas", {}, "完整浏览需要在 mdo 客户端打开，当前窗口没有原生浏览器后端。"), loading: false }; paint(); return view; }
  try { await initialize(); if (current()) void poll(); }
  catch (error) { if (current()) throw error; }
  return view;
}
