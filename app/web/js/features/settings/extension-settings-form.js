import { element } from "../../utils/dom.js";
import { subscribeLocale } from "../../i18n.js";
import { settingsText, localizedSetting } from "./extension-settings-text.js";

export const settingsFields = schema => (schema?.groups || []).flatMap(group => group.fields || []);
export function normalizedSettingsValues(snapshot) {
  return Object.fromEntries(settingsFields(snapshot.form).filter(field => field.type !== "secret").map(field => {
    const value = Object.hasOwn(snapshot.values || {}, field.name) ? snapshot.values[field.name] : field.default;
    return [field.name, value ?? (field.type === "switch" ? false : field.type === "multiselect" ? [] : ["integer", "number"].includes(field.type) ? null : "")];
  }));
}
export function rebaseSettingsDraft(before, draft, latest) {
  const allowed = new Map(settingsFields(latest.form).map(field => [field.name, field]));
  const values = { ...latest.values }, secrets = {};
  for (const [key, value] of Object.entries(draft.values || {})) if (allowed.get(key)?.type !== "secret" && allowed.has(key) && JSON.stringify(value) !== JSON.stringify(before.values[key])) values[key] = value;
  for (const [key, value] of Object.entries(draft.secrets || {})) if (allowed.get(key)?.type === "secret") secrets[key] = value;
  return { values, secrets };
}

export function createExtensionSettingsForm(snapshot, retained = null) {
  const form = element("form", { className: "extension-authoring extension-settings-form", attrs: { novalidate: "" } });
  const bindings = [], fields = new Map(), defaults = snapshot.form.default_locale;
  const initial = retained?.values || snapshot.values || {};
  const translate = (node, value, fallback = "", property = "textContent") => {
    const apply = () => { node[property] = localizedSetting(value, fallback, defaults); };
    bindings.push(apply); apply();
  };
  for (const group of snapshot.form.groups) {
    const fieldset = element("fieldset", { className: "extension-settings-group" });
    const legend = element("legend"); translate(legend, group.title, group.id); fieldset.append(legend);
    if (group.description) { const text = element("p", { className: "extension-help" }); translate(text, group.description); fieldset.append(text); }
    for (const spec of group.fields) {
      const row = element("div", { className: "extension-field" }), label = element("label");
      const id = `extension-setting-${spec.name}`; label.htmlFor = id; translate(label, spec.label, spec.name);
      const control = element(spec.type === "textarea" ? "textarea" : ["select", "multiselect"].includes(spec.type) ? "select" : "input", {
        attrs: { id, name: spec.name, autocomplete: spec.type === "secret" ? "new-password" : "off", spellcheck: "false" } });
      if (control.tagName === "INPUT") control.type = spec.type === "secret" ? "password" : spec.type === "switch" ? "checkbox" : ["integer", "number"].includes(spec.type) ? "number" : spec.type === "url" ? "url" : "text";
      if (spec.type === "switch") control.setAttribute("role", "switch");
      if (["integer", "number"].includes(spec.type)) { control.step = spec.type === "integer" ? "1" : "any"; for (const bound of ["min", "max"]) if (spec[bound] != null) control[bound] = String(spec[bound]); }
      if (spec.type === "textarea") control.rows = 4;
      if (spec.placeholder) translate(control, spec.placeholder, "", "placeholder");
      if (spec.type === "multiselect") control.multiple = true;
      if (spec.options) for (const option of spec.options) {
        const value = typeof option === "string" ? option : option.value, node = element("option", { attrs: { value } });
        translate(node, typeof option === "string" ? option : option.label, value); control.append(node);
      }
      const value = Object.hasOwn(initial, spec.name) ? initial[spec.name] : spec.default;
      if (spec.type === "switch") control.checked = value === true;
      else if (spec.type === "multiselect") for (const option of control.options) option.selected = Array.isArray(value) && value.includes(option.value);
      else control.value = spec.type === "secret" ? "" : value ?? "";
      if (spec.type === "switch") { row.classList.add("extension-settings-switch"); row.append(control, label); }
      else row.append(label, control);
      let clearKey;
      if (spec.type === "secret") {
        if (snapshot.secrets?.[spec.name]) { const apply = () => { control.placeholder = settingsText("configured"); }; bindings.push(apply); apply(); }
        clearKey = element("input", { attrs: { type: "checkbox", role: "switch" } }); const clearLabel = element("span"), wrap = element("label", { className: "extension-check extension-settings-clear-switch" }, [clearKey, clearLabel]);
        const apply = () => { clearLabel.textContent = settingsText("clearKey"); }; bindings.push(apply); apply(); row.append(wrap);
        if (retained?.secrets && Object.hasOwn(retained.secrets, spec.name)) { const secret = retained.secrets[spec.name]; clearKey.checked = secret === null; control.value = secret ?? ""; }
      }
      if (spec.description) { const text = element("small", { className: "extension-help" }); translate(text, spec.description); row.append(text); }
      const error = element("small", { className: "extension-error", attrs: { id: id + "-error", role: "status" } });
      control.setAttribute("aria-describedby", id + "-error"); control.setAttribute("aria-required", String(Boolean(spec.required))); row.append(error);
      fields.set(spec.name, { spec, row, control, clearKey, error }); fieldset.append(row);
    }
    form.append(fieldset);
  }
  function collect() {
    const values = {}, secrets = {};
    for (const [name, { spec, control, clearKey }] of fields) {
      if (spec.type === "secret") { if (clearKey.checked) secrets[name] = null; else if (control.value) secrets[name] = control.value; }
      else if (spec.type === "switch") values[name] = control.checked;
      else if (spec.type === "multiselect") values[name] = [...control.selectedOptions].map(option => option.value);
      else if (["integer", "number"].includes(spec.type)) values[name] = control.value === "" ? null : Number(control.value);
      else values[name] = control.value;
    }
    return { values, secrets };
  }
  function visible() {
    const { values } = collect();
    for (const { spec, row, clearKey, control } of fields.values()) {
      row.hidden = Boolean(spec.visible_if && JSON.stringify(values[spec.visible_if.field]) !== JSON.stringify(spec.visible_if.equals));
      if (clearKey) control.disabled = clearKey.checked;
    }
  }
  form.addEventListener("input", visible); form.addEventListener("change", visible); visible();
  const original = JSON.stringify({ values: normalizedSettingsValues(snapshot), secrets: {} });
  // Optional absent fields appear as empty controls; their initial display is
  // the baseline, rather than immediately making an unopened form dirty.
  const unsubscribe = subscribeLocale(() => { for (const binding of bindings) binding(); });
  return { element: form, collect, baseline: { values: normalizedSettingsValues(snapshot), secrets: {} },
    dirty: () => JSON.stringify(collect()) !== original,
    setIssues(issues = []) {
      for (const field of fields.values()) { field.error.textContent = ""; field.control.removeAttribute("aria-invalid"); }
      for (const issue of issues) { const field = fields.get(issue.field); if (field) { field.error.textContent = settingsText(issue.code); field.control.setAttribute("aria-invalid", "true"); } }
      const first = issues.map(issue => fields.get(issue.field)).find(field => field && !field.row.hidden);
      first?.control.focus();
    },
    dispose() { unsubscribe(); for (const field of fields.values()) if (field.spec.type === "secret") field.control.value = ""; },
  };
}
