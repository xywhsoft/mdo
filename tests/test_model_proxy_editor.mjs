import assert from "node:assert/strict";
import test from "node:test";
import { createModelProxyEditor } from "../app/web/js/features/settings/model-proxy-editor.js";
import { testDocument, descendants } from "./helpers/extension-dom.mjs";

function fixture(value, options = {}) {
  const previous = globalThis.document;
  const document = testDocument();
  const create = document.createElement;
  document.createElement = tag => {
    const node = create(tag);
    node.setCustomValidity = message => { node.validationMessage = message; };
    return node;
  };
  globalThis.document = document;
  const changes = [];
  const editor = createModelProxyEditor(value, { ...options, onChange: value => changes.push(value) });
  const prefix = options.namePrefix || "proxy_";
  const controls = Object.fromEntries(descendants(editor.element).filter(n => n.name)
    .map(node => [node.name.slice(prefix.length), node]));
  return { editor, controls, changes, change(name, value) {
    controls[name].value = value; controls[name].dispatchEvent(new Event("input"));
  }, restore() { globalThis.document = previous; } };
}

test("provider and model defaults are global, with explicit provider inheritance only on models", () => {
  for (const allowProvider of [false, true]) {
    const f = fixture(undefined, { allowProvider });
    try {
      assert.equal(f.controls.kind.value, "global");
      assert.equal(f.editor.element.open, false);
      assert.deepEqual(f.editor.read(), { kind: "global" });
      assert.equal(f.controls.host.disabled, true);
      assert.equal(f.controls.kind.children.some(n => n.value === "provider"), allowProvider);
      assert.equal(f.changes.length, 0);
    } finally { f.restore(); }
  }
});

test("manual proxy controls validate and serialize their own tuple and reference", () => {
  const f = fixture();
  try {
    f.change("kind", "socks5");
    assert.equal(f.controls.host.disabled, false); assert.equal(f.controls.host.required, true);
    f.change("host", " 127.0.0.1 "); f.change("port", "1080");
    f.change("secret_ref", " env:MY_PROXY_SECRET ");
    assert.ok(f.controls.user.validationMessage);
    f.change("user", " operator ");
    assert.equal(f.controls.user.validationMessage, "");
    assert.deepEqual(f.editor.read(), { kind: "socks5", host: "127.0.0.1", port: 1080,
      user: "operator", bypass: "", credential: { secret_ref: "env:MY_PROXY_SECRET" } });
    f.change("kind", "system");
    assert.equal(f.controls.user.validationMessage, "");
    assert.equal(f.controls.host.disabled, true);
    assert.deepEqual(f.editor.read(), { kind: "system" });
    f.change("kind", "http-connect");
    assert.equal(f.editor.read().host, "127.0.0.1");
  } finally { f.restore(); }
});

test("inheritance retains saved local parameters without mutating shared provider data or later edits", () => {
  const original = { kind: "http-connect", host: "saved", port: 8080, user: "local",
    credential: { secret_ref: "file:proxy.key" } };
  const f = fixture(original, { allowProvider: true });
  try {
    assert.equal(f.editor.element.open, true);
    f.change("host", "edited");
    f.change("kind", "provider");
    assert.deepEqual(f.editor.read(), { ...original, kind: "provider" });
    assert.equal(original.kind, "http-connect"); assert.equal(original.host, "saved");
    f.editor.rebase({ ...original, credential: { secret_ref: "file:new.key" } });
    assert.equal(f.editor.read().credential.secret_ref, "file:new.key");
    f.change("kind", "http-connect");
    assert.equal(f.editor.read().host, "edited");
  } finally { f.restore(); }
});

test("provider and model controls have distinct names in the shared add-models wizard", () => {
  const a = fixture(undefined, { namePrefix: "provider_proxy_", collapsible: false });
  const b = fixture(undefined, { namePrefix: "model_proxy_", allowProvider: true });
  try {
    const names = descendants(a.editor.element).concat(descendants(b.editor.element))
      .filter(n => n.name).map(n => n.name);
    assert.equal(new Set(names).size, names.length);
    assert.equal(a.editor.element.tagName, "SECTION");
  } finally { b.restore(); a.restore(); }
});
