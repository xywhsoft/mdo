/* The content WebViews have no host bindings. Only the trusted main document
 * can call this bounded, request/response bridge. */
export function createNativeBrowserBridge() {
  const pending = new Map(); let closed = false;
  const receive = value => {
    let message; try { message = typeof value === "string" ? JSON.parse(value) : value; } catch { return; }
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
      if (closed || signal?.aborted) return Promise.reject(new DOMException("操作已取消", "AbortError"));
      if (!wv?.postMessage && !android()?.request) return Promise.reject(new Error("当前窗口没有原生浏览器后端"));
      if (pending.size >= 32) return Promise.reject(new Error("浏览器正在处理过多请求"));
      const id = crypto.randomUUID();
      return new Promise((resolve, reject) => {
        const finish = callback => value => { signal?.removeEventListener("abort", abort); callback(value); };
        const abort = () => { const request = pending.get(id); if (!request) return; clearTimeout(request.timer); pending.delete(id); finish(reject)(new DOMException("操作已取消；已发送的动作需核实结果", "AbortError")); };
        const timer = setTimeout(() => { pending.delete(id); finish(reject)(new Error("浏览器操作超时，执行结果未确认，请读取页面状态")); }, timeout);
        pending.set(id, { resolve: finish(resolve), reject: finish(reject), timer, command });
        signal?.addEventListener("abort", abort, { once: true });
        try {
          const message = JSON.stringify({ ...params, channel: "mdo-browser", id, command });
          if (message.length > 262144) throw new Error("浏览器请求过大");
          if (wv?.postMessage) wv.postMessage(message); else android().request(message);
        } catch (error) { pending.delete(id); clearTimeout(timer); finish(reject)(error); }
      });
    },
    busy: () => [...pending.values()].some(request => ["create","navigate","back","forward","reload","stop","click","type","scroll","evaluate"].includes(request.command)),
    dispose() {
      closed = true; wv?.removeEventListener("message", listener); window.removeEventListener("mdo-browser-result", androidListener);
      for (const request of pending.values()) { clearTimeout(request.timer); request.reject(new Error("浏览器窗口已退出")); } pending.clear();
    },
  });
}
