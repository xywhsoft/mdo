import assert from "node:assert/strict";
import test from "node:test";
import { createConversationSearch } from
  "../app/web/js/features/chat/conversation-search.js";

function fixture(check) {
  const calls = [];
  const node = (name) => Object.assign(new EventTarget(), {
    value: "", textContent: "", hidden: false,
    setAttribute() {},
    focus() { calls.push(`focus:${name}`); },
    select() {},
  });
  const bar = node("bar");
  bar.hidden = true;
  const input = node("search");
  const prompt = node("prompt");
  const count = node("count");
  let route = { view: "workspace", sessionId: "" };
  let changed;
  const navigation = {
    get: () => route,
    subscribe(fn) { changed = fn; fn(route); return () => {}; },
    select(sessionId, view = "workspace") { route = { view, sessionId }; changed(route); },
  };
  const search = createConversationSearch({ bar, input, count, prompt, navigation,
    openButtons: [node("open")], closeButton: node("close"),
    onQuery(value) { calls.push(`query:${value}`); },
    onOpen() { calls.push("hide-docks"); },
    onClose() { calls.push("restore-docks"); },
  });
  function key(fields = {}) {
    const event = new Event("keydown", { cancelable: true });
    Object.assign(event, { key: "Escape", ...fields });
    input.dispatchEvent(event);
    return event;
  }
  check({ calls, bar, input, count, search, navigation, key });
}

test("search reveals history before focus and restores cards before the composer", () => {
  fixture(({ calls, input, search, navigation }) => {
    search.open();
    search.close(true);
    assert.deepEqual(calls, [], "an unavailable search must not hide decisions");
    navigation.select("one");
    search.open();
    input.value = "kept query";
    search.open();
    assert.equal(input.value, "kept query");
    assert.deepEqual(calls, ["hide-docks", "focus:search", "hide-docks", "focus:search"]);
    search.close(true);
    search.close(true);
    assert.deepEqual(calls.slice(4), ["query:", "restore-docks", "focus:prompt"]);
  });
});

test("navigation releases hidden cards without focusing the previous conversation", () => {
  fixture(({ calls, input, count, bar, search, navigation }) => {
    navigation.select("one");
    search.open();
    input.value = "old conversation";
    count.textContent = "one match";
    navigation.select("two");
    assert.equal(bar.hidden, true);
    assert.equal(input.value, "");
    assert.equal(count.textContent, "");
    assert.deepEqual(calls, ["hide-docks", "focus:search", "query:", "restore-docks"]);
  });
});

test("IME candidate cancellation keeps search and cards in the same lifecycle", () => {
  fixture(({ calls, input, search, navigation, key }) => {
    navigation.select("one");
    search.open();
    input.dispatchEvent(new Event("compositionstart"));
    key();
    input.dispatchEvent(new Event("compositionend"));
    key({ keyCode: 229 });
    assert.equal(search.isOpen(), true);
    assert.equal(calls.includes("restore-docks"), false);
    key();
    assert.equal(search.isOpen(), false);
    assert.deepEqual(calls.slice(-3), ["query:", "restore-docks", "focus:prompt"]);
  });
});
