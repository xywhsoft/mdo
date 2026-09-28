import assert from "node:assert/strict";
import test from "node:test";

import { trackMobileViewport, visibleViewportBottom } from
  "../app/web/js/features/shell/mobile-viewport.js";

test("visual viewport override follows keyboard occlusion and clears on restore", () => {
  const values = new Map();
  const attributes = new Set();
  const rootValues = new Map();
  const rootAttributes = new Set();
  const root = { style: {
    setProperty: (key, value) => rootValues.set(key, value),
    removeProperty: (key) => rootValues.delete(key),
  }, toggleAttribute: (name, present) => {
    if (present) rootAttributes.add(name);
    else rootAttributes.delete(name);
  } };
  const shell = { style: {
    setProperty: (key, value) => values.set(key, value),
    removeProperty: (key) => values.delete(key),
  }, toggleAttribute: (name, present) => {
    if (present) attributes.add(name);
    else attributes.delete(name);
  }, ownerDocument: { documentElement: root } };
  const viewport = Object.assign(new EventTarget(),
    { height: 700, offsetTop: 0, scale: 1 });
  const win = Object.assign(new EventTarget(),
    { innerHeight: 700, visualViewport: viewport });
  const mobile = Object.assign(new EventTarget(), { matches: true });
  trackMobileViewport(shell, mobile, win);
  assert.equal(values.has("--app-visible-height"), false);
  viewport.height = 390;
  viewport.dispatchEvent(new Event("resize"));
  assert.equal(values.get("--app-visible-height"), "390px");
  assert.equal(rootAttributes.has("data-visual-viewport-reduced"), true);
  assert.equal(rootValues.get("--app-visual-height"), "390px");
  assert.equal(attributes.has("data-compact-visual-viewport"), false);
  viewport.height = 250;
  viewport.dispatchEvent(new Event("resize"));
  assert.equal(attributes.has("data-compact-visual-viewport"), true);
  viewport.height = 390;
  viewport.dispatchEvent(new Event("resize"));
  assert.equal(attributes.has("data-compact-visual-viewport"), false);
  viewport.offsetTop = 35;
  viewport.dispatchEvent(new Event("scroll"));
  assert.equal(values.get("--app-visible-height"), "425px");
  assert.equal(rootValues.get("--app-visual-top"), "35px");
  assert.equal(rootValues.get("--app-visual-height"), "390px");
  viewport.height = 250;
  viewport.offsetTop = 450;
  viewport.dispatchEvent(new Event("scroll"));
  assert.equal(values.has("--app-visible-height"), false);
  assert.equal(rootAttributes.has("data-visual-viewport-reduced"), true);
  assert.equal(rootValues.get("--app-visual-top"), "450px");
  assert.equal(rootValues.get("--app-visual-height"), "250px");
  viewport.scale = 1.5;
  viewport.dispatchEvent(new Event("resize"));
  assert.equal(values.has("--app-visible-height"), false);
  assert.equal(rootAttributes.has("data-visual-viewport-reduced"), false);
  assert.equal(rootValues.has("--app-visual-height"), false);
  assert.equal(attributes.has("data-compact-visual-viewport"), false);
  viewport.scale = 1;
  mobile.matches = false;
  mobile.dispatchEvent(new Event("change"));
  assert.equal(values.has("--app-visible-height"), false);
  mobile.matches = true;
  viewport.height = 700;
  viewport.offsetTop = 0;
  viewport.dispatchEvent(new Event("resize"));
  assert.equal(values.has("--app-visible-height"), false);
});

test("unavailable and nearly full visual viewports retain CSS dynamic height", () => {
  assert.equal(visibleViewportBottom(700, undefined), null);
  assert.equal(visibleViewportBottom(700, { height: 680, offsetTop: 0,
    scale: 1 }), null);
  assert.equal(visibleViewportBottom(700, { height: 390, offsetTop: 20,
    scale: 1 }), 410);
});
