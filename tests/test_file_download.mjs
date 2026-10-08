import assert from "node:assert/strict";
import test from "node:test";
import { saveBlobFile, ANDROID_EXPORT_MAX_BYTES } from "../app/web/js/utils/file-download.js";

function fixture({ android = false, clickFails = false } = {}) {
  const globals = ["document", "window", "URL", "XsExport"];
  const prior = Object.fromEntries(globals.map((key) => [key, Object.getOwnPropertyDescriptor(globalThis, key)]));
  const urls = [], clicks = [], timers = [], revoked = [];
  let anchor;
  globalThis.URL = { createObjectURL(blob) { urls.push(blob); return "blob:fixture"; },
    revokeObjectURL(url) { revoked.push(url); } };
  globalThis.window = { setTimeout(fn, ms) { timers.push({ fn, ms }); } };
  globalThis.document = { body: { append(node) { node.isConnected = true; } },
    createElement(tag) { assert.equal(tag, "a"); return anchor = {
      isConnected: false, remove() { this.isConnected = false; },
      click() { clicks.push({ connected: this.isConnected, filename: this.download, href: this.href });
        if (clickFails) throw new Error("download handoff refused"); }
    }; } };
  if (android) globalThis.XsExport = { save() {} }; else delete globalThis.XsExport;
  return { urls, clicks, timers, revoked, anchor: () => anchor,
    restore() { for (const key of globals) {
      if (prior[key]) Object.defineProperty(globalThis, key, prior[key]); else delete globalThis[key];
    } } };
}

test("Android exports reject excess bytes before a Blob URL, DOM click or native handoff", () => {
  const ctx = fixture({ android: true });
  try {
    assert.throws(() => saveBlobFile({ blob: { size: ANDROID_EXPORT_MAX_BYTES + 1 }, filename: "large.json" }),
      { code: "export_platform_limit" });
    assert.equal(ctx.urls.length, 0); assert.equal(ctx.clicks.length, 0); assert.equal(ctx.timers.length, 0);
  } finally { ctx.restore(); }
});

test("the Android boundary keeps a connected anchor and filename for the document listener", () => {
  const ctx = fixture({ android: true }), blob = { size: ANDROID_EXPORT_MAX_BYTES };
  try {
    saveBlobFile({ blob, filename: "技能包.mdo-extension.json" });
    assert.deepEqual(ctx.urls, [blob]);
    assert.deepEqual(ctx.clicks, [{ connected: true, filename: "技能包.mdo-extension.json", href: "blob:fixture" }]);
    assert.equal(ctx.anchor().isConnected, false); assert.equal(ctx.timers[0].ms, 60000);
    ctx.timers[0].fn(); assert.deepEqual(ctx.revoked, ["blob:fixture"]);
  } finally { ctx.restore(); }
});

test("browser downloads above the Android limit retain their ordinary handoff", () => {
  const ctx = fixture();
  try {
    saveBlobFile({ blob: { size: 64 * 1024 * 1024 }, filename: "complete.json" });
    assert.equal(ctx.urls.length, 1); assert.equal(ctx.clicks[0].connected, true);
  } finally { ctx.restore(); }
});

test("failed browser handoff removes the anchor and releases its temporary URL", () => {
  const ctx = fixture({ clickFails: true });
  try {
    assert.throws(() => saveBlobFile({ blob: { size: 1 }, filename: "small.md" }), /handoff refused/);
    assert.equal(ctx.anchor().isConnected, false); assert.equal(ctx.timers.length, 1);
    ctx.timers[0].fn(); assert.deepEqual(ctx.revoked, ["blob:fixture"]);
  } finally { ctx.restore(); }
});
