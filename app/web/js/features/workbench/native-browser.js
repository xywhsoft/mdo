import { t as uiText } from "../../i18n.js";
/* The content WebViews have no host bindings. Only the trusted main document
 * can call this bounded, request/response bridge. */
export function createNativeBrowserBridge() {
  const pending = new Map(), creating = new Map(); let closed = false;
  const receive = value => {
    let message; try { message = typeof value === "string" ? JSON.parse(value) : value; } catch { return; }
    if (message?.channel === "mdo-browser-popup") { window.dispatchEvent(new CustomEvent("mdo-browser-popup", {detail:message})); return; }
    if (message?.channel === "mdo-browser-shortcut") { window.dispatchEvent(new CustomEvent("mdo-browser-shortcut", { detail:message })); return; }
    if (message?.channel !== "mdo-browser" || !pending.has(message.id)) return;
    const request = pending.get(message.id); pending.delete(message.id); clearTimeout(request.timer);
    if (message.error) request.reject(new Error(message.error)); else request.resolve(message.result);
  };
  const listener = event => receive(event.data);
  const wv = globalThis.chrome?.webview, android = () => globalThis.XsBrowser;
  wv?.addEventListener("message", listener);
  const androidListener = event => receive(event.detail);
  window.addEventListener("mdo-browser-result", androidListener);
  return Object.freeze({
    available: () => Boolean(wv?.postMessage || android()?.request),
    call(command, params = {}, { signal, timeout = 20000 } = {}) {
      if (closed || signal?.aborted) return Promise.reject(new DOMException(uiText("ui.nativeBrowser.operationCancelled", {}, "操作已取消"), "AbortError"));
      if (!wv?.postMessage && !android()?.request) return Promise.reject(new Error(uiText("ui.nativeBrowser.thisWindowHasNoNativeBrowserBackend", {}, "当前窗口没有原生浏览器后端")));
      if (pending.size >= 32) return Promise.reject(new Error(uiText("ui.nativeBrowser.theBrowserIsProcessingTooManyRequests", {}, "浏览器正在处理过多请求")));
      const id = crypto.randomUUID();
      if (command === "close") creating.delete(params.browser);
      if (command === "create" && creating.has(params.browser)) {
        const active = creating.get(params.browser);
        if(active.interception !== (params.intercept_downloads===true))return Promise.reject(new Error("原生浏览器正在使用不同的下载拦截策略创建，请检查已有标签。"));
        if (active.popup !== (params.popup || "") || active.opener !== (params.opener || "")) return Promise.reject(new Error("原生浏览器正在为另一个弹出请求创建"));
        return active.promise;
      }
      const result = new Promise((resolve, reject) => {
        const finish = callback => value => { signal?.removeEventListener("abort", abort); callback(value); };
        const abort = () => { const request = pending.get(id); if (!request) return; clearTimeout(request.timer); pending.delete(id); finish(reject)(new DOMException(uiText("ui.nativeBrowser.operationCancelledVerifyTheResultOfActionsAlreadySent", {}, "操作已取消；已发送的动作需核实结果"), "AbortError")); };
        const timer = setTimeout(() => { pending.delete(id); finish(reject)(new Error(uiText("ui.nativeBrowser.browserOperationTimedOutAndTheResultIsUnconfirmed", {}, "浏览器操作超时，执行结果未确认，请读取页面状态"))); }, timeout);
        pending.set(id, { resolve: finish(resolve), reject: finish(reject), timer, command });
        signal?.addEventListener("abort", abort, { once: true });
        try {
          const message = JSON.stringify({ ...params, channel: "mdo-browser", id, command });
          if (message.length > 262144) throw new Error(uiText("ui.nativeBrowser.browserRequestIsTooLarge", {}, "浏览器请求过大"));
          if (wv?.postMessage) wv.postMessage(message); else android().request(message);
        } catch (error) { pending.delete(id); clearTimeout(timer); finish(reject)(error); }
      });
      if (command !== "create") return result;
      const promise = result.finally(()=>{if(creating.get(params.browser)?.promise===promise)creating.delete(params.browser);});
      creating.set(params.browser,{promise,popup:params.popup||"",opener:params.opener||"",interception:params.intercept_downloads===true});
      return promise;
    },
    busy: () => [...pending.values()].some(request => ["create","navigate","back","forward","reload","stop","click","type","scroll","evaluate","input","automation","dialog","capture","capture_cancel","drag","drag_cancel","upload","chooser","downloads","frames","popup_prepare"].includes(request.command)),
    dispose() {
      closed = true; wv?.removeEventListener("message", listener); window.removeEventListener("mdo-browser-result", androidListener);
      for (const request of pending.values()) { clearTimeout(request.timer); request.reject(new Error(uiText("ui.nativeBrowser.browserWindowExited", {}, "浏览器窗口已退出"))); } pending.clear();
    },
  });
}
