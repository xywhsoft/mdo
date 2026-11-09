import assert from "node:assert/strict";
import test from "node:test";
import { settingsPatch, validateSettingsProxy } from
  "../app/web/js/features/settings/settings-view.js";

function fixture() {
  const names = ["locale", "theme", "font_size", "density", "submit_mode", "completion_sound",
    "prevent_sleep", "permission_profile", "reasoning_effort", "user_instructions", "reply_language",
    "reply_language_custom", "web_search", "memory", "schedules", "max_parallel_tools",
    "max_parallel_subagents", "max_turns", "recovery_retries", "ca_pem_path", "open_mode", "confirm_external_write", "proxy_kind",
    "proxy_host", "proxy_port", "proxy_user", "proxy_secret_ref", "proxy_bypass", "proxy_clear_secret"];
  const elements = Object.fromEntries(names.map(name => [name, {
    value: "", checked: false, disabled: false, validity: { valid: true },
    setCustomValidity(message) { this.validity.valid = !message; this.message = message; },
  }]));
  return { elements };
}

test("system mode ignores invalid manual fields without clearing the saved manual proxy", () => {
  const form = fixture();
  const fields = form.elements;
  fields.proxy_kind.value = "system";
  fields.proxy_host.value = "retained-host";
  fields.proxy_port.value = "not a number";
  fields.proxy_port.validity.valid = false;
  fields.proxy_secret_ref.value = "file:unused-reference";
  fields.proxy_clear_secret.checked = true;
  assert.equal(validateSettingsProxy(form, { transport: { proxy: { credential_configured: true } } }), true);
  assert.deepEqual(settingsPatch(form).transport.proxy, { kind: "system" });
  assert.equal(fields.proxy_host.value, "retained-host");
  for (const name of ["proxy_host", "proxy_port", "proxy_user", "proxy_secret_ref",
    "proxy_clear_secret", "proxy_bypass"]) assert.equal(fields[name].disabled, true, name);
  assert.equal(fields.proxy_host.required, false);
});

test("switching back to manual restores validation and the retained values", () => {
  const form = fixture(), fields = form.elements;
  fields.proxy_kind.value = "system"; validateSettingsProxy(form, null);
  fields.proxy_kind.value = "socks5";
  fields.proxy_host.value = "127.0.0.1"; fields.proxy_port.value = "1080";
  fields.proxy_secret_ref.value = "env:PROXY_TEST_SECRET";
  assert.equal(validateSettingsProxy(form, null), false);
  assert.ok(fields.proxy_user.message);
  assert.equal(fields.proxy_host.required, true);
  assert.equal(fields.proxy_port.min, "1");
  fields.proxy_user.value = "tester";
  assert.equal(validateSettingsProxy(form, null), true);
  const proxy = settingsPatch(form).transport.proxy;
  assert.equal(proxy.host, "127.0.0.1"); assert.equal(proxy.port, 1080);
  assert.equal(proxy.credential.secret_ref, "env:PROXY_TEST_SECRET");
  assert.equal(fields.proxy_host.disabled, false);
});

test("Agent turn settings preserve unlimited zero and a finite integer in the save patch", () => {
  const form = fixture();
  form.elements.max_turns.value = "0";
  assert.equal(settingsPatch(form).agent.max_turns, 0);
  form.elements.max_turns.value = "512";
  assert.equal(settingsPatch(form).agent.max_turns, 512);
});

test("recovery retries retain zero and configured counts in the settings form patch", () => {
  const form = fixture();
  for (const count of [0, 3, 100]) {
    form.elements.recovery_retries.value = String(count);
    assert.equal(settingsPatch(form).agent.recovery_retries, count);
  }
  delete form.elements.recovery_retries;
  assert.equal(settingsPatch(form).agent.recovery_retries, 10);
});

test("multiline proxy bypass rules save as a comma-separated list without newline characters", () => {
  const form = fixture();
  form.elements.proxy_kind.value = "none";
  form.elements.proxy_bypass.value = " localhost\r\n\n *.internal, example.com \r 127.0.0.1\n";
  assert.equal(settingsPatch(form).transport.proxy.bypass, "localhost,*.internal,example.com,127.0.0.1");
  form.elements.proxy_bypass.value = " \n,\r\n ";
  assert.equal(settingsPatch(form).transport.proxy.bypass, "");
});
