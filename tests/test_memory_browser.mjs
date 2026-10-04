import assert from "node:assert/strict";
import test from "node:test";
import { createMemoryBrowser, memoryCollectionPath } from "../app/web/js/features/settings/memory-browser.js";

function fixture() {
  const calls = [];
  const browser = createMemoryBrowser({ read(path, { signal }) {
    return new Promise((resolve, reject) => calls.push({ path, signal, resolve, reject }));
  } });
  return { browser, calls };
}
const tick = async () => { await Promise.resolve(); await Promise.resolve(); };
const collection = (ids) => ({ data: { revision: 1, items: ids.map(id => ({ id, title: id })) } });
const entry = (id, content = id) => ({ data: { id, content } });

test("memory browsing is lazy, shows exact file content, and refreshes the selected file", async () => {
  const { browser, calls } = fixture();
  assert.equal(calls.length, 0);
  const load = browser.refresh();
  calls[0].resolve(collection(["rules", "build"])); await tick();
  assert.equal(calls[1].path, "/memory/global/rules");
  calls[1].resolve(entry("rules", "# Rules\n<script>plain text</script>\n")); await load;
  assert.equal(browser.get().entry.content, "# Rules\n<script>plain text</script>\n");
  const select = browser.select("build");
  assert.equal(calls[2].path, "/memory/global/build");
  calls[2].resolve(entry("build")); await select;
  const refresh = browser.refresh();
  calls[3].resolve(collection(["rules", "build"])); await tick();
  assert.equal(calls[4].path, "/memory/global/build");
  calls[4].resolve(entry("build", "updated")); await refresh;
  assert.equal(browser.get().entry.content, "updated");
});

test("late scope replies cannot replace the current project files", async () => {
  const { browser, calls } = fixture();
  const first = browser.refresh();
  const second = browser.refresh({ id: "alpha", name: "Alpha" }, "");
  assert.equal(calls[0].signal.aborted, true);
  calls[1].resolve(collection(["coding"])); await tick();
  calls[2].resolve(entry("coding", "project")); await second;
  calls[0].resolve(collection(["old"])); await first;
  assert.equal(browser.get().project.id, "alpha");
  assert.deepEqual(browser.get().collection.items.map(item => item.id), ["coding"]);
  assert.equal(browser.get().entry.content, "project");
  assert.equal(calls.length, 3);
});

test("rapid file selection ignores both late successes and late failures", async () => {
  const { browser, calls } = fixture();
  const load = browser.refresh(); calls[0].resolve(collection(["a", "b"])); await tick();
  const select = browser.select("b");
  calls[2].resolve(entry("b")); await select;
  calls[1].reject(new Error("stale failure")); await load;
  assert.equal(browser.get().selectedId, "b");
  assert.equal(browser.get().error, null);
  assert.equal(browser.get().busy, false);
});

test("leaving the memory page cancels reads and empty folders remain usable", async () => {
  const { browser, calls } = fixture();
  const load = browser.refresh(); browser.cancel();
  calls[0].resolve(collection(["late"])); await load;
  assert.equal(browser.get().collection, null);
  assert.equal(browser.get().busy, false);
  const retry = browser.refresh({ id: "empty" }, "");
  calls[1].resolve(collection([])); await retry;
  assert.equal(browser.get().selectedId, "");
  assert.equal(browser.get().entry, null);
  assert.equal(browser.get().error, null);
  assert.throws(() => memoryCollectionPath({ id: "../escape" }));
});
