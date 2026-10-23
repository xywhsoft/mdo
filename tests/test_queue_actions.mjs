import assert from "node:assert/strict";
import test from "node:test";
import { queueMoveAnchor } from "../app/web/js/features/chat/queue-reorder.js";
import { createPromptQueueTransport, queueResponse } from "../app/web/js/features/chat/prompt-queue-transport.js";
import { createRequestRecovery } from "../app/web/js/api/request-recovery.js";

test("dragging uses stable before anchors in both directions and at the tail", () => {
  const ids = ["a", "b", "c", "d"];
  assert.equal(queueMoveAnchor(ids, "c", "a"), "a");
  assert.equal(queueMoveAnchor(ids, "a", "c", true), "d");
  assert.equal(queueMoveAnchor(ids, "a", "d", true), "");
  assert.equal(queueMoveAnchor(ids, "a", "a"), undefined);
  assert.equal(queueMoveAnchor(ids, "missing", "b"), undefined);
  assert.equal(queueMoveAnchor(ids, "a", "missing"), undefined);
  assert.equal(queueMoveAnchor(ids, "b", "a", true), undefined);
  assert.equal(queueMoveAnchor(ids, "a", "b"), undefined);
});

test("lost action ACK is confirmed by its exact operation receipt without another POST", async context => {
  const action = { id: "e".repeat(32), kind: "move", item: "a".repeat(32),
    before: "", revision: 4, draft_revision: 0 };
  const calls = [];
  context.mock.method(globalThis, "fetch", async (url, options) => {
    calls.push([url, options.method]);
    if (options.method === "POST") throw new TypeError("lost acknowledgement");
    return Response.json({ ok: true, data: { items: [], revision: 5, managed: true, actions: [action] } });
  });
  const transport = createPromptQueueTransport({ stamp: () => "fixture",
    createRecovery: () => createRequestRecovery({ eventTarget: new EventTarget() }) });
  const reply = await transport.action("default/session", action);
  assert.equal(reply.data.revision, 5);
  assert.deepEqual(calls.map(call => call[1]), ["POST", "GET"]);
});

test("a different payload with the same operation ID never confirms a queue write", async context => {
  const action = { id: "e".repeat(32), kind: "withdraw", item: "a".repeat(32),
    before: "", revision: 4, draft_revision: 2 };
  let writes = 0;
  context.mock.method(globalThis, "fetch", async (_url, options) => {
    if (options.method === "POST") { writes++; throw new TypeError("lost"); }
    return Response.json({ ok: true, data: { items: [], revision: 5, managed: true,
      actions: [{ ...action, draft_revision: 3 }] } });
  });
  const transport = createPromptQueueTransport({ stamp: () => "fixture",
    createRecovery: () => createRequestRecovery({ eventTarget: new EventTarget() }) });
  await assert.rejects(transport.action("default/session", action), error =>
    error.queueAdmissionUncertain && error.confirmationError.code === "queue_action_conflict");
  assert.equal(writes, 1);
});

test("editing rows remain visible and malformed queue revisions are rejected", () => {
  const response = { data: { items: [{ id: "a".repeat(32), text: "saved",
    attachments: [], priority: false, state: "editing" }], revision: 9, actions: [] } };
  assert.equal(queueResponse(response), response);
  assert.throws(() => queueResponse({ data: { ...response.data, revision: -1 } }), /metadata/);
  assert.throws(() => queueResponse({ data: { ...response.data, managed: "yes" } }), /metadata/);
});
