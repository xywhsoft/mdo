import { api } from "../../api/client.js";
import { targetState, subscribeTarget } from "../../api/target.js";
import { element, errorMessage, toast } from "../../utils/dom.js";
import { makeStorePackage, STORE_PLATFORMS, packageFields, resourceReferences } from "./store-formats.js";
import { renderSourceMarkdown, renderPackageAttachment } from "../../utils/source-markdown.js";

const node = (tag, text, className) => element(tag, { text, className });
const device = () => targetState().selected?.id || "local";
const button = (text, action, primary = false) => {
  const b = element("button", { text, className: primary ? "primary-button" : "secondary-button", attrs: { type: "button" } });
  b.addEventListener("click", () => { void action(); }); return b;
};
export const singleResource = (row, kind) => {
  const refs = resourceReferences(row);
  return refs.length === 1 && refs[0].kind === kind;
};

// Each instance is bound to one resource type and one selected device. There
// is deliberately no resource multi-select in this publication flow.
export function createSingleResourceStore(kind, onChange) {
  if (!["skills", "tools"].includes(kind)) throw Error("Unsupported resource store");
  const name = kind === "skills" ? "Skill" : "Agent 工具";
  const root = node("div", "", "single-resource-store"), statusRoot = element("p", { className: "extension-help", attrs: { role: "status" } });
  let visible = false, disposed = false, epoch = 0, controller = null;
  let items = [], installed = {}, cursor = 0, query = "", busy = false, error = "", status = "";
  let dialog = null, dirty = false, saving = false, loginRequired = false;
  const call = async (body, signal = controller?.signal) => (await api.post("/ecosystem", { ...body, single_kind: kind }, { signal })).data;
  const current = (id, target) => !disposed && epoch === id && device() === target;
  function close(force = false) {
    if (!dialog) return true;
    if (!force && (saving || (dirty && !confirm("放弃未提交的作品信息？")))) return false;
    dialog.close(); dialog.remove(); dialog = null; dirty = saving = false; return true;
  }
  function modal(title) {
    if (!close()) return null;
    dialog = element("dialog", { className: "store-dialog", attrs: { "aria-label": title } });
    dialog.append(element("div", { className: "store-dialog-heading" }, [node("h2", title), button("关闭", () => close())]));
    dialog.addEventListener("cancel", e => { e.preventDefault(); close(); });
    document.body.append(dialog); dialog.showModal(); return dialog;
  }
  function link(text, path) {
    return element("a", { text, className: "secondary-button", attrs: { href: `https://ai.xywhsoft.com${path}`, target: "_blank", rel: "noopener noreferrer" } });
  }
  function report(e) {
    loginRequired = e.status === 401;
    error = loginRequired ? "请先在账户页登录，再发布或管理作品。" : errorMessage(e);
  }
  async function load(append = false) {
    if (busy || disposed) return;
    const id = ++epoch, target = device(); controller?.abort(); controller = new AbortController();
    busy = true; error = ""; render();
    try {
      const [data, local] = await Promise.all([call({ action: "catalog", query, before: append ? cursor : 0 }), api.get("/ecosystem", { signal: controller.signal })]);
      if (!current(id, target)) return;
      items = append ? [...items, ...data.items] : data.items; installed = local.data; cursor = data.next_before || 0;
    } catch (e) { if (current(id, target) && e.name !== "AbortError") report(e); }
    finally { if (current(id, target)) { busy = false; render(); } }
  }
  async function operate(label, work) {
    if (busy || disposed) return;
    const id = ++epoch, target = device(); controller?.abort(); controller = new AbortController();
    busy = true; error = ""; status = `${label}…`; render();
    try {
      await work(); if (!current(id, target)) return;
      status = `${label}完成`; onChange(); busy = false; await load();
    } catch (e) { if (current(id, target) && e.name !== "AbortError") { report(e); status = `${label}未完成，请刷新核对状态后重试。`; } }
    finally { if (current(id, target)) { busy = false; render(); } }
  }
  const owned = row => Object.values(installed).find(r => r.source !== "local" && r.owner === row.owner && r.slug === row.slug);
  function render() {
    const search = element("form", { className: "store-search" });
    const input = element("input", { attrs: { type: "search", maxlength: "100", placeholder: `搜索${name}、作者或用途`, "aria-label": `搜索${name}` } }); input.value = query;
    search.append(input, element("button", { text: "搜索", className: "secondary-button", attrs: { type: "submit" } }), button("刷新", () => load()), link("创作中心", `/mdo/creator?kind=${kind}`));
    search.addEventListener("submit", e => { e.preventDefault(); query = input.value.trim(); void load(); });
    const rows = node("div", "", "extension-list");
    for (const row of items) {
      const receipt = owned(row), same = receipt?.id === row.id;
      const actions = node("div", "", "resource-actions");
      actions.append(button(same ? "查看已安装版本" : receipt ? "查看更新" : "详情与安装", () => detail(row)));
      if (same) actions.append(node("span", "已安装", "store-badge"));
      rows.append(element("article", { className: "resource-card" }, [node("h3", row.name), node("p", row.description), node("small", `${row.author} · ${row.version}`), actions]));
    }
    if (!items.length && !busy && !error) rows.append(node("p", `暂无公开${name}。可从本机列表分享你的作品。`, "empty-state"));
    root.replaceChildren(search, node("p", `每个作品独立安装和更新；${kind === "skills" ? "包含 SKILL.md 及其参考文件。" : "安装前可检查 C 源码，确认信任后才会编译加载。"}`, "extension-help"), rows);
    if (cursor) root.append(button("加载更多", () => load(true)));
    statusRoot.textContent = error || (busy ? status || "正在载入…" : status); statusRoot.hidden = !statusRoot.textContent; statusRoot.className = error ? "extension-error" : "extension-help";
    if (loginRequired) root.append(button("登录账户", () => { location.hash = "#/settings/account"; }));
    root.querySelectorAll("button").forEach(b => { b.disabled = busy; });
  }
  async function detail(row) {
    if (busy) return;
    const target = device(), id = epoch; busy = true; render();
    try {
      const data = await call({ action: "detail", id: row.id });
      if (!current(id, target)) return;
      const p = data.package;
      if (!singleResource(p, kind)) throw Error(`作品必须只包含一个${name}`);
      const d = modal(data.name); if (!d) return;
      d.append(node("p", `${data.author} · ${data.version} · ${p.manifest.license} · ${p.manifest.platforms.join(" / ")}`));
      d.append(renderSourceMarkdown(p.manifest.readme));
      if (p.manifest.changelog) d.append(node("h3", "更新说明"), renderSourceMarkdown(p.manifest.changelog));
      const source = node("details"); source.append(node("summary", "查看完整源码与参考文件"), node("pre", p.resources[0].content, "store-source"));
      for (const file of p.resources[0].files || []) source.append(renderPackageAttachment(file, { download: "下载文件", binary: "二进制文件，请下载检查", truncated: "仅显示前 64 KiB，请下载完整文件" }));
      d.append(source);
      const versions = node("details"), history = node("div"); let historyLoaded = false, historyBusy = false, before = 0;
      versions.append(node("summary", "版本历史"), history); d.append(versions);
      const moreHistory = async () => {
        if (historyBusy) return; historyBusy = true;
        try { const h = await call({ action: "history", id: row.id, before }); if (!current(id, target) || dialog !== d) return;
          for (const v of h.items) history.append(node("p", `${v.version} · ${v.state} · ${v.changelog || ""}`));
          before = h.next_before || 0; historyLoaded = true;
          if (before) history.append(button("更多版本", async () => { history.lastChild.remove(); await moreHistory(); }));
        } catch (e) { if (dialog === d) history.append(node("p", errorMessage(e), "extension-error")); }
        finally { historyBusy = false; }
      };
      versions.addEventListener("toggle", () => { if (versions.open && !historyLoaded) void moreHistory(); });
      if (data.state !== "published") return;
      const plan = await call({ action: "plan", id: row.id });
      if (!current(id, target) || dialog !== d) return;
      const preview = node("details"); preview.open = true; preview.append(node("summary", "安装计划"));
      for (const f of plan.files) preview.append(node("p", `${f.operation} · ${f.path} · ${f.bytes} B`)); d.append(preview);
      let trusted = kind === "skills";
      const same = owned(row)?.id === row.id;
      const install = button(same ? "已安装" : owned(row) ? "更新到此版本" : "下载安装", () => {
        if (!trusted || same) return;
        close(true); void operate("下载安装", () => call({ action: "install", id: row.id, trust_code: trusted, expected_plan: plan.sha256 }));
      }, true); install.disabled = same || !trusted;
      if (kind === "tools") {
        const check = element("input", { attrs: { type: "checkbox" } });
        check.addEventListener("change", () => { trusted = check.checked; install.disabled = same || !trusted; });
        d.append(element("label", { className: "store-code-consent" }, [check, node("span", "我已检查源码并信任作者。C 工具在 mdo 进程内运行，编译注册时也可能执行代码。") ]));
      }
      d.append(install);
    } catch (e) { if (current(id, target)) { report(e); if (dialog) dialog.append(node("p", error, "extension-error")); } }
    finally { if (current(id, target)) { busy = false; render(); } }
  }
  async function publish(item, receipt = null) {
    if (busy || disposed) return;
    const id = epoch, target = device(); busy = true;
    try {
      const exported = await call({ action: "export", references: [{ kind, id: item.id, ...(item.project ? { project: item.project } : {}) }] });
      if (!current(id, target)) return;
      if (!singleResource(exported, kind)) throw Error("导出结果必须只有一个资源");
      const previous = singleResource(receipt || {}, kind) ? packageFields(receipt.manifest) : {};
      if (previous.version?.match(/^\d+\.\d+\.\d+$/)) previous.version = previous.version.replace(/\d+$/, n => String(Number(n) + 1));
      const d = modal(`分享${name} · ${item.name || item.id}`); if (!d) return;
      const form = element("form", { className: "store-publish-form" }), fields = {};
      for (const [key, text, limit, area] of [["name", "作品名称", 100], ["slug", "作品 ID（小写英文，发布后不变）", 64], ["version", "版本（如 1.0.0）", 32], ["description", "简介", 600, true], ["readme", "使用说明", 32768, true], ["license", "许可证", 80], ["changelog", "更新说明", 8192, true], ["review_notes", "审核说明：用法、构建与权限（申请公布必填）", 8000, true], ["source_url", "源码或说明链接（HTTPS，可选）", 1000]]) {
        const control = element(area ? "textarea" : "input", { attrs: { name: key, maxlength: String(limit), required: ["name", "slug", "version", "description", "readme", "license"].includes(key) ? "" : null } });
        control.value = previous[key] || ({ name: item.name || item.id, slug: `${kind === "skills" ? "skill" : "tool"}-${item.id}`.slice(0, 64), version: "1.0.0", description: item.description || "", license: "MIT" })[key] || "";
        fields[key] = control; form.append(element("label", {}, [node("span", text), control]));
      }
      const platforms = element("fieldset"), selected = []; platforms.append(node("legend", "支持平台（仅选择已验证的平台）"));
      for (const value of STORE_PLATFORMS) { const check = element("input", { attrs: { type: "checkbox", value } }); check.checked = previous.platforms?.includes(value) || false; selected.push(check); platforms.append(element("label", { className: "store-choice" }, [check, node("span", value)])); }
      form.append(platforms, node("p", `仅提交当前${name}及其参考文件。源文件随作品提供审核；请先检查其中的私密信息。配额与管理在创作中心。`, "store-note"));
      const publicly = element("input", { attrs: { type: "checkbox" } }); publicly.checked = true;
      form.append(element("label", { className: "store-choice" }, [publicly, node("span", "申请公布（审核后可供其他用户下载；取消则保存为私有作品）")]));
      const message = element("p", { attrs: { role: "status" } }); form.append(message, element("button", { text: "提交作品", className: "primary-button", attrs: { type: "submit" } }));
      form.addEventListener("input", () => { dirty = true; });
      form.addEventListener("submit", async e => {
        e.preventDefault(); if (saving || !current(id, target)) return;
        try {
          const values = Object.fromEntries(Object.entries(fields).map(([k, v]) => [k, v.value.trim()])); values.platforms = selected.filter(c => c.checked).map(c => c.value);
          if (!/^\d+\.\d+\.\d+$/.test(values.version)) throw Error("请使用 1.0.0 格式的版本号");
          if (publicly.checked && values.review_notes.length < 10) throw Error("申请公布时请填写至少 10 个字符的审核说明");
          if (values.source_url && !/^https:\/\//i.test(values.source_url)) throw Error("源码链接须为 HTTPS");
          const p = makeStorePackage(values, exported.resources);
          saving = true; message.textContent = "正在上传与提交…"; for (const control of form.elements) control.disabled = true;
          const result = await call({ action: "submit", package: p, publish: publicly.checked, review_notes: values.review_notes, source_url: values.source_url });
          if (!current(id, target) || dialog !== d) return;
          dirty = false; saving = false; status = result.state === "pending" ? "作品已提交审核，审核通过后公开。" : "作品已保存为私有，可在创作中心管理。";
          close(true); toast(status, "success"); render();
        } catch (e) { if (current(id, target) && dialog === d) message.textContent = errorMessage(e); }
        finally { if (dialog === d) { saving = false; for (const control of form.elements) control.disabled = false; } }
      });
      d.append(form, link("管理作品、审核与配额", `/mdo/creator?kind=${kind}`));
    } catch (e) { if (current(id, target)) toast(errorMessage(e), "error"); }
    finally { if (current(id, target)) busy = false; }
  }
  function localActions(receipt) {
    if (!receipt || !singleResource(receipt, kind)) return [];
    const controls = [];
    if (receipt.source !== "local") controls.push(button("检查更新", async () => {
      if (busy) return; const id = epoch, target = device(); busy = true;
      try { const latest = await call({ action: "latest", id: receipt.id }); if (!current(id, target)) return; busy = false;
        if (latest.id === receipt.id) toast("已是最新公开版本"); else await detail(latest);
      } catch (e) { if (current(id, target)) toast(errorMessage(e), "error"); }
      finally { if (current(id, target)) busy = false; }
    }));
    controls.push(button("卸载商店资源", () => {
      if (!confirm("卸载此资源？本机修改过的文件会保留为自定义资源。")) return;
      void operate("卸载", () => call({ action: "uninstall", entry: { id: receipt.id, source: receipt.source || "store", slug: receipt.slug } }));
    })); return controls;
  }
  const unsubscribe = subscribeTarget(() => { ++epoch; controller?.abort(); close(true); items = []; installed = {}; cursor = 0; busy = false; error = status = ""; if (visible) void load(); });
  render();
  return { root, statusRoot, publish, localActions, activate(value) { visible = value; if (value && !busy) void load(); }, hasPendingChanges: () => Boolean(dialog) || dirty || saving || busy,
    destroy() { disposed = true; ++epoch; controller?.abort(); unsubscribe(); close(true); } };
}
