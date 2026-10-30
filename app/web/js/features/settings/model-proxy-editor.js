import { setUiAttribute as writeUiAttribute, t as uiText, uiMessage, t } from "../../i18n.js";

import { element } from "../../utils/dom.js";

const clone = value => structuredClone(value);
function copy(tag, key, fallback, attrs = {}) {
  return element(tag, { text: uiMessage(key, {}, fallback), attrs: { ...attrs,
    "data-model-copy-key": key, "data-model-copy-params": "{}",
    "data-model-copy-fallback": fallback } });
}

// The provider/model editors use one proxy form and the same tuple contract.
// Secret references are edited here; resolved passwords never reach this UI.
export function createModelProxyEditor(value, { allowProvider = false,
  collapsible = true, titleKey = "modelProxy.advanced", namePrefix = "proxy_", onChange = () => {} } = {}) {
  let saved = clone(value || { kind: "global" });
  const root = element(collapsible ? "details" : "section", {
    className: collapsible ? "model-advanced model-proxy-editor" : "model-proxy-inline" });
  root.append(copy(collapsible ? "summary" : "h4", titleKey, uiText("modelProxy.advanced", {}, "高级网络配置")));
  if (collapsible) root.open = saved.kind !== "global";
  const body = element("div", { className: collapsible ? "model-advanced-body" : "model-proxy-body" });
  const mode = element("select", { attrs: { name: namePrefix + "kind" } });
  const choices = [["global", "modelProxy.global", uiText("modelProxy.global", {}, "跟随全局设置")],
    ...(allowProvider ? [["provider", "modelProxy.provider", uiText("modelProxy.provider", {}, "跟随供应商")]] : []),
    ["none", "modelProxy.direct", uiText("modelProxy.direct", {}, "直连（不使用代理）")],
    ["system", "settings.proxySystem", uiText("settings.proxySystem", {}, "使用系统代理")],
    ["http-connect", null, "HTTP CONNECT"], ["socks5", null, "SOCKS5"]];
  for (const [kind, key, label] of choices)
    mode.append(key ? copy("option", key, label, { value: kind }) : element("option", { text: label, attrs: { value: kind } }));
  mode.value = saved.kind || "global";
  const label = (key, fallback, control) => element("label", { className: "model-field" }, [copy("span", key, fallback), control]);
  body.append(label("settings.proxyKind", uiMessage("settings.proxyKind", {}, "模型请求代理"), mode),
    copy("p", allowProvider ? "modelProxy.modelHelp" : "modelProxy.providerHelp",
      allowProvider ? uiText("modelProxy.modelHelp", {}, "默认直接跟随全局。选择跟随供应商时使用该供应商的代理；独立配置会覆盖整组代理参数。")
        : uiText("modelProxy.providerHelp", {}, "默认跟随全局。模型选择跟随供应商后，使用此处设置；直连可绕过全局代理。")));
  const fields = {};
  const manual = element("div", { className: "model-form-grid model-proxy-fields" });
  for (const [name, key, fallback, initial, limit] of [
    ["host", "settings.proxyHost", uiText("settings.proxyHost", {}, "代理主机"), saved.host || "", 255],
    ["port", "settings.proxyPort", uiText("settings.proxyPort", {}, "代理端口"), saved.port || "", 5],
    ["user", "settings.proxyUser", uiText("settings.proxyUser", {}, "用户名（可选）"), saved.user || "", 255],
    ["secret_ref", "settings.proxySecretRef", uiText("settings.proxySecretRef", {}, "密码引用（可选）"), saved.credential?.secret_ref || "", 2048],
    ["bypass", "settings.proxyBypass", uiText("settings.proxyBypass", {}, "不走代理的地址"), saved.bypass || "", 1023],
  ]) {
    const input = element("input", { attrs: { name: namePrefix + name,
      type: name === "port" ? "number" : "text", autocomplete: "off", maxlength: limit,
      ...(name === "port" ? { min: 1, max: 65535, step: 1 } : {}) } });
    input.value = initial; fields[name] = input;
    if (name === "secret_ref") writeUiAttribute(input, "placeholder", "env:MY_PROXY_PASSWORD");
    manual.append(label(key, fallback, input));
  }
  body.append(manual, copy("p", "modelProxy.secretHelp", uiText("modelProxy.secretHelp", {}, "代理密码使用 env:、file: 或 vault: 等引用，不填写密码明文。")));
  root.append(body);
  function read() {
    if (!["http-connect", "socks5"].includes(mode.value)) return { ...clone(saved), kind: mode.value };
    const secret = fields.secret_ref.value.trim();
    return { kind: mode.value, host: fields.host.value.trim(), port: Number(fields.port.value),
      user: fields.user.value.trim(), bypass: fields.bypass.value.trim(),
      ...(secret ? { credential: { secret_ref: secret } } : {}) };
  }
  function sync(notify = true) {
    const active = ["http-connect", "socks5"].includes(mode.value);
    manual.hidden = !active;
    for (const input of Object.values(fields)) input.disabled = !active;
    fields.host.required = fields.port.required = active;
    fields.user.setCustomValidity?.(active && fields.secret_ref.value.trim() && !fields.user.value.trim()
      ? t("settings.proxyUserRequired", {}, "配置密码引用时须填写代理用户名。") : "");
    if (notify) onChange(read());
  }
  for (const control of [mode, ...Object.values(fields)]) {
    control.addEventListener("input", () => sync());
    control.addEventListener("change", () => sync());
  }
  sync(false);
  return { element: root, read, rebase(value) { saved = clone(value || { kind: "global" }); } };
}
