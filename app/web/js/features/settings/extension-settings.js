import { api } from "../../api/client.js";
import { targetState, subscribeTarget } from "../../api/target.js";
import { subscribeLocale } from "../../i18n.js";
import { element, errorMessage } from "../../utils/dom.js";
import { openLayer } from "../../components/layers.js";
import { createExtensionSettingsForm, rebaseSettingsDraft } from "./extension-settings-form.js";
import { settingsText, localizedSetting } from "./extension-settings-text.js";

export function extensionSettingsDescriptor(item, kind = item?.kind) {
  if (!item?.settings?.form || !kind) return null;
  return { kind, id: item.directory_id || item.id, title: item.name || item.title || item.id };
}
export function extensionSettingsButton(descriptor, owner) {
  if (!descriptor) return null;
  const button = element("button", { className: "secondary-button eb-configure", text: settingsText("configure"), attrs: { type: "button" } });
  button.addEventListener("click", () => { void openExtensionSettings(descriptor, { owner, opener: button }); });
  return button;
}
export async function openExtensionSettings(descriptor, { owner, opener } = {}) {
  const device = () => targetState().selected?.id || "local", ownDevice = device();
  const controller = new AbortController(); let snapshot = null, renderer = null, busy = false, ended = false, confirmAction = "";
  const path = `/extensions/${encodeURIComponent(descriptor.kind)}/${encodeURIComponent(descriptor.id)}/settings`;
  const note = element("p", { attrs: { role: "status", tabindex: "-1" } }), content = element("div");
  const createButton = (key, action) => { const button = element("button", { className: "secondary-button", text: settingsText(key), attrs: { type: "button" } }); button.addEventListener("click", () => { void action(); }); return button; };
  const discard = createButton("discardClose", () => layer.dispose()); discard.hidden = true;
  const layer = openLayer({ owner, opener, title: descriptor.title, width: "46rem", height: "80%", beforeClose() {
    if (busy) return false;
    if (!renderer?.dirty()) return true;
    note.textContent = settingsText("discard"); discard.hidden = false; return false;
  }, onClosed() { ended = true; controller.abort(); renderer?.dispose(); unTarget?.(); unLocale?.(); } });
  let unTarget, unLocale;
  const current = () => !ended && ownDevice === device() && !controller.signal.aborted;
  const notify = (message, error = false) => { note.textContent = message; note.className = error ? "extension-error" : "extension-help"; };
  const reload = createButton("reload", () => load(true)); reload.hidden = true;
  const preview = createButton("preview", () => previewTemplates());
  const previews = element("div", { className: "extension-settings-previews" });
  const save = createButton("save", () => submit());
  const cancel = createButton("cancel", () => layer.close());
  const reset = createButton("reset", () => destructive("reset")), clear = createButton("clear", () => destructive("clear"));
  layer.body.append(note, content, previews); layer.footer.append(reset, clear, preview, reload, discard, cancel, save);
  function setBusy(value) { busy = value; for (const b of [reset, clear, preview, reload, discard, cancel, save]) b.disabled = value || (!snapshot && b !== cancel); content.inert = value; }
  function render(data, retained) {
    previews.replaceChildren(); renderer?.dispose(); snapshot = data; renderer = createExtensionSettingsForm(data, retained); renderer.element.addEventListener("submit", event => { event.preventDefault(); void submit(); });
    renderer.element.addEventListener("input", () => { confirmAction = ""; }); content.replaceChildren(renderer.element);
    layer.update({ title: localizedSetting(data.form.title, descriptor.title, data.form.default_locale) });
  }
  async function load(preserve = false) {
    const before = snapshot && { ...snapshot, values: renderer.baseline.values }, draft = preserve && renderer ? renderer.collect() : null;
    if (!current() || busy) return;
    setBusy(true); notify(settingsText("loading"));
    try {
      const { data } = await api.get(path, { signal: controller.signal }); if (!current()) return;
      const retained = draft ? rebaseSettingsDraft(before, draft, data) : null;
      render(data, retained); reload.hidden = true; discard.hidden = true;
      notify(preserve ? settingsText("rebase") : data.valid ? "" : settingsText("invalid"), !data.valid);
    } catch (error) { if (current()) notify(errorMessage(error), true); }
    finally { if (current()) setBusy(false); }
  }
  async function previewTemplates() {
    if (!current() || busy || !snapshot) return;
    const input = { revision: snapshot.revision, schema_digest: snapshot.schema_digest, ...renderer.collect() };
    setBusy(true);
    try {
      const { data } = await api.post(path + "/preview", input, { ifMatch: `"${snapshot.revision}"`, signal: controller.signal });
      if (!current()) return;
      previews.replaceChildren();
      for (const item of data.templates || []) {
        previews.append(element("h4", { text: item.file }), element("pre", { text: item.text ?? item.error, className: item.error ? "extension-error" : "" }));
      }
      if (!data.templates?.length) previews.append(element("p", { text: settingsText("noTemplates") }));
      notify(data.valid ? "" : settingsText("invalid"), !data.valid);
    } catch (error) { if (current()) notify(errorMessage(error), true); }
    finally { if (current()) setBusy(false); }
  }
  async function submit(options = {}) {
    if (!current() || busy || !snapshot || !renderer) return;
    const input = { revision: snapshot.revision, schema_digest: snapshot.schema_digest, ...renderer.collect(), ...options };
    const guard = { ifMatch: `"${snapshot.revision}"`, signal: controller.signal };
    setBusy(true); notify(settingsText("saving"));
    try {
      if (!options.reset) {
        const { data } = await api.post(path + "/preview", input, guard); if (!current()) return;
        if (!data.valid) { setBusy(false); renderer.setIssues(data.issues); notify(settingsText("invalid"), true); return; }
      }
      if (!current()) return;
      const { data } = await api.put(path, input, guard); if (!current()) return;
      render(data); confirmAction = ""; reload.hidden = discard.hidden = true; notify(settingsText("saved"));
      window.dispatchEvent(new CustomEvent("mdo-extension-settings-changed", { detail: { ...descriptor, revision: data.revision } }));
    } catch (error) {
      if (current()) { reload.hidden = false; notify(error?.status === 412 ? settingsText("conflict") : error?.code === "network_error" ? settingsText("uncertain") : errorMessage(error), true); }
    } finally { if (current()) setBusy(false); }
  }
  async function destructive(action) {
    if (confirmAction !== action) { confirmAction = action; notify(settingsText(action === "clear" ? "clearConfirm" : "resetConfirm"), true); return; }
    await submit({ reset: true, clear_secrets: action === "clear", values: {}, secrets: {} });
  }
  unTarget = subscribeTarget(() => { if (ownDevice !== device()) layer.dispose(); });
  unLocale = subscribeLocale(() => {
    for (const [button, key] of [[preview,"preview"],[save,"save"],[cancel,"cancel"],[reset,"reset"],[clear,"clear"],[reload,"reload"],[discard,"discardClose"]]) button.textContent = settingsText(key);
    if (snapshot && current()) layer.update({ title: localizedSetting(snapshot.form.title, descriptor.title, snapshot.form.default_locale) });
  });
  await load();
  return layer;
}
