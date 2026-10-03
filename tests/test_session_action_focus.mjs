import assert from "node:assert/strict";
import test from "node:test";
import { rememberSessionActionFocus } from
  "../app/web/js/features/sessions/session-action-focus.js";

function fixture() {
  const body = { isConnected: true };
  const document = { body, activeElement: body };
  let route = { view: "workspace", projectId: "default", sessionId: "one" };
  const navigation = { get: () => route };
  const buttons = [];
  const container = { querySelectorAll: () => buttons };
  const control = (key = null) => ({
    isConnected: true, disabled: false, visible: true, inert: false,
    dataset: { sessionKey: key },
    classList: { contains: (name) => Boolean(key) && name === "session-more" },
    closest() { return this.inert ? {} : null; },
    getClientRects() { return this.visible ? [{}] : []; },
    focus(options) { document.activeElement = this; this.options = options; },
  });
  const fallback = control();
  const origin = control("default/one");
  buttons.push(origin);
  document.activeElement = origin;
  const remember = () => rememberSessionActionFocus({
    container, fallback, navigation, document,
  });
  const close = () => { document.activeElement = body; origin.isConnected = false; };
  return { body, document, buttons, fallback, origin, control, remember, close,
    navigate() { route = { ...route, sessionId: "two" }; } };
}

test("renaming a sidebar session returns focus to its replacement button", () => {
  const f = fixture();
  const restore = f.remember();
  const replacement = f.control("default/one");
  f.buttons.splice(0, 1, f.control("default/two"), replacement);
  f.close();
  restore();
  assert.equal(f.document.activeElement, replacement);
  assert.deepEqual(replacement.options, { preventScroll: true });
});

test("removing the session from the current list returns focus to the filter", () => {
  const f = fixture();
  const restore = f.remember();
  f.buttons.splice(0, 1);
  f.close();
  restore();
  assert.equal(f.document.activeElement, f.fallback);
});

test("a newer focus choice or navigation is never overridden", () => {
  for (const changeRoute of [false, true]) {
    const f = fixture();
    const restore = f.remember();
    f.close();
    const newer = f.control();
    if (changeRoute) f.navigate();
    else newer.focus();
    restore();
    assert.equal(f.document.activeElement, changeRoute ? f.body : newer);
  }
});

test("hidden, inert or disabled sidebar controls cannot regain focus", () => {
  for (const property of ["visible", "inert", "disabled"]) {
    const f = fixture();
    const restore = f.remember();
    const replacement = f.control("default/one");
    replacement[property] = property !== "visible";
    f.buttons.splice(0, 1, replacement);
    f.close();
    restore();
    assert.equal(f.document.activeElement, f.body);
  }
});

test("native dialog focus return to a stable header button is preserved", () => {
  const f = fixture();
  const header = f.control();
  f.document.activeElement = header;
  const restore = f.remember();
  f.document.activeElement = f.body;
  restore();
  assert.equal(f.document.activeElement, header);
});
