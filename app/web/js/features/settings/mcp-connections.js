import { api } from "../../api/client.js";
import { targetState, subscribeTarget } from "../../api/target.js";
import { element, errorMessage, toast } from "../../utils/dom.js";
import { openLayer } from "../../components/layers.js";
import { mcpVariantInputs, mcpTemplateSummary } from "./mcp-template.js";

const owner = () => targetState().selected?.id || "local";
const button = (text, action, primary = false) => {
  const b = element("button", { text, className: primary ? "primary-button" : "secondary-button", attrs: { type: "button" } });
  b.addEventListener("click", () => { void action(); }); return b;
};

export function createMcpConnections(onChange) {
  let layer = null, dirty = false, saving = false;
  const close = (force = false) => { if (!layer) return true; if (!force && (saving || dirty && !confirm("放弃未保存的连接配置？"))) return false; layer.dispose(); return true; };
  async function open({ item = null, receipt = null, template = null, project = "" } = {}) {
    if (!close()) return;
    const target = owner();
    const view = openLayer({ title: item ? `配置连接 · ${item.name || item.id}` : "从模板创建 MCP 连接", icon: "globe", width: "54rem", height: "85%",
      beforeClose: () => !saving && (!dirty || confirm("放弃未保存的连接配置？")),
      onClosed() { if (layer === view) { layer = null; dirty = saving = false; } } }); layer = view;view.element.classList.add("extension-authoring");
    const live = () => layer === view && !view.signal.aborted && target === owner();
    const options = () => ({ signal: view.signal });
    const message = element("p", { attrs: { role: "status" } }), content = element("div"); view.body.append(content, message);
    try {
      if (item) item = (await api.get(project ? `/mcp/project/${encodeURIComponent(item.id)}?project=${encodeURIComponent(project)}` : `/extensions/mcp/${encodeURIComponent(item.id)}`, options())).data;
      const binding = item ? JSON.parse(item.content).mdo_template : null;
      const templates = (await api.get("/mcp/templates", options())).data.items.filter(row => row.valid);
      if (!live()) return;
      if (binding && !templates.some(row => row.receipt.key === binding.receipt.key)) templates.push({ receipt: binding.receipt, template: binding.template });
      if (template && receipt && !templates.some(row => row.receipt.key === receipt.key)) templates.push({ receipt, template });
      if (!templates.length) { message.textContent = "请先从 MCP 商店安装一个服务模板。安装模板不会启动服务。"; view.footer.append(button("关闭", () => view.close())); return; }
      const form = element("form", { className: "store-publish-form" }); form.id = `${view.element.id}-connection`;
      const row = (label, input, hint = "") => { form.append(element("label", { className: "extension-field" }, [element("span", { text: label }), input, hint ? element("small", { text: hint }) : null])); return input; };
      const identity = row("连接 ID", element("input", { attrs: { required: "", maxlength: "64", value: item?.id || "", placeholder: "personal-service" } })); identity.disabled = Boolean(item);
      const name = row("连接名称", element("input", { attrs: { required: "", maxlength: "256", value: item?.name || receipt?.name || "MCP 连接" } }));
      const scope = row("所属范围", element("select")); scope.append(element("option", { text: "个人 MCP", attrs: { value: "" } }));
      const projects = (await api.get("/projects", options())).data.items || []; if (!live()) return;
      if (!projects.some(p => p.id === "default")) projects.unshift({ id: "default", name: "默认项目" });
      for (const p of projects.filter(p => p.status !== "trash")) scope.append(element("option", { text: p.name || p.id, attrs: { value: p.id } })); scope.value = project; scope.disabled = Boolean(item);
      const selection = row("服务模板与版本", element("select"));
      templates.forEach((r, index) => selection.append(element("option", { text: `${r.receipt.name} · ${r.receipt.version} · ${r.receipt.submitter || r.receipt.author || "本机导入"}`, attrs: { value: String(index) } })));
      selection.value = String(Math.max(0, templates.findIndex(r => r.receipt.key === (binding?.receipt.key || receipt?.key))));
      const variant = row("连接方案", element("select")), fields = element("div"), summary = element("p", { className: "extension-help" });
      form.append(summary, fields);
      const allow = row("允许的工具（可选，每行一个远端工具名）", element("textarea")), deny = row("排除的工具（每行一个）", element("textarea"));
      const oldConfig = item ? JSON.parse(item.content) : null; allow.value = (oldConfig?.tools?.allow || []).join("\n"); deny.value = (oldConfig?.tools?.deny || []).join("\n");
      const enabled = element("input", { attrs: { type: "checkbox" } }); enabled.checked = false;
      row("保存后启用", enabled, "首次安装默认停用。启用后由实际调用建立连接；也可先保存，再测试与授权。");
      if (item) enabled.checked = item.enabled;
      const trust = element("input", { attrs: { type: "checkbox" } });
      const trustRow = element("label", { className: "store-code-consent" }, [trust, element("span", { text: "我已检查完整启动命令并信任本机服务代码。程序可使用当前设备权限；首次运行可能下载依赖。" })]); form.append(trustRow);
      const preview = element("pre", { className: "store-source", attrs: { "aria-label": "连接配置预览" } }); form.append(preview);
      let controls = new Map(), approved = "", selectedTemplate, selectedVariant;
      function invalidate() { approved = ""; trust.checked = false; preview.textContent = "修改后请重新预览；凭据不显示在预览中。"; }
      function renderFields() {
        selectedTemplate = templates[Number(selection.value)]; selectedVariant = selectedTemplate.template.variants.find(v => v.id === variant.value);
        controls = new Map(); fields.replaceChildren(); invalidate();
        const meta = mcpTemplateSummary({ variants: [selectedVariant] }); summary.textContent = `${meta.transports.join(" / ")} · ${meta.authentication.join(" / ")} · ${selectedVariant.requirements || "授权由服务所属设备完成"}`;
        trustRow.hidden = selectedVariant.config.transport.type !== "stdio";
        for (const input of mcpVariantInputs(selectedTemplate.template, selectedVariant)) {
          const control = element(input.type === "select" || input.type === "boolean" ? "select" : "input", { attrs: { "aria-label": input.label, type: input.secret ? "password" : input.type === "number" ? "number" : "text", autocomplete: "off", maxlength: "4096" } });
          if (control.tagName === "SELECT") for (const option of input.type === "boolean" ? ["false", "true"] : input.options || []) control.append(element("option", { text: option, attrs: { value: option } }));
          if (!input.secret) control.value = binding?.values?.[input.id] ?? input.default ?? "";
          if (input.secret && binding?.values?.[input.id]) control.placeholder = "已保存，留空保留；更换模板或目标需重填";
          if (input.min != null) control.min = input.min; if (input.max != null) control.max = input.max;
          controls.set(input.id, control); fields.append(element("label", { className: "extension-field" }, [element("span", { text: `${input.label}${input.required ? " · 必填" : ""}${input.secret ? " · 私密" : ""}` }), control, element("small", { text: input.description || "" })]));
        }
      }
      function variants() { const vs=templates[Number(selection.value)].template.variants;variant.replaceChildren(...vs.map(v => element("option", { text: v.label, attrs: { value: v.id } })));variant.value=vs[0].id; if (binding && templates[Number(selection.value)].receipt.key === binding.receipt.key) variant.value = binding.variant; renderFields(); }
      selection.addEventListener("change", variants); variant.addEventListener("change", renderFields); variants();
      form.addEventListener("input", event => { dirty = true; if (event.target !== trust) invalidate(); });
      function body(action) { return { action, id: identity.value.trim(), name: name.value.trim(), variant: variant.value, receipt_key: selectedTemplate.receipt.key, ...(selectedTemplate.receipt.id > 0 ? { receipt_id: selectedTemplate.receipt.id } : {}), values: Object.fromEntries([...controls].map(([key, c]) => [key, c.value])), enabled: enabled.checked, confirm_launch: trust.checked, tools: { allow: allow.value.split(/\r?\n/).map(s => s.trim()).filter(Boolean), deny: deny.value.split(/\r?\n/).map(s => s.trim()).filter(Boolean) } }; }
      async function perform(action) {
        if (!live() || saving) return;
        const payload = body(action), fingerprint = JSON.stringify({ ...payload, action: "save", confirm_launch: false });
        if (action === "save" && approved !== fingerprint) { message.textContent = "请先预览当前配置，再保存。"; return; }
        saving = true; message.textContent = action === "save" ? "正在保存…" : "正在检查配置…";
        try {
          const result = (await api.post(`/mcp/connections${scope.value ? `?project=${encodeURIComponent(scope.value)}` : ""}`, payload, { ...options(), ifMatch: `"${item?.revision || "new"}"` })).data;
          if (!live()) return;
          if (action === "preview") { approved = fingerprint; preview.textContent = JSON.stringify(result.config, null, 2); message.textContent = result.target_changed ? "服务目标或授权配置发生变化，需要重新填写凭据并授权。" : "请核对地址、启动参数和工具范围。"; }
          else { dirty = false; saving = false; close(true); onChange(); toast("连接已保存；尚未启动服务", "success"); }
        } catch (e) { if (live() && e.name !== "AbortError") message.textContent = `${errorMessage(e)}${action === "save" ? "；请重新读取连接状态后再重试。" : ""}`; }
        finally { saving = false; }
      }
      form.addEventListener("submit", e => { e.preventDefault(); void perform("save"); }); content.append(form);
      view.footer.append(button("预览配置", () => perform("preview")), button("保存连接", () => perform("save"), true), button("取消", () => view.close()));
    } catch (e) { if (live() && e.name !== "AbortError") message.textContent = errorMessage(e); }
  }
  const stop = subscribeTarget(() => close(true));
  return { open, hasPendingChanges: () => Boolean(layer), destroy() { close(true); stop(); } };
}
