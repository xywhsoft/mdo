import assert from "node:assert/strict";
import test from "node:test";
import { TestElement, testDocument } from "./helpers/extension-dom.mjs";
import { layerSize, openLayer } from "../app/web/js/components/layers.js";

const deferred = () => { let resolve; const promise = new Promise(r => { resolve = r; }); return { promise, resolve }; };
function environment(t, width = 1200, height = 900) {
  const keys = ["document", "window", "visualViewport", "innerWidth", "innerHeight", "devicePixelRatio"];
  const saved = Object.fromEntries(keys.map(key => [key, globalThis[key]]));
  Object.assign(globalThis, { document: testDocument(), window: new EventTarget(),
    visualViewport: Object.assign(new EventTarget(), { width, height, offsetLeft: 0, offsetTop: 0 }), devicePixelRatio: 3 });
  t.after(() => Object.assign(globalThis, saved));
}
const cancel = layer => { const event = new Event("cancel", { cancelable: true }); layer.element.dispatchEvent(event); return event; };

test("dimensions accept logical pixels, rem and viewport percentages with strict input validation", () => {
  assert.equal(layerSize(800, "width"), "800px");
  assert.equal(layerSize("52rem", "width"), "52rem");
  assert.equal(layerSize("80%", "height"), "calc(var(--layer-viewport-height) * 0.8)");
  assert.equal(layerSize("auto", "height"), "auto");
  for (const bad of [0, -1, NaN, Infinity, "-2px", "0rem", "100vh", "calc(1px)", "42", null]) assert.throws(() => layerSize(bad, "width"), TypeError);
});

test("window chrome updates independently of content; close restores focus and exact scroll styles", async t => {
  environment(t); const opener = new TestElement("button"), settings = new TestElement("div"); settings.className = "settings-content";
  document.body.append(settings); settings.append(opener); opener.focus(); settings.scrollTop = 800;
  document.body.style.setProperty("overflow", "clip", "important");
  let cleaned = 0, closed = 0;
  const layer = openLayer({ title: "Tool", icon: "terminal", width: 840, height: "80%", render(root) { root.append(new TestElement("p")); return () => cleaned++; }, onClosed() { closed++; } });
  await layer.ready;
  assert.equal(layer.element.getAttribute("aria-labelledby"), layer.element.querySelector("h2").id);
  assert.equal(layer.element.querySelector("h2").textContent, "Tool");
  assert.equal(settings.style.getPropertyValue("overflow"), "hidden");
  assert.equal(layer.element.style.width, "840px", "PPI must not shrink logical dimensions");
  const content = layer.body.firstChild; layer.body.scrollTop = 140;
  layer.update({ title: "Updated", icon: "spark", width: "90%" });
  settings.scrollTop = 0; // A background refresh can temporarily collapse its layout.
  assert.equal(layer.body.firstChild, content); assert.equal(layer.body.scrollTop, 140);
  assert.throws(() => layer.update({ width: "garbage" }), TypeError);
  layer.closeButton.click(); await layer.closed;
  assert(layer.signal.aborted); assert.equal(cleaned, 1); assert.equal(closed, 1); assert.equal(document.activeElement, opener);
  assert.equal(document.body.style.getPropertyValue("overflow"), "clip"); assert.equal(document.body.style.getPropertyPriority("overflow"), "important");
  assert.equal(settings.style.getPropertyValue("overflow"), ""); assert.equal(settings.scrollTop, 800);
  layer.dispose(); await layer.close(); assert.equal(cleaned, 1);
});

test("nested windows share locks and only the top window handles Escape", async t => {
  environment(t);
  const parent = openLayer({ title: "Parent" }), child = openLayer({ title: "Child" });
  assert(cancel(parent).defaultPrevented); assert(parent.element.open);
  cancel(child); assert(!child.element.open); assert(parent.element.open); assert.equal(document.body.style.getPropertyValue("overflow"), "hidden");
  assert.equal(document.activeElement, parent.closeButton);
  cancel(parent); assert(!parent.element.open); assert.equal(document.body.style.getPropertyValue("overflow"), "");
  await Promise.all([parent.ready, child.ready]);
});

