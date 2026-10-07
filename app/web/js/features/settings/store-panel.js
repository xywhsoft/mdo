import { api } from "../../api/client.js";
import { targetState, subscribeTarget } from "../../api/target.js";
import { subscribeLocale, t } from "../../i18n.js";
import { element, errorMessage, toast } from "../../utils/dom.js";
import { loadAgents } from "../../state/catalogs.js";
import { loadResource } from "../../state/resources.js";
import { makeStorePackage, STORE_KINDS, STORE_PLATFORMS, containsCode } from "./store-formats.js";

const text = (key, fallback, args = {}) => t(`store.${key}`, args, fallback);
const kindName = kind => ({ agents: "Agent", subagents: "SubAgent", tools: text("tools", "工具"),
  skills: "Skill", mcp: "MCP", commands: text("commands", "命令"), "c-agents": "C Agent", "c-subagents": "C SubAgent" })[kind] || kind;
const node = (tag, value, className) => element(tag, { text: value, className });

export function createStorePanel() {
  const root = document.querySelector("[data-extension-store]");
  let visible = false, view = "discover", items = [], installed = {}, query = "", kind = "", cursor = 0;
  let busy = false, error = "", loginRequired = false, serial = 0, dialog = null, editor = null;
  const targetKey = () => targetState().selected?.id || "local";
  const call = async body => (await api.post("/ecosystem", body)).data;
  const button = (label, action, primary = false) => {
    const b = element("button", { text: label, className: primary ? "primary-button" : "secondary-button", attrs: { type: "button" } });
    b.addEventListener("click", () => { void action(); }); return b;
  };
  const feedback = e => {
    error = ["ecosystem_login_required", "ecosystem_login_refresh"].includes(e.code)
      ? text("loginRequired", "登录 ai.xywhsoft.com 账号后，即可浏览、安装和发布插件。") : errorMessage(e);
    loginRequired = e.status === 401;
  };
  const changed = () => {
    for (const kind of STORE_KINDS) window.dispatchEvent(new CustomEvent("mdo-extensions-changed", { detail: { kind } }));
    void loadAgents();
    for (const name of ["modules", "skills", "mcp"]) void loadResource(name);
  };
  function closeDialog() { dialog?.close(); dialog?.remove(); dialog = null; editor = null; }
  function createDialog(title) {
    closeDialog(); dialog = element("dialog", { className: "store-dialog", attrs: { "aria-label": title } });
    const heading = node("div", "", "store-dialog-heading");heading.append(node("h2", title), button(text("close", "关闭"), () => { if (!busy) closeDialog(); }));
    dialog.append(heading);document.body.append(dialog);
    dialog.addEventListener("cancel", e => { if (busy) e.preventDefault(); });
    dialog.addEventListener("close", () => { if (editor?.dirty && !editor.saved) toast(text("draftClosed", "未发布的内容未上传。")); });
    return dialog;
  }
  async function load(append = false) {
    if (busy) return;
    const epoch = ++serial, target = targetKey();busy = true;error = "";render();
    try {
      const records = (await api.get("/ecosystem")).data;
      const data = view === "installed" ? null : await call({ action: view === "mine" ? "mine" : "catalog", query, kind, before: append ? cursor : 0 });
      if (epoch !== serial || target !== targetKey()) return;
      installed = records;items = view === "installed" ? Object.values(records) : append ? items.concat(data.items) : data.items;
      cursor = data?.next_before || 0;loginRequired = false;
    } catch (e) { if (epoch === serial) { items = []; feedback(e); } }
    finally { if (epoch === serial) { busy = false;render(); } }
  }
  async function act(work) {
    if (busy) return;const target = targetKey();busy = true;error = "";
    try { await work();if (target !== targetKey()) return;changed();busy = false;await load(); }
    catch (e) { if (target === targetKey()) { feedback(e);toast(error, "error"); } }
    finally { if (target === targetKey()) { busy = false;render(); } }
  }
  function render() {
    if (!root) return;
    const heading = node("div", "", "store-heading");
    heading.append(node("div", "", "store-intro"), button(text("publish", "发布插件"), publish, true));
    heading.firstChild.append(node("p", text("subtitle", "分享你的工作方式，让墨斗更顺手。")));
    const tabs = node("div", "", "store-tabs");tabs.setAttribute("role", "tablist");
    for (const [id, label] of [["discover", text("discover", "发现")], ["installed", text("installed", "已安装")], ["mine", text("mine", "我的发布")]]) {
      const b = button(label, () => { view = id;query = "";kind = "";void load(); });b.setAttribute("role", "tab");b.setAttribute("aria-selected", String(view === id));tabs.append(b);
    }
    const nodes = [heading, tabs];
    if (view === "discover") {
      const form = element("form", { className: "store-search" });
      const input = element("input", { attrs: { type: "search", placeholder: text("search", "搜索插件、作者或用途"), "aria-label": text("search", "搜索插件、作者或用途"), maxlength: "100" } });input.value = query;
      const select = element("select", { attrs: { "aria-label": text("type", "资源类型") } });
      select.append(element("option", { text: text("all", "全部类型"), attrs: { value: "" } }));
      for (const k of STORE_KINDS) select.append(element("option", { text: kindName(k), attrs: { value: k } }));select.value = kind;
      const submit = element("button", { text: text("searchButton", "搜索"), className: "secondary-button", attrs: { type: "submit" } });submit.disabled = busy;
      form.append(input, select, submit);form.onsubmit = e => { e.preventDefault();query = input.value.trim();kind = select.value;void load(); };nodes.push(form);
    }
    if (error || busy) { const status = node("p", busy ? text("loading", "正在读取…") : error, "store-status");status.setAttribute("role", "status");nodes.push(status); }
    if (loginRequired) nodes.push(button(text("signIn", "登录账号"), () => { location.hash = "#/settings/account"; }));
    else if (!items.length && !busy && !error) nodes.push(node("div", text(view === "mine" ? "noPublished" : view === "installed" ? "noInstalled" : "noResults", view === "mine" ? "还没有投稿。选择本地资源，发布你的第一个插件。" : view === "installed" ? "安装的插件会显示在这里。" : "暂无公开插件，欢迎发布你的作品。"), "store-empty"));
    const cards = node("div", "", "store-grid");
    for (const row of items) {
      const card = node("article", "", "store-card"), icon = node("div", [...(row.name || "M")][0], "store-card-icon");
      card.append(icon, node("h3", row.name), node("p", row.description || text("installedHint", "资源保存在本机，可在对应管理页编辑。"), "store-card-description"), node("div", `${row.author} · ${row.version}`, "store-card-author"));
      const actions = node("div", "", "store-card-actions");
      if (view === "installed") {
        actions.append(button(text("checkUpdate", "检查更新"), async () => {
          try { const latest = await call({ action: "latest", id: row.id });
            if (!latest || latest.id === row.id) toast(text("upToDate", "已是最新公开版本。"));else await detail(latest); }
          catch (e) { toast(errorMessage(e), "error"); }
        }), button(text("uninstall", "卸载"), () => {
          if (confirm(text("uninstallConfirm", "卸载此插件？安装后修改过的文件将保留为本地自定义资源。"))) void act(() => call({ action: "uninstall", entry: { id: row.id } }));
        }));
      } else {
        actions.append(button(text("details", "查看详情"), () => detail(row)));
        if (view === "mine") {
          const label = text(`state.${row.state}`, { pending: "待审核", published: "已公开", rejected: "已退回", hidden: "已下架" }[row.state]);
          card.append(node("span", label, `store-badge store-${row.state}`));if (row.reason) card.append(node("p", row.reason, "store-status"));
          actions.append(button(text("newVersion", "发布新版本"), () => publish(row)));
        } else if (installed[row.id]) card.append(node("span", text("installed", "已安装"), "store-badge"));
      }
      card.append(actions);cards.append(card);
    }
    nodes.push(cards);if (cursor) nodes.push(button(text("more", "加载更多"), () => load(true)));
    nodes.push(button(text("refresh", "刷新"), () => load()));root.replaceChildren(...nodes);
    for (const b of root.querySelectorAll("button")) b.disabled = busy;
  }
  async function detail(row) {
    if (busy) return;busy = true;
    try {
      const data = await call({ action: "detail", id: row.id }), p = data.package;
      const d = createDialog(data.name);d.append(node("p", `${data.author} · ${data.version} · ${p.manifest.license}`, "store-detail-meta"), node("p", data.description));
      d.append(node("pre", p.manifest.readme, "store-readme"));
      const files = node("div", "", "store-resource-list");files.append(node("h3", text("included", "包含的资源")));
      for (const r of p.resources) { const source = node("details");source.append(node("summary", `${kindName(r.kind)} / ${r.id}`), node("pre", r.content, "store-source"));for (const f of r.files || []) source.append(node("p", f.path));files.append(source); }d.append(files);
      d.append(node("p", p.manifest.platforms.join(" · "), "store-detail-meta"));
      if (p.resources.some(r => r.kind === "mcp")) d.append(node("p", text("mcpSetup", "MCP 安装后保持停用。请在 MCP 管理页填写本机凭据、检查程序或地址，再启用并测试连接。"), "store-note"));
      const hasCode = containsCode(p.resources), already = Boolean(installed[row.id]);let approved = !hasCode;
      if (data.state === "published") {
        const install = button(already ? text("installed", "已安装") : text("install", "安装到当前设备"), () => {
          if (!approved || already) return;closeDialog();void act(() => call({ action: "install", id: row.id, trust_code: approved }));
        }, true);install.disabled = already || !approved;
        if (hasCode) { const label = node("label", "", "store-code-consent"), checkbox = element("input", { attrs: { type: "checkbox" } });
          label.append(checkbox, node("span", text("codeConsent", "我已检查源码并信任作者。C 扩展在墨斗进程内运行，编译注册时也能执行代码。")));checkbox.onchange = () => { approved = checkbox.checked;install.disabled = !approved || already; };d.append(label); }
        d.append(install);
      }
      busy = false;d.showModal();
    } catch (e) { feedback(e);toast(error, "error"); }
    finally { busy = false; }
  }
  async function publish(previous = null) {
    if (busy) return;busy = true;
    try {
      // Native store access also checks login, before exposing publish tools.
      await api.get("/ecosystem");
      let selectedResources = [];
      if (previous?.id) {
        const old = await call({ action: "detail", id: previous.id });
        selectedResources = old.package.resources;previous = { ...previous, ...old.package.manifest };
        const parts = /^(\d+)\.(\d+)\.(\d+)$/.exec(previous.version);
        if (parts) previous.version = `${parts[1]}.${parts[2]}.${Number(parts[3]) + 1}`;
      }
      const lists = await Promise.all(STORE_KINDS.map(async k => [k, (await api.get(`/extensions/${k}`)).data.items]));
      const cSources = await call({ action: "c_sources" });
      for (const k of ["c-agents", "c-subagents"]) lists.push([k, cSources.items.filter(r => r.kind === k)]);
      const d = createDialog(text("publish", "发布插件")), form = element("form", { className: "store-publish-form" });
      editor = { dirty: false, saved: false };form.oninput = () => { if (editor) editor.dirty = true; };
      const fields = {};
      for (const [key, label, limit, multiline] of [["name", text("name", "插件名称"), 100], ["slug", text("slug", "插件 ID（小写英文）"), 64], ["version", text("version", "版本"), 32], ["description", text("description", "简短描述"), 600, true], ["readme", text("readme", "使用说明"), 32768, true], ["license", text("license", "许可证"), 80]]) {
        const l = node("label", label), input = element(multiline ? "textarea" : "input", { attrs: { name: key, maxlength: String(limit), required: "", autocomplete: "off" } });
        input.value = previous?.[key] || (key === "version" ? "1.0.0" : key === "license" ? "MIT" : "");fields[key] = input;l.append(input);form.append(l);
      }
      const platformBox = element("fieldset");platformBox.append(node("legend", text("platforms", "支持的平台（按实际验证选择）")));
      const platforms = [];
      for (const p of STORE_PLATFORMS) { const label = node("label", "", "store-choice"), b = element("input", { attrs: { type: "checkbox", value: p } });b.checked = previous?.platforms?.includes(p) || false;platforms.push(b);label.append(b, node("span", p));platformBox.append(label); }form.append(platformBox);
      const choices = element("fieldset");choices.append(node("legend", text("selectResources", "选择要分享的资源（最多 16 项）")));const refs = [];
      for (const [k, list] of lists) for (const r of list) {
        if (k === "agents" && r.id === "default") continue;
        const label = node("label", "", "store-choice"), b = element("input", { attrs: { type: "checkbox" } });
        b.checked = selectedResources.some(old => old.kind === k && old.id === r.id);
        refs.push({ b, kind: k, id: r.id });label.append(b, node("span", `${kindName(k)} · ${r.name || r.id}`));choices.append(label);
      }
      form.append(choices, node("p", text("publishHint", "只上传选中的资源。MCP 凭据和本机工作目录会自动移除；请检查源码、说明和其他资源中是否包含私密信息。投稿经管理员审核后公开。"), "store-note"));
      const preview = node("pre", "", "store-source");preview.hidden = true;
      const message = node("p", "", "store-status");message.setAttribute("role", "status");
      const actions = node("div", "", "store-card-actions");let prepared = null;
      const build = async () => {
        const resources = (await call({ action: "export", references: refs.filter(r => r.b.checked).map(({ kind, id }) => ({ kind, id })) })).resources;
        return makeStorePackage(Object.fromEntries([...Object.entries(fields).map(([k, v]) => [k, v.value]), ["platforms", platforms.filter(b => b.checked).map(b => b.value)]]), resources);
      };
      const previewButton = button(text("preview", "预览发布内容"), async () => {
        previewButton.disabled = true;message.textContent = "";
        try { if (!form.reportValidity()) return;prepared = await build();preview.textContent = JSON.stringify(prepared, null, 2);preview.hidden = false; }
        catch (e) { message.textContent = errorMessage(e); }finally { previewButton.disabled = false; }
      });
      const exportButton = button(text("export", "导出插件包"), async () => {
        exportButton.disabled = true;
        try { if (!form.reportValidity()) return;const p = await build();const url = URL.createObjectURL(new Blob([JSON.stringify(p, null, 2)], { type: "application/json" }));
          const a = element("a", { attrs: { href: url, download: `${p.manifest.slug}-${p.manifest.version}.mdo-extension.json` } });a.click();setTimeout(() => URL.revokeObjectURL(url), 1000);
        } catch (e) { message.textContent = errorMessage(e); }finally { exportButton.disabled = false; }
      });
      const submit = element("button", { text: text("submit", "提交审核"), className: "primary-button", attrs: { type: "submit" } });
      form.onsubmit = async e => {
        e.preventDefault();if (busy) return;busy = true;submit.disabled = true;previewButton.disabled = true;message.textContent = text("submitting", "正在提交…");
        const controls = [...form.querySelectorAll("input,textarea,button")];controls.forEach(control => { control.disabled = true; });
        try { prepared = await build();await call({ action: "submit", package: prepared });editor.saved = true;editor.dirty = false;closeDialog();view = "mine";busy = false;await load();toast(text("submitted", "投稿已提交，审核通过后会公开显示。")); }
        catch (e) { message.textContent = errorMessage(e); }
        finally { busy = false;controls.forEach(control => { control.disabled = false; }); }
      };
      actions.append(previewButton, exportButton, submit);form.append(actions, message, preview);d.append(form);busy = false;d.showModal();
    } catch (e) { feedback(e);render(); }
    finally { busy = false; }
  }
  const unsubLocale = subscribeLocale(() => { if (!dialog?.open) render(); });
  const unsubTarget = subscribeTarget(() => { ++serial;items = [];installed = {};closeDialog();if (visible) void load(); });
  return {
    selectSection(section) { visible = section === "store";if (visible) void load(); },
    hasPendingChanges: () => busy || Boolean(editor?.dirty && dialog?.open),
    destroy() { ++serial;closeDialog();unsubLocale();unsubTarget(); },
  };
}
