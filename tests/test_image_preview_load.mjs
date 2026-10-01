import assert from "node:assert/strict";
import test from "node:test";
import { readFile } from "node:fs/promises";
import { createImagePreview } from "../app/web/js/features/chat/image-preview.js";
import { loadLocale } from "../app/web/js/i18n.js";

// A tiny event/DOM host models ownership and focus; browser fixtures separately
// exercise real image decoding, native dialog events and viewport geometry.
class Node {
  attrs = new Map(); dataset = {}; listeners = new Map(); hidden = false;
  isConnected = true; textContent = ""; alt = ""; naturalWidth = 0;
  constructor(tag = "div") { this.tag = tag; }
  set src(value) { this.setAttribute("src", value); }
  get src() { return this.getAttribute("src") || ""; }
  setAttribute(key, value) { this.attrs.set(key, String(value)); }
  getAttribute(key) { return this.attrs.get(key) ?? null; }
  removeAttribute(key) { this.attrs.delete(key); }
  hasAttribute(key) { return this.attrs.has(key); }
  addEventListener(type, fn) {
    if (!this.listeners.has(type)) this.listeners.set(type, []);
    this.listeners.get(type).push(fn);
  }
  fire(type, target = this) {
    for (const fn of this.listeners.get(type) || []) fn({ target, preventDefault() {} });
  }
  cloneNode() {
    const node = new Node(this.tag); node.attrs = new Map(this.attrs); node.alt = this.alt;
    return node;
  }
  replaceWith(node) { this.isConnected = false; this.parent.image = node; node.parent = this.parent; }
  closest(selector) { return selector === "button[data-image-preview]" && this.tag === "button" ? this : null; }
  querySelector(selector) { return selector === "img" ? this.image : null; }
  focus() { document.activeElement = this; }
}

test("failed preview retries once, preserves names/focus, and rejects old image events", async () => {
  const previous = { document: globalThis.document, Element: globalThis.Element, fetch: globalThis.fetch };
  const doc = new Node(); doc.documentElement = new Node(); doc.querySelectorAll = () => [];
  const dialog = new Node(); dialog.open = false;
  dialog.showModal = () => { dialog.open = true; };
  // Closing is deliberately queued, matching the native browser dialog.
  dialog.close = () => { dialog.open = false; queueMicrotask(() => dialog.fire("close")); };
  const image = new Node("img"); image.parent = dialog; dialog.image = image;
  const caption = new Node(), status = new Node(), retry = new Node("button"), close = new Node("button");
  let navigate;
  globalThis.document = doc; globalThis.Element = Node;
  globalThis.fetch = async (path) => Response.json(JSON.parse(await readFile(
    new URL(`../app/web${path}`, import.meta.url), "utf8")));
  const trigger = (source, name) => {
    const button = new Node("button"); button.image = new Node("img");
    button.image.src = source; button.image.alt = name; return button;
  };
  try {
    await loadLocale("zh-CN");
    createImagePreview({ dialog, image, caption, status, retryButton: retry, closeButton: close,
      navigation: { subscribe(fn) { navigate = fn; } } });
    const first = trigger("/first.png", "第一张.png"), second = trigger("/second.png", "第二张.png");
    doc.fire("click", first);
    assert.equal(dialog.dataset.imageState, "loading");
    assert.equal(dialog.image.hidden, true);
    assert.match(status.textContent, /加载/);
    assert.equal(retry.hidden, true);
    const failed = dialog.image;
    failed.fire("error");
    assert.equal(dialog.dataset.imageState, "error");
    assert.equal(caption.textContent, "第一张.png");
    assert.equal(retry.hidden, false);
    assert.equal(doc.activeElement, close);
    await loadLocale("en-US");
    assert.match(status.textContent, /image/i);
    assert.equal(retry.textContent, "Reload image");
    assert.equal(caption.textContent, "第一张.png");
    retry.focus(); retry.fire("click");
    const retried = dialog.image;
    assert.equal(retried.src, "/first.png");
    assert.equal(doc.activeElement, retry);
    assert.equal(retry.hidden, false);
    assert.equal(retry.getAttribute("aria-disabled"), "true");
    retry.fire("click"); assert.equal(dialog.image, retried);
    failed.naturalWidth = 1; failed.fire("load"); failed.fire("error");
    assert.equal(dialog.dataset.imageState, "loading");
    retried.naturalWidth = 1; retried.fire("load");
    assert.equal(dialog.dataset.imageState, "ready");
    assert.equal(dialog.image.hidden, false);
    assert.equal(status.hidden, true); assert.equal(retry.hidden, true);
    assert.equal(doc.activeElement, close);
    close.fire("click"); doc.fire("click", second);
    const next = dialog.image;
    await Promise.resolve();
    retried.fire("error");
    assert.equal(dialog.open, true);
    assert.equal(dialog.dataset.imageState, "loading");
    assert.equal(caption.textContent, "第二张.png");
    next.fire("load"); // A load without decoded pixels is still a failure.
    assert.equal(dialog.dataset.imageState, "error");
    navigate(); const prompt = new Node("textarea"); prompt.focus();
    await Promise.resolve(); next.fire("error");
    assert.equal(dialog.open, false); assert.equal(doc.activeElement, prompt);
    assert.equal(dialog.image.hasAttribute("src"), false);
    assert.equal(caption.textContent, ""); assert.equal(status.hidden, true);
    assert.equal(retry.hidden, true);
  } finally {
    globalThis.document = previous.document; globalThis.Element = previous.Element;
    globalThis.fetch = previous.fetch;
  }
});
