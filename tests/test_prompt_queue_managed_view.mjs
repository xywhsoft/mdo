import assert from "node:assert/strict";
import test from "node:test";
import { TestElement, testDocument } from "./helpers/extension-dom.mjs";
import { createPromptQueue } from "../app/web/js/features/chat/prompt-queue.js";

test("native worker progress is not an uncertain write; paused or unmanaged dispatch still requires review", async context => {
  globalThis.document = testDocument();
  globalThis.window = Object.assign(new EventTarget(), { setTimeout, clearTimeout });
  const container = new TestElement("div");
  container.ownerDocument = document;
  document.body.append(container);
  let data, revision = 0;
  context.mock.method(globalThis, "fetch", async (_url, options) => {
    assert.equal(options.method, "GET", "rendering progress must never repeat a submission");
    return Response.json({ ok: true, data });
  });
  const queue = createPromptQueue({ container,
    navigation: { get: () => ({ projectId: "default", sessionId: "one" }), subscribe() {} },
    isRunActive: () => false });
  async function show(flags, managed = true, paused = false) {
    data = { items: [{ id: "a".repeat(32), text: "Local fixture", attachments: [], priority: false, ...flags }],
      managed, paused, error: "", revision: ++revision, actions: [] };
    await queue.select("default", "one");
    return container.querySelectorAll(".prompt-queue-warning");
  }
  for (const flags of [{ state: "pending" }, { state: "sending" },
    { state: "sending", start_claimed: true },
    { state: "sending", start_claimed: true, run_id: "run-fixture" }])
    assert.equal((await show(flags)).length, 0);
  assert((await show({ state: "sending" }, true, true)).length > 0);
  assert((await show({ state: "sending" }, false)).length > 0);
});