test("async close guard deduplicates close requests, can refuse, and cannot obstruct owner teardown", async t => {
  environment(t); const gate = deferred(); let calls = 0, result;
  const layer = openLayer({ beforeClose() { calls++; return gate.promise; }, onClosed(value) { result = value; } });
  const first = layer.close("first"), second = layer.close("second"); assert.equal(first, second);
  await Promise.resolve(); assert.equal(calls, 1); gate.resolve(false); assert.equal(await first, false); assert(layer.element.open);
  layer.update({ beforeClose: () => new Promise(() => {}) }); void layer.close();
  layer.dispose(); assert.equal(result.reason, "dispose"); assert(layer.signal.aborted); assert(!layer.element.open);
});

test("late async render cleanup runs once after forced teardown; failed renders release all resources", async t => {
  environment(t); const gate = deferred(); let cleaned = 0;
  const layer = openLayer({ render: async () => { await gate.promise; return () => cleaned++; } });
  await Promise.resolve(); layer.dispose(); gate.resolve(); await layer.ready; assert.equal(cleaned, 1);
  const errors = [], broken = openLayer({ render() { throw Error("renderer failed"); }, onError: error => errors.push(error.message) });
  await assert.rejects(broken.ready, /renderer failed/); assert.deepEqual(errors, ["renderer failed"]);
  assert(broken.signal.aborted); assert.equal(document.body.style.getPropertyValue("overflow"), "");
});

test("visual viewport follows mobile rotation and keyboard without applying physical pixel scaling", t => {
  environment(t, 390, 844);
  const layer = openLayer({ width: 840, height: "80%" });
  assert(layer.element.classList.contains("is-mobile")); assert(layer.element.classList.contains("is-fullscreen"));
  assert.equal(layer.element.style.width, "840px");
  Object.assign(visualViewport, { height: 400, offsetTop: 20 }); visualViewport.dispatchEvent(new Event("resize"));
  assert.equal(layer.element.style.getPropertyValue("--layer-viewport-height"), "400px"); assert.equal(layer.element.style.top, "220px");
  Object.assign(visualViewport, { width: 1000, height: 700, offsetTop: 0 }); visualViewport.dispatchEvent(new Event("resize"));
  assert(!layer.element.classList.contains("is-mobile")); assert(!layer.element.classList.contains("is-fullscreen"));
  const maximize = layer.element.querySelector(".mdo-layer-controls").firstChild;
  maximize.click(); assert(layer.element.classList.contains("is-fullscreen"));
  maximize.click(); assert(!layer.element.classList.contains("is-fullscreen"));
  layer.dispose();
  const fit = openLayer({ mobile: "fit" }); visualViewport.width = 320; visualViewport.dispatchEvent(new Event("resize"));
  assert(!fit.element.classList.contains("is-fullscreen")); fit.dispose();
});

test("backdrop closing is opt-in and requires both press and release outside; IME Escape stays open", t => {
  environment(t); const layer = openLayer();
  const pointer = (name, x, y, target = layer.element) => {
    const event = new Event(name); Object.assign(event, { clientX: x, clientY: y }); target.dispatchEvent(event);
  };
  pointer("pointerdown", 0, 0); pointer("pointerup", 0, 0); assert(layer.element.open);
  layer.update({ closeOnBackdrop: true });
  pointer("pointerdown", 20, 30); pointer("pointerup", 0, 0); assert(layer.element.open);
  const ime = new Event("cancel", { cancelable: true }); Object.assign(ime, { keyCode: 229 }); layer.element.dispatchEvent(ime); assert(layer.element.open);
  pointer("pointerdown", 0, 0); pointer("pointerup", 0, 0); assert(!layer.element.open);
});
