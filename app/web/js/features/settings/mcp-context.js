import { api } from "../../api/client.js";
import { targetState, subscribeTarget } from "../../api/target.js";
import { element, clear, errorMessage } from "../../utils/dom.js";
import { mcpContextRead, mcpPromptText } from "../chat/mcp-context.js";

export function createMcpContext() {
  let dialog, controller, stop;
  const close = () => { controller?.abort(); stop?.(); dialog?.close(); dialog?.remove(); dialog = null; };
  function open(server) {
    close(); controller = new AbortController(); const { signal } = controller;
    const owner = targetState().selected?.id || "local";
    const current = () => dialog && !signal.aborted && owner === (targetState().selected?.id || "local");
    const options = { signal, isCurrent: current, onProgress: info => {
      if (current() && Number.isFinite(info.progress)) status.textContent = `读取中 · ${info.progress}${info.total > 0 ? ` / ${info.total}` : ""}${info.message ? ` · ${info.message}` : ""}`;
    } };
    dialog = element("dialog", { className: "extension-editor skill-workbench" });
    const status = element("p", { attrs: { role: "status" } }), rows = element("div"), form = element("div");
    const output = element("textarea", { className: "extension-source", attrs: { readonly: "", "aria-label": "MCP 结果" } });
    let busy = false, cursor = null, method = "resources/list";
    async function run(fn) {
      if (!current() || busy) return; busy = true; status.textContent = "读取中…";
      try { await fn(); if (current()) status.textContent = ""; }
      catch (error) { if (current()) status.textContent = errorMessage(error); }
      finally { busy = false; }
    }
    const button = (text, fn) => {
      const b = element("button", { text, className: "secondary-button", attrs: { type: "button" } });
      b.addEventListener("click", () => { void run(fn); }); return b;
    };
    async function list(next = false) {
      const result = await mcpContextRead(server, method, next && cursor ? { cursor } : {}, options);
      if (!current()) return; clear(rows); clear(form); cursor = result.nextCursor || null;
      for (const item of result.resources || result.resourceTemplates || result.prompts || []) {
        const action = button(item.name || item.uri || item.uriTemplate, async () => {
          clear(form);
          if (method === "prompts/list") {
            const inputs = (item.arguments || []).map(arg => {
              const input = element("input", { attrs: { "aria-label": arg.name, placeholder: arg.description || arg.name } });
              form.append(element("label", {}, [element("span", { text: `${arg.name}${arg.required ? " *" : ""}` }), input])); return { arg, input };
            });
            form.append(button("读取提示词", async () => {
              if (inputs.some(({ arg, input }) => arg.required && !input.value)) throw new Error("请填写必需参数。");
              const args = Object.fromEntries(inputs.filter(({ input }) => input.value).map(({ arg, input }) => [arg.name, input.value]));
              const data = await mcpContextRead(server, "prompts/get", { name: item.name, arguments: args }, options);
              if (current()) output.value = mcpPromptText(data);
            }));
          } else {
            const uri = element("input", { attrs: { "aria-label": "资源 URI", value: item.uri || item.uriTemplate || "" } });
            form.append(uri, button("读取资源", async () => {
              const data = await mcpContextRead(server, "resources/read", { uri: uri.value }, options);
              if (current()) output.value = JSON.stringify(data, null, 2);
            }));
          }
        });
        rows.append(element("article", {}, [action, element("p", { text: item.description || item.uriTemplate || item.uri || "" })]));
      }
      if (cursor) rows.append(button("下一页", () => list(true)));
    }
    const done = element("button", { text: "关闭", attrs: { type: "button" } }); done.addEventListener("click", close);
    dialog.append(element("h2", { text: `MCP 资源与提示词 · ${server}` }),
      element("p", { text: "资源模板需填写实际 URI。提示词可在会话中通过 /mcp 服务器 提示词名称 {参数JSON} 调用；返回内容作为普通会话输入。" }),
      ...[["资源", "resources/list"], ["模板", "resources/templates/list"], ["提示词", "prompts/list"]].map(([label, name]) => button(label, () => { method = name; return list(); })),
      button("诊断", async () => { const { data } = await api.get(`/mcp/${encodeURIComponent(server)}/diagnostics`, { signal }); if (current()) output.value = JSON.stringify(data, null, 2); }),
      rows, form, output, status, done);
    dialog.addEventListener("cancel", event => { event.preventDefault(); close(); });
    document.body.append(dialog); dialog.showModal(); stop = subscribeTarget(() => { if (!current()) close(); });
  }
  return { open, destroy: close };
}
