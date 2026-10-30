import { setUiText as writeUiText, t as uiText, uiMessage } from "../../i18n.js";

import { openLayer } from "../../components/layers.js";
import { api } from "../../api/client.js";
import { targetState, subscribeTarget } from "../../api/target.js";
import { element, errorMessage } from "../../utils/dom.js";

export function createMcpAuthorization() {
  let view, dialog, controller, unsubscribe, timer;
  const close = () => {
    clearTimeout(timer); controller?.abort(); unsubscribe?.();
    const previous=view;view=null;dialog=null;previous?.dispose();
  };
  function open(id) {
    close(); const owner = targetState().selected?.id || "local";
    controller = new AbortController(); const { signal } = controller;
    const current = () => dialog && !signal.aborted && owner === (targetState().selected?.id || "local");
    const path = `/mcp/${encodeURIComponent(id)}/oauth`;
    view = openLayer({ title: uiMessage("ui.mcpAuthorization.mcpAuthorizationValue1", { value1: id }, "MCP 授权 · {value1}"), icon:"globe", width:"44rem", height:"80%", onClosed: close }); dialog=view.element;dialog.classList.add("extension-authoring");
    const status = element("p", { attrs: { role: "status" } });
    const destination=element("p",{className:"extension-help"});
    let busy = false, available = true;
    const buttons = [];
    const updateButtons = () => buttons.forEach((b, i) => { b.disabled = busy || (!available && i < 2); });
    const run = async fn => {
      if (!current() || busy) return;
      busy = true; buttons.forEach(b => { b.disabled = true; });
      try { await fn(); await poll(); }
      catch (error) { if (current()) writeUiText(status, errorMessage(error)); }
      finally { busy = false; if (current()) updateButtons(); }
    };
    const button = (text, fn) => {
      const node = element("button", { text, attrs: { type: "button" }, className: "secondary-button" });
      node.addEventListener("click", () => { void run(fn); }); buttons.push(node); return node;
    };
    async function poll() {
      clearTimeout(timer);
      try {
        const { data } = await api.get(path, { signal }); if (!current()) return;
        available = data.available !== false; updateButtons();
        if(data.authorization_url){try{const url=new URL(data.authorization_url);writeUiText(destination, uiMessage("ui.mcpAuthorization.authorizationServiceValue1Value2RequestedPermissionsValue3VerifyThe", { value1: url.origin, value2: url.pathname, value3: url.searchParams.get("scope")||uiMessage("ui.mcpAuthorization.confirmedByService", {}, "由服务确认") }, "授权服务：{value1}{value2}\n申请权限：{value3}\n请核对服务域名和权限，再打开授权网页。"));}catch{writeUiText(destination, uiMessage("ui.mcpAuthorization.invalidAuthorizationServiceUrlStartAuthorizationAgain", {}, "授权服务地址无效，请重新发起授权。"));}}else if(!data.busy)writeUiText(destination, "");
        const labels = { idle: uiText("ui.mcpAuthorization.waitingForAuthorization", {}, "等待授权"), discovery: uiText("ui.mcpAuthorization.discoveringAuthorizationService", {}, "正在发现授权服务…"), ready: uiText("ui.mcpAuthorization.readyOpenTheAuthorizationPage", {}, "已准备好，请打开授权网页"), exchange: uiText("ui.mcpAuthorization.completingAuthorization", {}, "正在完成授权…"), error: uiText("ui.mcpAuthorization.authorizationIncomplete", {}, "授权未完成"), unsupported: uiText("ui.mcpAuthorization.thisDeviceCannotSecurelyStoreOauthCredentials", {}, "当前设备不支持安全保存 OAuth 凭据") };
        writeUiText(status, `${data.authorized ? uiMessage("ui.mcpAuthorization.authorized", {}, "已授权") : labels[data.state] || data.state}${data.http_status >= 400 ? ` · HTTP ${data.http_status}` : ""}${data.message ? `：${data.message}` : ""}`);
        if (data.busy || data.state === "ready") timer = setTimeout(poll, 1500);
      } catch (error) { if (current()) writeUiText(status, errorMessage(error)); }
    }
    const done = element("button", { text: uiMessage("context.close", {}, "关闭"), attrs: { type: "button" } }); done.addEventListener("click", close);
    view.body.append(element("h2", { text: uiMessage("ui.mcpAuthorization.mcpAuthorizationValue1", { value1: id }, "MCP 授权 · {value1}") }),
      element("p", { text: uiMessage("ui.mcpAuthorization.theAuthorizationPageOpensInTheBrowserOnThe", {}, "授权网页将在该 MCP 所属设备的浏览器中打开。完成后返回墨斗；凭据保存在该设备的系统凭据保护下。") }),
      status, destination, button(uiMessage("ui.mcpAuthorization.startAuthorization", {}, "开始授权"), () => api.post(`${path}/start`, {}, { signal })),
      button(uiMessage("ui.mcpAuthorization.openAuthorizationPage", {}, "打开授权网页"), () => api.post(`${path}/open`, {}, { signal })),
      button(uiMessage("ui.mcpAuthorization.revokeLocalAuthorization", {}, "撤销本机授权"), async () => {
        if (window.confirm(uiText("ui.mcpAuthorization.removeTheMcpAuthorizationSavedOnThisDevice", {}, "移除此设备保存的 MCP 授权？"))) await api.delete(path, { signal });
      }));
    view.footer.append(done);
    dialog.addEventListener("cancel", event => { event.preventDefault(); close(); });

    unsubscribe = subscribeTarget(() => { if (!current()) close(); }); void poll();
  }
  return { open, destroy: close };
}
