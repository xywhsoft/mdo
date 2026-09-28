import assert from "node:assert/strict";
import test from "node:test";

import { createQueueGate } from "../app/web/js/features/chat/queue-gate.js";

test("an older navigation load cannot release a newer queue gate", () => {
  const gate = createQueueGate();
  const finishOld = gate.beginLoad("default/session-a");
  const finishNew = gate.beginLoad("default/session-a");
  finishOld();
  assert.equal(gate.has("default/session-a"), true);
  finishNew();
  assert.equal(gate.has("default/session-a"), false);
});

test("manual review and navigation loads block independently", () => {
  const gate = createQueueGate();
  gate.block("default/session-a");
  const finish = gate.beginLoad("default/session-a");
  gate.unblock("default/session-a");
  assert.equal(gate.has("default/session-a"), true);
  finish();
  assert.equal(gate.has("default/session-a"), false);
  gate.block("default/session-a");
  const finishAnother = gate.beginLoad("default/session-b");
  finishAnother();
  assert.equal(gate.has("default/session-a"), true);
});
