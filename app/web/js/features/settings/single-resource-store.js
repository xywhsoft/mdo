import { createExtensionBrowser, detailTabs, detailFacts, browserText } from "../../components/extension-browser.js";
import { t } from "../../i18n.js";
import { api } from "../../api/client.js";
import { targetState, subscribeTarget } from "../../api/target.js";
import { element, errorMessage, toast } from "../../utils/dom.js";
import { makeStorePackage, STORE_PLATFORMS, packageFields, resourceReferences } from "./store-formats.js";
import { renderSourceMarkdown, renderPackageAttachment } from "../../utils/source-markdown.js";
import { customizableResource, resourceCredits, storeReadOnlyMessage } from "./resource-credits.js";
import { openLayer } from "../../components/layers.js";

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
export function createSingleResourceStore(kind, onChange, onBrowse = () => {}, onManage = null) {
  if (!["skills", "tools", "commands"].includes(kind)) throw Error("Unsupported resource store");
  const name = ({ skills: "Skill", tools: "Agent 工具", commands: "编辑器命令" })[kind];
  const root = node("div", "", "single-resource-store"), statusRoot = element("p", { className: "extension-help", attrs: { role: "status" } });
  let visible = false, disposed = false, epoch = 0, controller = null, activeTarget = device();
  let items = [], installed = {}, cursor = 0, query = "", catalogQuery = "", busy = false, error = "", status = "";
  let dialog = null, dirty = false, saving = false, loginRequired = false;
  let backgroundWrite = null, detailSerial = 0, detailController = null, stateFilter = "all";
  const browser = createExtensionBrowser({ kind, onClose: () => { ++detailSerial; detailController?.abort(); } });
  const call = async (body, signal = controller?.signal, download) => (await api.post("/ecosystem", { ...body, single_kind: kind }, { signal, download })).data;
  const current = (id, target) => !disposed && epoch === id && device() === target;
  function close(force = false) {
    if (!dialog) return true;
    if (!force && (saving || (dirty && !confirm("放弃未提交的作品信息？")))) return false;
    dialog.dispose({ restoreFocus: true }); return true;
  }
  function modal(title) {
    if (!close()) return null;
    const view = openLayer({ title, icon: kind === "skills" ? "spark" : "terminal", width: "48rem", height: "80%",
      beforeClose: () => !saving && (!dirty || confirm("放弃未提交的作品信息？")),
      onClosed() { if (dialog === view) { dialog = null; dirty = saving = false; } } });
    dialog = view; view.element.classList.add("extension-authoring"); return view;
  }
  function report(e) {
    loginRequired = e.status === 401;
    error = loginRequired ? "请先在账户页登录，再发布或管理作品。" : errorMessage(e);
  }
  async function load(append = false) {
    if (busy || disposed) return;
    const id = ++epoch, target = device(), requestQuery = append ? catalogQuery : query; controller?.abort(); controller = new AbortController();
    busy = true; error = ""; render();
    try {
      const [data, local] = await Promise.all([call({ action: "catalog", query: requestQuery, before: append ? cursor : 0 }), api.get("/ecosystem", { signal: controller.signal })]);
      if (!current(id, target)) return;
      items = append ? [...new Map([...items, ...data.items].map(row => [row.id, row])).values()] : data.items; installed = local.data; cursor = data.next_before || 0; catalogQuery = requestQuery;
    } catch (e) { if (current(id, target) && e.name !== "AbortError") report(e); }
    finally { if (current(id, target)) { busy = false; render(); } }
  }
  async function operate(label, work, background = false) {
    if (busy || disposed) return;
    const id = ++epoch, target = device(); controller?.abort(); controller = new AbortController();
    if(background)backgroundWrite=id;busy = true; error = ""; status = `${label}…`; render();
    try {
      await work(); if (!current(id, target)) return;
      status = `${label}完成`; onChange(); busy = false; await load();
    } catch (e) { if (current(id, target) && e.name !== "AbortError") { report(e); status = `${label}未完成，请刷新核对状态后重试。`; } }
    finally { if(backgroundWrite===id)backgroundWrite=null;if (current(id, target)) { busy = false; render(); } }
  }
  const owned = row => Object.values(installed).find(r => r.source !== "local" && r.owner === row.owner && r.slug === row.slug);
  function render() {
    browser.rememberFocus();
    const search = element("form", { className: "store-search eb-toolbar" });
    const input = element("input", { attrs: { type: "search", maxlength: "100", placeholder: `搜索${name}、作者或用途`, "aria-label": `搜索${name}` } }); input.value = query;
    input.addEventListener("input", () => { query = input.value; });
    const filter = element("select", { attrs: { "aria-label": browserText("status", "状态") } });
    for (const [value, text] of [["all", browserText("all", "全部状态")], ["installed", browserText("installed", "已安装")], ["updates", browserText("updates", "可更新")], ["new", browserText("notInstalled", "未安装")]]) filter.append(element("option", { text, attrs: { value } }));
    filter.value = stateFilter; filter.addEventListener("change", () => { stateFilter = filter.value; render(); });
    search.append(input, element("button", { text: "搜索", className: "secondary-button", attrs: { type: "submit" } }), filter, button("刷新", () => load()));
    search.addEventListener("submit", e => { e.preventDefault(); query = input.value.trim(); void load(); });
    const rows = items.filter(row => stateFilter === "all" || (stateFilter === "installed" ? Boolean(owned(row)) : stateFilter === "updates" ? owned(row) && owned(row).id !== row.id : !owned(row))).map(row => {
      const receipt = owned(row), same = receipt?.id === row.id;
      const primary = button(same ? browserText("manage", "管理") : receipt ? browserText("update", "更新") : browserText("install", "安装"), () => same && onManage ? onManage(receipt) : detail(row), !same);
      primary.disabled = busy;
      const credits=resourceCredits({...row,store_managed:true});
      return { key: String(row.id), kind, title: row.name, byline:credits.submitter, description: row.description, meta:credits.meta, badge: { text: same ? browserText("installed", "已安装") : receipt ? browserText("updates", "可更新") : "" }, primary, onSelect: () => loadDetail(row) };
    });
    browser.setItems(rows, { busy, pageKey:JSON.stringify([catalogQuery,stateFilter]), onNext: cursor ? () => { void load(true); } : null, empty: items.length ? browserText("empty", "没有匹配的扩展") : `暂无公开${name}。可从本机列表分享你的作品。` });
    root.replaceChildren(search, node("p", kind === "skills" ? "按需读取 Skill，包含说明与参考文件。" : kind === "commands" ? "输入 / 选择命令并传递参数；C 代码在调用时编译运行，安装前请检查源码并确认信任。" : "C 工具在本机执行；安装前请检查源码并确认信任。", "extension-help"), browser.root);
    statusRoot.textContent = error || (busy ? status || "正在载入…" : status); statusRoot.hidden = !statusRoot.textContent; statusRoot.className = error ? "extension-error" : "extension-help";
    if (loginRequired) root.append(button("登录账户", () => { location.hash = "#/settings/account"; }));
    search.querySelectorAll("button").forEach(b => { b.disabled = busy; });
  }
  function detail(row) {
    onBrowse();
    if (!items.some(r => r.id === row.id)) items = [row, ...items];
    stateFilter = "all"; render(); browser.open(String(row.id));
  }
  async function loadDetail(row) {
    const id = ++detailSerial, target = device(), key = String(row.id);
    detailController?.abort(); detailController = new AbortController();
    const signal = detailController.signal;
    const valid = () => !disposed && device() === target && id === detailSerial && browser.isSelected(key);
    const read = body => call(body, signal);
    try {
      const data = await read({ action: "detail", id: row.id }); if (!valid()) return;
      const p = data.package;
      if (!singleResource(p, kind)) throw Error(`作品必须只包含一个${name}`);
      const credits=resourceCredits({...data,store_managed:true},{...data,manifest:p.manifest});
      const description = node("div"); description.append(node("p", data.description || row.description),credits.facts(),renderSourceMarkdown(p.manifest.readme));
      if (p.manifest.changelog) description.append(node("h4", "更新说明"), renderSourceMarkdown(p.manifest.changelog));
      const usage = node("div"); usage.append(detailFacts([["作者", credits.author], ["版本", data.version], ["许可", credits.license], ["支持平台", p.manifest.platforms.join(" / ")]]));
      usage.append(node("p", kind === "skills" ? `$${p.resources[0].id} · /skill:${p.resources[0].id}` : kind === "commands" ? `/${p.resources[0].id} [参数] · 支持引号分词或 JSON 数组，参数通过 xvalue array 传入 C 命令。` : "安装后在 Agent 工具页管理；模型通过结构化参数调用。"));
      const source = node("div"); source.append(node("pre", p.resources[0].content, "store-source"));
      for (const file of p.resources[0].files || []) source.append(renderPackageAttachment(file, { download: "下载文件", binary: "二进制文件，请下载检查", truncated: "仅显示前 64 KiB，请下载完整文件" }));
      const history = node("div"); let historyLoaded = false, historyBusy = false, before = 0;
      const moreHistory = async () => {
        if (historyBusy || !valid()) return; historyBusy = true;
        const message = node("p", "正在读取版本…"); history.append(message);
        try {
          const h = await read({ action: "history", id: row.id, before }); if (!valid()) return;
          for (const v of h.items) history.append(node("p", `${v.version} · ${t(`store.state.${v.state}`, {}, v.state)} · ${v.changelog || ""}`));
          before = h.next_before || 0; historyLoaded = true;
          if (before) { const more = button("更多版本", () => { more.remove(); void moreHistory(); }); history.append(more); }
          if (!h.items.length) history.append(node("p", "暂无历史版本"));
        } catch (e) { if (valid() && e.name !== "AbortError") { history.append(node("p", errorMessage(e), "extension-error")); const retry = button("重试", () => { retry.remove(); void moreHistory(); }); history.append(retry); } }
        finally { message.remove(); historyBusy = false; }
      };
      const tabs = detailTabs([{ id: "description", label: browserText("description", "说明"), content: description }, { id: "usage", label: browserText("usage", "使用方式"), content: usage }, { id: "versions", label: browserText("versions", "版本"), content: history, onSelect: () => { if (!historyLoaded) void moreHistory(); } }, { id: "source", label: browserText("sourceFiles", "源码与文件"), content: source }]);
      const d = node("div"); d.append(tabs); browser.showDetail(key, d);
      if (data.state !== "published") return;
      const plan = await read({ action: "plan", id: row.id }); if (!valid()) return;
      const preview = node("details"); preview.append(node("summary", "安装计划"));
      for (const f of plan.files) preview.append(node("p", `${f.operation} · ${f.path} · ${f.bytes} B`)); d.append(preview);
      let trusted = kind === "skills";
      const same = owned(row)?.id === row.id;
      const install = button(same ? "已安装" : owned(row) ? "更新到此版本" : "下载安装", () => {
        if (!valid() || !trusted || same || busy) return;
        browser.closeDetail(); void operate("下载安装", () => call({ action: "install", id: row.id, trust_code: trusted, expected_plan: plan.sha256 }, null, { name: row.name }), true);
      }, true); install.disabled = same || !trusted;
      if (kind !== "skills") {
        const check = element("input", { attrs: { type: "checkbox" } });
        check.addEventListener("change", () => { trusted = check.checked; install.disabled = same || !trusted; });
        d.append(element("label", { className: "store-code-consent" }, [check, node("span", kind === "commands" ? "我已检查源码并信任作者。C 命令在调用时由独立工作进程编译运行，可通过 Application SDK 操作 mdo 和本机资源。" : "我已检查源码并信任作者。C 工具在 mdo 进程内运行，编译注册时也可能执行代码。") ]));
      }
      d.append(install);
    } catch (e) { if (valid() && e.name !== "AbortError") { const message = node("div"); message.append(node("p", errorMessage(e), "extension-error"), button("重试", () => loadDetail(row))); browser.showDetail(key, message); } }
  }
  async function publish(item, receipt = null) {
    if (busy || disposed) return;
    if(!customizableResource(item,receipt)){toast(storeReadOnlyMessage(),"error");return;}
    const id = epoch, target = device(); controller?.abort(); controller = new AbortController(); busy = true;
    try {
      const exported = await call({ action: "export", references: [{ kind, id: item.id, ...(item.project ? { project: item.project } : {}) }] });
      if (!current(id, target)) return;
      if (!singleResource(exported, kind)) throw Error("导出结果必须只有一个资源");
      const previous = singleResource(receipt || {}, kind) ? packageFields(receipt.manifest) : {};
      if (previous.version?.match(/^\d+\.\d+\.\d+$/)) previous.version = previous.version.replace(/\d+$/, n => String(Number(n) + 1));
      const d = modal(`分享${name} · ${item.name || item.id}`); if (!d) return;
      const form = element("form", { className: "store-publish-form" }), fields = {};
      for (const [key, text, limit, area] of [["name", "作品名称", 100], ["slug", "作品 ID（小写英文，发布后不变）", 64], ["version", "版本（如 1.0.0）", 32], ["description", "简介", 600, true], ["author", "作品作者（可选，与提交者账号不同）", 100], ["copyright", "版权声明（可选，保留原作者归属）", 4096, true], ["readme", "使用说明", 32768, true], ["license", "许可证", 80], ["changelog", "更新说明", 8192, true], ["review_notes", "审核说明：用法、构建与权限（申请公布必填）", 8000, true], ["source_url", "源码或说明链接（HTTPS，可选）", 1000]]) {
        const control = element(area ? "textarea" : "input", { attrs: { name: key, maxlength: String(limit), required: ["name", "slug", "version", "description", "readme", "license"].includes(key) ? "" : null } });
        control.value = previous[key] || ({ name: item.name || item.id, slug: `${kind === "skills" ? "skill" : kind === "commands" ? "command" : "tool"}-${item.id}`.slice(0, 64), version: "1.0.0", description: item.description || "", license: "MIT" })[key] || "";
        fields[key] = control; form.append(element("label", {}, [node("span", text), control]));
      }
      const platforms = element("fieldset"), selected = []; platforms.append(node("legend", "支持平台（仅选择已验证的平台）"));
      for (const value of STORE_PLATFORMS) { const check = element("input", { attrs: { type: "checkbox", value } }); check.checked = previous.platforms?.includes(value) || false; selected.push(check); platforms.append(element("label", { className: "store-choice" }, [check, node("span", value)])); }
      form.append(platforms, node("p", `仅提交当前${name}及其参考文件。源文件随作品提供审核；请先检查其中的私密信息。配额与管理在创作中心。`, "store-note"));
      const publicly = element("input", { attrs: { type: "checkbox" } }); publicly.checked = true;
      form.append(element("label", { className: "store-choice" }, [publicly, node("span", "申请公布（审核后可供其他用户下载；取消则保存为私有作品）")]));
      form.id = `${d.element.id}-form`;
      const submit = element("button", { text: "提交作品", className: "primary-button", attrs: { type: "submit", form: form.id } });
      const cancel = button("取消", () => { void d.close("cancel"); });
      const message = element("p", { attrs: { role: "status" } }); form.append(message);
      d.footer.append(submit, cancel);
      form.addEventListener("input", () => { dirty = true; });
      form.addEventListener("submit", async e => {
        e.preventDefault(); if (saving || dialog !== d || d.signal.aborted || !current(id, target)) return;
        try {
          const values = Object.fromEntries(Object.entries(fields).map(([k, v]) => [k, v.value.trim()])); values.platforms = selected.filter(c => c.checked).map(c => c.value);
          if (!/^\d+\.\d+\.\d+$/.test(values.version)) throw Error("请使用 1.0.0 格式的版本号");
          if (publicly.checked && values.review_notes.length < 10) throw Error("申请公布时请填写至少 10 个字符的审核说明");
          if (values.source_url && !/^https:\/\//i.test(values.source_url)) throw Error("源码链接须为 HTTPS");
          const p = makeStorePackage(values, exported.resources);
          saving = true; submit.disabled = cancel.disabled = d.closeButton.disabled = true;
          message.textContent = "正在上传与提交…"; for (const control of form.elements) control.disabled = true;
          const result = await call({ action: "submit", package: p, publish: publicly.checked, review_notes: values.review_notes, source_url: values.source_url });
          if (!current(id, target) || dialog !== d) return;
          dirty = false; saving = false; status = result.state === "pending" ? "作品已提交审核，审核通过后公开。" : "作品已保存为私有，可在创作中心管理。";
          close(true); toast(status, "success"); render();
        } catch (e) { if (current(id, target) && dialog === d) message.textContent = errorMessage(e); }
        finally { if (dialog === d) { saving = false; submit.disabled = cancel.disabled = d.closeButton.disabled = false; for (const control of form.elements) control.disabled = false; } }
      });
      d.body.append(form);
    } catch (e) { if (current(id, target)) toast(errorMessage(e), "error"); }
    finally { if (current(id, target)) busy = false; }
  }
  function localActions(receipt) {
    if (!receipt || !singleResource(receipt, kind)) return [];
    const controls = [];
    if (receipt.source !== "local") controls.push(button("检查更新", async () => {
      if (busy) return; const id = epoch, target = device(); controller?.abort(); controller = new AbortController(); busy = true;
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
  const unsubscribe = subscribeTarget(() => { if (activeTarget === device()) return; activeTarget = device(); ++epoch; controller?.abort(); browser.reset(); close(true); items = []; installed = {}; cursor = 0; busy = false; error = status = ""; if (visible) void load(); });
  render();
  return { root, statusRoot, publish, localActions, activate(value) { visible = value; if (!value) browser.closeDetail(false); if (value && !busy) void load(); }, hasPendingChanges: () => Boolean(dialog) || dirty || saving || (busy && backgroundWrite === null),
    destroy() { disposed = true; ++epoch; controller?.abort(); browser.destroy(); unsubscribe(); close(true); } };
}
