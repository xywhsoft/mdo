import { createLocalModelDeployments } from "./local-model-deployments.js";
import { api } from "../../api/client.js";
import { subscribeTarget } from "../../api/target.js";
import { t, currentLocale, subscribeLocale } from "../../i18n.js";
import { element, errorMessage } from "../../utils/dom.js";
import { openLayer } from "../../components/layers.js";
import { actionMenu, browserText, createExtensionBrowser, detailFacts } from "../../components/extension-browser.js";
const tr = key => t(`localModels.${key}`, {}, key);
export const localModelLabel = (value, locale = currentLocale()) => typeof value === "string" ? value : value?.[locale] ?? value?.[locale.split("-")[0]] ?? value?.["en-US"] ?? Object.values(value || {})[0] ?? "";
const bytes = value => { if (!value) return "0 B"; const unit = Math.min(4, Math.floor(Math.log(value) / Math.log(1024))); return `${(value / 1024 ** unit).toFixed(unit ? 1 : 0)} ${["B","KiB","MiB","GiB","TiB"][unit]}`; };
const localizedModel = model => model?.i18n?.[currentLocale()] || model?.i18n?.[model.default_locale] || model?.i18n?.["en-US"];

export function createLocalModelPanel(root) {
  let active = false, tab = "local", snapshot, catalog, generation = 0, timer, query = "", reader;
  let controller = new AbortController();
  const layers = new Set();
  const localBrowser = createExtensionBrowser({ kind: "local-models" });
  const storeBrowser = createExtensionBrowser({ kind: "local-models" });
  const note = element("p", { className: "extension-help", attrs: { role: "status", hidden: "" } });
  const button = (key, run) => { const b = element("button", { className: "secondary-button", text: tr(key), attrs: { type: "button" } }); b.addEventListener("click", () => void run()); return b; };
  const deployments = createLocalModelDeployments({ tr, bytes, button, layer, field, toggle, call, load, snapshot: () => snapshot, current: () => { const epoch = generation; return () => epoch === generation; } });
  const tabs = element("div", { className: "store-tabs", attrs: { role: "tablist" } });
  const local = button("local", () => select("local")), store = button("store", () => select("store"));
  for (const control of [local, store]) {
    control.setAttribute("role", "tab");
    control.addEventListener("keydown", event => {
      if (!["ArrowLeft", "ArrowRight", "Home", "End"].includes(event.key)) return;
      event.preventDefault();
      const next = event.key === "Home" ? local : event.key === "End" ? store : control === local ? store : local;
      next.click();
      next.focus();
    });
    tabs.append(control);
  }

  const toolbar = element("div", { className: "eb-toolbar" });
  const filter = element("input", { attrs: { type: "search" } });
  const refresh = button("refresh", () => load()), importing = button("import", importFile);
  const storage = button("storage", storageForm), hardware = button("hardware", hardwareInfo), cache = button("cache", cacheInfo);
  const resourceMenu = actionMenu([importing, hardware, storage, cache]);
  toolbar.append(filter, refresh, resourceMenu);
  filter.addEventListener("input", render);

  const searchForm = element("form", { className: "store-search eb-toolbar" });
  const search = element("input", { attrs: { type: "search", maxlength: "240" } });
  const submitSearch = element("button", { className: "secondary-button", attrs: { type: "submit" } });
  const refreshStore = button("refresh", () => load());
  searchForm.append(search, submitSearch, refreshStore);
  searchForm.addEventListener("submit", event => {
    event.preventDefault();
    const next = search.value.trim();
    if (next !== query) { query = next; catalog = null; }
    void load();
  });

  const localPanel = element("div", { attrs: { role: "tabpanel" } }, [toolbar, localBrowser.root]);
  const storePanel = element("div", { className: "single-resource-store", attrs: { role: "tabpanel" } }, [searchForm, storeBrowser.root]);
  for (const [control, panel, suffix] of [[local, localPanel, "local"], [store, storePanel, "store"]]) {
    control.id = `local-models-tab-${suffix}`;
    panel.id = `local-models-panel-${suffix}`;
    panel.setAttribute("aria-labelledby", control.id);
    control.setAttribute("aria-controls", panel.id);
  }
  root.append(tabs, note, localPanel, storePanel);

  function report(message, error = false) {
    note.classList.toggle("extension-error", error);
    note.textContent = message;
    note.hidden = !message;
  }
  function poll() {
    clearTimeout(timer);
    if (!active || tab !== "local") return;
    const installing = Object.values(snapshot?.installations || {}).some(row => ["queued", "installing", "removing"].includes(row.state));
    const running = Object.values(snapshot?.deployments || {}).some(row => ["starting", "loading", "ready", "stopping"].includes(row.service_state));
    if (installing || running) timer = setTimeout(() => load({ quiet: true }), 1500);
  }
  async function call(body, signal = controller.signal) { return (await api.post("/local-models", body, { signal })).data; }
  async function load({ quiet = false, append = false } = {}) {
    if (!active || (quiet && reader)) return;
    clearTimeout(timer);
    reader?.abort();
    const read = reader = new AbortController();
    const isLocal = tab === "local", offset = append ? catalog?.items.length || 0 : 0;
    let changed = false;
    if (!quiet) { report(""); render(); }
    try {
      if (isLocal) {
        const { data } = await api.get("/local-models", { signal: read.signal });
        if (reader !== read) return;
        changed = JSON.stringify(snapshot) !== JSON.stringify(data);
        snapshot = data;
      } else {
        const data = await call({ action: "catalog", limit: 50, offset, q: query }, read.signal);
        if (reader !== read) return;
        catalog = { ...data, items: append ? [...catalog.items, ...data.items] : data.items };
      }
      if (!quiet || changed) report(isLocal && snapshot?.read_only ? tr("readOnly") : "");
    } catch (e) { if (reader === read) report(errorMessage(e), true); }
    finally { if (reader === read) { reader = null; if (!quiet || changed) render(); poll(); } }
  }
  function select(next) {
    if (next === tab) return;
    resourceMenu.open = false;
    tab = next;
    void load();
  }
  function installationRows() {
    const grouped = new Map();
    for (const deployment of Object.values(snapshot?.deployments || {})) {
      if (!grouped.has(deployment.installation)) grouped.set(deployment.installation, []);
      grouped.get(deployment.installation).push(deployment);
    }
    return Object.values(snapshot?.installations || {}).flatMap(row => {
      const model = row.local_model, label = localizedModel(model);
      const title = label?.name || row.name || row.id;
      const assets = row.assets || [], size = `${assets.length} ${tr("files")} · ${bytes(assets.reduce((sum, file) => sum + file.bytes, 0))}`;
      const status = tr(`state.${row.state}`), problem = ["failed", "interrupted", "attention"].includes(row.state);
      const description = row.error || label?.description || (row.kind === "download" ? "" : tr(row.kind === "reference" ? "referenceNotice" : "importedNotice"));
      const tags = (model?.tags || []).map(tag => localModelLabel(tag.label));
      const info = [[browserText("status", "状态"), status], [tr("files"), size], [tr("storage"), snapshot.roots?.[row.store]?.path], [tr("path"), row.source_path]];
      const detailsNode = (facts, error) => element("div", {}, [
        description && !row.error ? element("p", { text: description }) : null,
        detailFacts(facts), error ? element("p", { className: "extension-error", text: error }) : null,
      ]);
      const create = () => button("createDeployment", () => deployments.edit(row.id));
      const installActions = () => [
        ...(row.state === "installed" && row.recipe_ref ? [button("checkUpdate", () => checkUpdate(row))] : []),
        ...(["queued", "installing"].includes(row.state)
          ? [button("cancel", () => operate({ action: "cancel", id: row.id }))]
          : [button("uninstall", () => removeForm(row))]),
      ];
      const configured = grouped.get(row.id);
      if (configured?.length) return configured.map(deployment => {
        const { runtime, verification, ...controls } = deployments.presentation(deployment);
        return { ...controls, key: `deployment:${deployment.id}`, title: deployment.name, tags,
          description: deployment.service_error || `${title} · ${runtime}`, meta: [size, verification],
          actions: [...controls.actions, ...(row.state === "installed" ? [create()] : []), ...installActions()],
          detail: () => detailsNode([
            [tr("title"), title], [tr("runtime"), runtime], [tr("context"), deployment.config.context_tokens.toLocaleString()],
            [tr("parallel"), deployment.config.parallel], [tr("activeRequests"), deployment.active_requests || "0"],
            [tr("verify"), verification], ...info,
          ], deployment.service_error || row.error),
        };
      });
      return [{ key: `installation:${row.id}`, title, description, tags, meta: [size],
        badge: { text: status, tone: problem ? "error" : "" },
        primary: row.state === "installed" ? create() : problem ? button("retry", () => operate({ action: "retry", id: row.id })) : null,
        actions: installActions(), detail: () => detailsNode(info, row.error),
      }];
    });
  }
  function render() {
    const isLocal = tab === "local";
    for (const [control, selected] of [[local, isLocal], [store, !isLocal]]) {
      control.setAttribute("aria-selected", String(selected));
      control.tabIndex = selected ? 0 : -1;
    }
    localPanel.hidden = !isLocal;
    storePanel.hidden = isLocal;
    importing.disabled = storage.disabled = cache.disabled = !!snapshot?.read_only;
    if (isLocal) {
      const needle = filter.value.trim().toLocaleLowerCase();
      const rows = installationRows().filter(row => `${row.title} ${row.description} ${row.tags.join(" ")}`.toLocaleLowerCase().includes(needle));
      localBrowser.setItems(rows, { pageKey: needle, busy: !snapshot && !!reader, empty: needle ? browserText("empty", "没有匹配的扩展") : tr("emptyLocal") });
      return;
    }
    submitSearch.disabled = refreshStore.disabled = !!reader;
    const rows = (catalog?.items || []).map(row => {
      const model = row.local_model, label = localizedModel(model);
      return { key: row.version_id, title: label?.name || row.name || row.id, description: label?.description || row.description,
        tags: (model?.tags || []).map(tag => localModelLabel(tag.label)),
        meta: (model?.weights || []).map(weight => `${weight.quantization}: ${bytes(weight.bytes ?? (weight.files || []).reduce((sum, file) => sum + file.bytes, 0))}`),
        primary: button("details", () => storeBrowser.open(row.version_id)), onSelect: view => details(row, view),
      };
    });
    storeBrowser.setItems(rows, { pageKey: query, busy: !!reader, empty: query ? browserText("empty", "没有匹配的扩展") : tr("emptyStore"),
      onNext: rows.length < (catalog?.total || 0) ? () => load({ append: true }) : null,
    });
  }
  async function checkUpdate(row) {
    const epoch = generation;
    try {
      const data = await call({ action: "updates", id: row.id });
      if (epoch !== generation || !active || tab !== "local") return;
      const latest = data.items?.find(item => item.id === row.recipe_ref);
      if (latest && latest.version_id !== row.recipe_version_id) await details(latest);
      else report(tr(latest ? "upToDate" : "updateUnavailable"));
    } catch (e) { if (epoch === generation && active && tab === "local") report(errorMessage(e), true); }
  }
  async function operate(body) {
    const epoch = generation;
    try { await call(body); if (epoch === generation) await load(); }
    catch (e) { if (epoch === generation && active) report(errorMessage(e), true); }
  }
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
  async function details(row, view = layer(localizedModel(row.local_model)?.name || row.name || row.id)) {
    let plan, accepted = false, pending = false;
    const epoch = generation;
    const valid = () => epoch === generation && !view.signal.aborted;
    function discardPlan() {
      if (!plan || accepted || epoch !== generation) return;
      const { id, revision } = plan;
      plan = null;
      void call({ action: "discard_plan", plan_id: id, revision }).catch(() => {});
    }
    void view.closed.then(discardPlan);
    view.body.classList.add("eb-detail");
    const status = element("p", { attrs: { role: "status" }, text: tr("loading") });
    view.update({ beforeClose: () => !pending });
    view.body.replaceChildren(status);
    try {
      const data = await call({ action: "detail", version: row.version_id }); if (!valid()) return;
      const doc = data.local_model; if (!doc) throw Error(tr("unavailable"));
      const description = element("p", { text: localizedModel(doc)?.description || data.description || "" });
      const form = element("div", { className: "extension-settings-form" });
      const select = element("select");
      form.append(element("label", { className: "extension-field" }, [element("span", { text: tr("profile") }), select]));
      const requirements = element("div");
      const platform = snapshot?.platform || (await api.get("/local-models", { signal: controller.signal })).data.platform;
      if (!valid()) return;
      const profiles = (doc.profiles || []).filter(p => doc.runtimes.some(r => r.id === p.runtime && r.platform === platform));
      if (!profiles.length) throw Error(tr("unavailable"));
      for (const profile of profiles) select.append(element("option", { text: localModelLabel(profile.label), attrs: { value: profile.id } }));
      function show() {
        const profile = profiles.find(p => p.id === select.value);
        const runtime = doc.runtimes.find(r => r.id === profile.runtime), weight = doc.weights.find(w => w.id === profile.weight);
        const needs = profile.requirements;
        requirements.replaceChildren(detailFacts([
          [browserText("platform", "支持平台"), [runtime.platform, runtime.min_os].filter(Boolean).join(" · ")],
          [tr("runtime"), `${weight.quantization} · ${runtime.engine} ${runtime.build} · ${runtime.backend}`],
          [tr("cpuFeatures"), (runtime.cpu_features || []).join(" · ")], [tr("driver"), runtime.min_driver],
          [`${tr("ram")} / ${tr("vram")}`, `${bytes(needs.ram_bytes)} / ${bytes(needs.vram_bytes)}`],
          [`${tr("disk")} / ${tr("margin")}`, `${bytes(needs.disk_bytes)} / ${bytes(needs.margin_bytes)}`],
          [`${tr("context")} / ${tr("output")}`, `${profile.context_tokens} / ${profile.output_tokens}`],
        ]));
        const notes = localModelLabel(needs.notes);
        if (notes) requirements.append(element("p", { text: notes }));
      }
      select.addEventListener("change", show);
      show();
      status.textContent = "";
      view.body.append(description, form, requirements);
      const planInfo = element("pre", { className: "local-model-requirements" }); view.body.append(planInfo);
      const approval = element("div", { className: "extension-settings-form" }), trust = toggle(approval, "trustRuntime", false); approval.hidden = true; view.body.append(approval);
      const install = button("install", async () => {
        if (!plan || !trust.checked || !plan.runtime_plan.ready || pending) return; pending = true; install.disabled = true;
        try { await call({ action: "install", plan_id: plan.id, revision: plan.revision, trust_code: true }); if (!valid()) return; accepted = true; view.dispose(); tab = "local"; await load(); }
        catch (e) { if (valid()) { status.textContent = errorMessage(e); install.disabled = !trust.checked; } } finally { pending = false; }
      }); install.disabled = true; trust.addEventListener("change", () => { install.disabled = !trust.checked || !plan?.runtime_plan?.ready; });
      const inspect = button("inspect", async () => {
        if (pending) return;
        inspect.disabled = true; pending = true; try { plan = await call({ action: "plan", version: data.version_id, profile: select.value }); if (!valid()) { discardPlan(); return; }
          select.disabled = true; approval.hidden = false;
          planInfo.textContent = [...(plan.model.runtime_plan.items || []).map(item => `${item.id}
${item.version_id}
SHA-256: ${item.sha256}`),
            ...(plan.model.assets || []).map(file => `${file.path} · ${bytes(file.bytes)}
SHA-256: ${file.sha256}
${file.urls.join("\n")}`)].join("\n\n"); status.textContent = plan.runtime_plan.ready ? tr("inspectNotice") : tr("unavailable");
        } catch (e) { if (valid()) status.textContent = errorMessage(e); } finally { pending = false; if (valid()) inspect.disabled = !!plan; }
      }); view.footer.append(inspect, install);
    } catch (e) { if (valid()) status.textContent = errorMessage(e); }
  }
  function cancelRead() { reader?.abort(); reader = null; clearTimeout(timer); }
  function updateLabels() {
    for (const [node, key] of [[local, "local"], [store, "store"], [refresh, "refresh"], [refreshStore, "refresh"], [importing, "import"], [storage, "storage"], [hardware, "hardware"], [cache, "cache"]]) node.textContent = tr(key);
    const summary = resourceMenu.querySelector("summary"), more = browserText("more", "更多操作");
    summary.textContent = more;
    summary.setAttribute("aria-label", more);
    resourceMenu.querySelector("[role=group]").setAttribute("aria-label", more);
    tabs.setAttribute("aria-label", tr("title"));
    filter.placeholder = t("ecosystem.filter");
    filter.setAttribute("aria-label", filter.placeholder);
    search.placeholder = tr("search");
    search.setAttribute("aria-label", search.placeholder);
    submitSearch.textContent = t("store.searchButton");
    render();
  }
  const unsubscribeTarget = subscribeTarget(() => {
    generation++;
    cancelRead();
    controller.abort();
    controller = new AbortController();
    snapshot = catalog = null;
    localBrowser.reset(); storeBrowser.reset(); resourceMenu.open = false;
    for (const view of [...layers]) view.dispose();
    if (active) void load(); else render();
  });
  const unsubscribeLocale = subscribeLocale(updateLabels);
  updateLabels();
  return {
    hasPendingChanges: () => deployments.hasPendingChanges(),
    selectSection(section) {
      const selected = section === "local-models";
      if (active === selected) return;
      active = selected;
      if (active) void load();
      else { cancelRead(); resourceMenu.open = false; localBrowser.closeDetail(false); storeBrowser.closeDetail(false); }
    },
    destroy() {
      active = false;
      generation++;
      cancelRead(); controller.abort(); unsubscribeTarget(); unsubscribeLocale();
      localBrowser.destroy(); storeBrowser.destroy();
      for (const view of [...layers]) view.dispose();
    },
  };
}
