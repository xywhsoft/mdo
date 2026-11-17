import { createLocalModelDeployments } from "./local-model-deployments.js";
import { api } from "../../api/client.js";
import { subscribeTarget } from "../../api/target.js";
import { t, currentLocale, subscribeLocale } from "../../i18n.js";
import { element, errorMessage } from "../../utils/dom.js";
import { openLayer } from "../../components/layers.js";
const tr = key => t(`localModels.${key}`, {}, key);
export const localModelLabel = (value, locale = currentLocale()) => typeof value === "string" ? value : value?.[locale] ?? value?.[locale.split("-")[0]] ?? value?.["en-US"] ?? Object.values(value || {})[0] ?? "";
const bytes = value => { if (!value) return "0 B"; const unit = Math.min(4, Math.floor(Math.log(value) / Math.log(1024))); return `${(value / 1024 ** unit).toFixed(unit ? 1 : 0)} ${["B","KiB","MiB","GiB","TiB"][unit]}`; };
export function createLocalModelPanel(root) {
  let active = false, tab = "local", snapshot, catalog, busy = false, generation = 0, timer, reload = false, offset = 0, query = "", controller = new AbortController();
  const layers = new Set();
  const pageSize = 50;
  const note = element("p", { attrs: { role: "status" } }), toolbar = element("div", { className: "extension-toolbar" }), content = element("div", { className: "local-model-list" });
  const button = (key, run) => { const b = element("button", { className: "secondary-button", text: tr(key), attrs: { type: "button" } }); b.addEventListener("click", () => void run()); return b; };
  const deployments = createLocalModelDeployments({ tr, bytes, button, layer, field, toggle, call, load, snapshot: () => snapshot, current: () => { const epoch = generation; return () => epoch === generation; } });
  const local = button("local", () => select("local")), store = button("store", () => select("store")), refresh = button("refresh", () => load()), importing = button("import", () => importFile()), storage = button("storage", () => storageForm());
  const hardware = button("hardware", hardwareInfo); const cache = button("cache", cacheInfo); toolbar.append(local, store, refresh, importing, storage, hardware, cache);
  const searchForm = element("form", { className: "extension-toolbar" }), search = element("input", { attrs: { type: "search", "aria-label": tr("search") } });
  const submitSearch = button("search", () => { query = search.value.trim(); offset = 0; void load(); });
  const previous = button("previous", () => { offset = Math.max(0, offset - pageSize); void load(); }), next = button("next", () => { offset += pageSize; void load(); });
  searchForm.append(search, submitSearch, previous, next); searchForm.addEventListener("submit", e => { e.preventDefault(); submitSearch.click(); });
  root.append(toolbar, searchForm, note, content);
  function poll() { clearTimeout(timer); if (active && tab === "local" && Object.values(snapshot?.installations || {}).some(row => ["queued","installing","removing"].includes(row.state)) || active && tab === "local" && Object.values(snapshot?.deployments || {}).some(row => ["starting","loading","ready","stopping"].includes(row.service_state))) timer = setTimeout(() => load(true), 1500); }
  async function call(body) { return (await api.post("/local-models", body, { signal: controller.signal })).data; }
  async function load(quiet = false) {
    if (!active) return; if (busy) { reload = true; return; } busy = true; const epoch = generation, ownTab = tab; if (!quiet) note.textContent = tr("loading");
    try { const data = tab === "local" ? (await api.get("/local-models", { signal: controller.signal })).data : await call({ action: "catalog", limit: pageSize, offset, q: query });
      if (epoch !== generation || ownTab !== tab) return; const changed = JSON.stringify(tab === "local" ? snapshot : catalog) !== JSON.stringify(data); if (tab === "local") snapshot = data; else catalog = data;
      note.textContent = snapshot?.read_only ? tr("readOnly") : ""; if (!quiet || changed) render();
    } catch (e) { if (epoch === generation) note.textContent = errorMessage(e); }
    finally { if (epoch === generation) { busy = false; if (reload) { reload = false; void load(); } else poll(); } }
  }
  function select(next) { tab = next; render(); void load(); }
  function render() {
    content.replaceChildren(); searchForm.hidden = tab !== "store";
    previous.disabled = !offset; next.disabled = offset + (catalog?.items?.length || 0) >= (catalog?.total || 0);
    importing.disabled = storage.disabled = !!snapshot?.read_only;
    local.setAttribute("aria-pressed", String(tab === "local")); store.setAttribute("aria-pressed", String(tab === "store"));
    const rows = tab === "local" ? Object.values(snapshot?.installations || {}) : catalog?.items || [];
    if (!rows.length) content.append(element("p", { text: tr(tab === "local" ? "emptyLocal" : "emptyStore") }));
    for (const row of rows) {
      const card = element("article", { className: "local-model-card" }), actions = element("div", { className: "extension-toolbar" });
      const model = row.local_model, localized = model?.i18n?.[currentLocale()] || model?.i18n?.[model.default_locale] || model?.i18n?.["en-US"];
      card.append(element("h3", { text: localized?.name || row.name || row.id }));
      if (tab === "store") {
        card.append(element("p", { text: localized?.description || row.description || "" }));
        for (const tag of model?.tags || []) card.append(element("span", { className: "tool-member-badge", text: localModelLabel(tag.label) }));
        card.append(element("p", { text: (model?.weights || []).map(w => `${w.quantization}: ${bytes(w.bytes ?? (w.files || []).reduce((sum, f) => sum + f.bytes, 0))}`).join(" · ") }));
        actions.append(button("details", () => details(row)));
      } else {
        card.append(element("p", { text: tr(`state.${row.state}`) }), element("p", { text: `${(row.assets || []).length} ${tr("files")} · ${bytes((row.assets || []).reduce((sum, f) => sum + f.bytes, 0))}` }));
        card.append(element("p", { text: `${tr("storage")}: ${snapshot?.roots?.[row.store]?.path || ""}` }));
        if (row.kind === "reference") card.append(element("p", { text: `${tr("path")}: ${row.source_path}` }));
        if (row.error) card.append(element("p", { className: "extension-error", text: row.error }));
        if (row.state === "installed") { actions.append(button("createDeployment", () => deployments.edit(row.id))); if (row.recipe_ref) actions.append(button("checkUpdate", () => checkUpdate(row))); }
        for (const deployment of Object.values(snapshot?.deployments || {}).filter(d => d.installation === row.id)) { const node = element("section", { className:"local-model-deployment" }); deployments.actions(node, deployment); card.append(node); }
        if (row.kind !== "download") card.append(element("p", { text: tr(row.kind === "reference" ? "referenceNotice" : "importedNotice") }));
        if (["failed","interrupted","attention"].includes(row.state)) actions.append(button("retry", async () => { await operate({ action: "retry", id: row.id }); }));
        if (["queued","installing"].includes(row.state)) actions.append(button("cancel", async () => { await operate({ action: "cancel", id: row.id }); }));
        else actions.append(button("uninstall", () => removeForm(row)));
      }
      card.append(actions); content.append(card);
    }
  }
  async function checkUpdate(row) { const epoch = generation; try { const data = await call({ action:"updates", id:row.id }); if (epoch !== generation) return; const latest = data.items?.find(item => item.id === row.recipe_ref); if (latest && latest.version_id !== row.recipe_version_id) await details(latest); else note.textContent = tr(latest ? "upToDate" : "updateUnavailable"); } catch(e) { if (epoch === generation) note.textContent = errorMessage(e); } }
  async function operate(body) { try { await call(body); await load(); } catch (e) { note.textContent = errorMessage(e); } }
  function layer(title, onClosed) {
    const view = openLayer({ owner: root, title, width: "48rem", height: "80%", onClosed() { layers.delete(view); onClosed?.(); } }); layers.add(view); return view;
  }
  function field(form, key, value = "") { const label = element("label", { className: "extension-field" }), input = element("input", { attrs: { name: key, type: "text" } }); input.value = value; label.append(element("span", { text: tr(key) }), input); form.append(label); return input; }
  function toggle(form, key, checked) { const input = element("input", { attrs: { type: "checkbox", role: "switch" } }); input.checked = checked; const label = element("label", { className: "extension-settings-clear-switch" }, [input, element("span", { text: tr(key) })]); form.append(label); return input; }
  function mutationForm(title, setup, request) {
    const view = layer(tr(title)), form = element("form", { className: "extension-settings-form" }), error = element("p", { className: "extension-error", attrs: { role: "status" } });
    const fields = setup(form); view.body.append(form, error); let pending = false; view.update({ beforeClose: () => !pending });
    const save = button("confirm", async () => {
      if (pending) return; const epoch = generation; pending = true; save.disabled = true; form.inert = true;
      try { const body = await request(fields); if (body) await call(body); if (epoch !== generation) return; view.dispose(); await load(); }
      catch (e) { if (epoch === generation) error.textContent = errorMessage(e); }
      finally { if (epoch === generation) { pending = false; save.disabled = false; form.inert = false; } }
    });
    form.addEventListener("submit", e => { e.preventDefault(); save.click(); }); view.footer.append(button("cancel", () => { if (!pending) view.close(); }), save);
  }
  function cacheInfo() { mutationForm("cache", form => { form.append(element("p", { text:tr("cacheNotice") })); const label = element("label", { className:"extension-field" }), select = element("select"); for (const r of Object.values(snapshot?.roots || {})) select.append(element("option", { text:r.path, attrs:{value:r.id} })); select.value = snapshot?.default_root || ""; label.append(element("span", { text:tr("storage") }), select); form.append(label); return select; }, input => ({ action:"prune_cache", id:input.value })); }
  async function hardwareInfo() { const epoch = generation, view = layer(tr("hardware")), text = element("pre", { className:"local-model-requirements", text:tr("loading") }); view.body.append(text); try { const h = await call({action:"hardware"}); if (epoch !== generation || !layers.has(view)) return; text.textContent = `${h.platform} · ${h.os_version || ""}
${tr("availableRam")}: ${bytes(h.available_ram_bytes)} / ${bytes(h.ram_bytes)}
${(h.cpu_features || []).join(" · ")}
${(h.gpus || []).map(g => `${g.name} · ${tr("gpuCapacity")}: ${bytes(g.capacity_bytes)}`).join("\n")}
${tr("unknownVram")}`; } catch(e) { if (epoch === generation) text.textContent = errorMessage(e); } }
  function storageForm() { mutationForm("storage", form => { form.append(element("p", { text: tr("storageNotice") })); return field(form, "path", snapshot?.roots?.[snapshot.default_root]?.path || ""); }, input => ({ action: "storage", path: input.value.trim() })); }
  function importFile() { mutationForm("import", form => { form.append(element("p", { text: tr("importNotice") })); return { path: field(form, "path"), name: field(form, "name"), copy: toggle(form, "copy", true) }; }, f => ({ action: "import", path: f.path.value.trim(), name: f.name.value.trim(), mode: f.copy.checked ? "copy" : "reference" })); }
  function removeForm(row) { if (Object.values(snapshot?.deployments || {}).some(d => d.installation === row.id)) { const view = layer(tr("uninstall")); view.body.append(element("p", { text:tr("removeDeploymentsFirst") })); return; } mutationForm("uninstall", form => { form.append(element("p", { text: row.name }), element("p", { text: tr("removeNotice") })); return toggle(form, "deleteFiles", true); }, input => ({ action: "uninstall", id: row.id, delete_files: input.checked })); }
  async function details(row) {
    let plan, accepted = false, pending = false;
    const epoch = generation;
    const labels = row.local_model?.i18n, label = labels?.[currentLocale()] || labels?.[row.local_model?.default_locale] || labels?.["en-US"];
    const view = layer(label?.name || row.name || row.id, () => { if (plan && !accepted && epoch === generation) void call({ action: "discard_plan", plan_id: plan.id, revision: plan.revision }).catch(() => {}); }), status = element("p", { attrs: { role: "status" }, text: tr("loading") }); view.update({ beforeClose: () => !pending }); view.body.append(status);
    try {
      const data = await call({ action: "detail", version: row.version_id }); if (epoch !== generation || !layers.has(view)) return;
      const doc = data.local_model; if (!doc) throw Error(tr("unavailable"));
      const label = doc.i18n?.[currentLocale()] || doc.i18n?.[doc.default_locale] || doc.i18n?.["en-US"];
      status.textContent = label?.description || data.description || "";
      const select = element("select", { attrs: { "aria-label": tr("profile") } }), requirements = element("pre", { className: "local-model-requirements" });
      const platform = snapshot?.platform || (await api.get("/local-models", { signal: controller.signal })).data.platform;
      if (epoch !== generation || !layers.has(view)) return;
      const profiles = (doc.profiles || []).filter(p => doc.runtimes.some(r => r.id === p.runtime && r.platform === platform));
      if (!profiles.length) throw Error(tr("unavailable"));
      for (const profile of profiles) select.append(element("option", { text: localModelLabel(profile.label), attrs: { value: profile.id } }));
      function show() { const p = doc.profiles.find(p => p.id === select.value), runtime = doc.runtimes.find(r => r.id === p.runtime), weight = doc.weights.find(w => w.id === p.weight);
        requirements.textContent = `${weight.quantization} · ${runtime.engine} ${runtime.build} · ${runtime.backend}\n${tr("ram")}: ${bytes(p.requirements.ram_bytes)} · ${tr("vram")}: ${bytes(p.requirements.vram_bytes)}\n${tr("disk")}: ${bytes(p.requirements.disk_bytes)}\n${tr("context")}: ${p.context_tokens} · ${tr("output")}: ${p.output_tokens}\n${localModelLabel(p.requirements.notes)}`;
      } select.addEventListener("change", show); show(); view.body.append(select, requirements);
      const planInfo = element("pre", { className: "local-model-requirements" }); view.body.append(planInfo);
      const approval = element("div", { className: "extension-settings-form" }), trust = toggle(approval, "trustRuntime", false); approval.hidden = true; view.body.append(approval);
      const install = button("install", async () => {
        if (!plan || !trust.checked || !plan.runtime_plan.ready || pending) return; pending = true; install.disabled = true;
        try { await call({ action: "install", plan_id: plan.id, revision: plan.revision, trust_code: true }); if (epoch !== generation) return; accepted = true; view.dispose(); tab = "local"; render(); await load(); }
        catch (e) { if (epoch === generation) { status.textContent = errorMessage(e); install.disabled = !trust.checked; } } finally { pending = false; }
      }); install.disabled = true; trust.addEventListener("change", () => { install.disabled = !trust.checked || !plan?.runtime_plan?.ready; });
      const inspect = button("inspect", async () => {
        inspect.disabled = true; pending = true; try { plan = await call({ action: "plan", version: data.version_id, profile: select.value }); if (epoch !== generation || !layers.has(view)) return;
          select.disabled = true; approval.hidden = false;
          planInfo.textContent = [...(plan.model.runtime_plan.items || []).map(item => `${item.id}
${item.version_id}
SHA-256: ${item.sha256}`),
            ...(plan.model.assets || []).map(file => `${file.path} · ${bytes(file.bytes)}
SHA-256: ${file.sha256}
${file.urls.join("\n")}`)].join("\n\n"); status.textContent = plan.runtime_plan.ready ? tr("inspectNotice") : tr("unavailable");
        } catch (e) { if (epoch === generation) status.textContent = errorMessage(e); } finally { pending = false; if (epoch === generation) inspect.disabled = !!plan; }
      }); view.footer.append(inspect, install);
    } catch (e) { if (epoch === generation && layers.has(view)) status.textContent = errorMessage(e); }
  }
  const unsubscribeTarget = subscribeTarget(() => { generation++; controller.abort(); controller = new AbortController(); busy = reload = false; offset = 0; snapshot = catalog = null; for (const view of [...layers]) view.dispose(); render(); if (active) void load(); });
  const unsubscribeLocale = subscribeLocale(() => { for (const [node,key] of [[local,"local"],[store,"store"],[refresh,"refresh"],[importing,"import"],[storage,"storage"],[hardware,"hardware"],[cache,"cache"],[submitSearch,"search"],[previous,"previous"],[next,"next"]]) node.textContent = tr(key); render(); });
  return { hasPendingChanges: () => deployments.hasPendingChanges(), selectSection(section) { active = section === "local-models"; clearTimeout(timer); if (active) void load(); }, destroy() { generation++; controller.abort(); clearTimeout(timer); unsubscribeTarget(); unsubscribeLocale(); for (const view of [...layers]) view.dispose(); } };
}
