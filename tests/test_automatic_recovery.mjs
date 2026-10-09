import assert from "node:assert/strict";
import test from "node:test";
import { createAutomaticRecovery } from "../app/web/js/features/chat/automatic-recovery.js";

const pending = () => ({ project_id: "default", session_id: "one",
  automatic_resume: true, resume_required: true, recovery_token: "a",
  history_epoch: "a".repeat(64),
  items: [{ turn: 1, tool_available: true, automatic_retry_safe: true }] });
test("a safe interrupted read gets one automatic attempt, not a polling retry loop", () => {
  const auto = createAutomaticRecovery(), data = pending();
  assert.equal(auto.claim(data, true), true);
  assert.equal(auto.claim({ ...data, recovery_token: "b" }, true), false);
  assert.equal(auto.claim({ ...data, items: [{ ...data.items[0], turn: 2 }] }, true), true);
});
test("stops, mutations, unavailable tools and inactive views remain manual", () => {
  for (const patch of [{ automatic_resume: false }, { resume_required: false },
    { items: [] }, { items: [{ ...pending().items[0], automatic_retry_safe: false }] },
    { items: [{ ...pending().items[0], tool_available: false }] }])
    assert.equal(createAutomaticRecovery().claim({ ...pending(), ...patch }, true), false);
  const auto = createAutomaticRecovery();
  assert.equal(auto.claim(pending(), false), false);
  assert.equal(auto.claim(pending(), true), true);
});
test("turns are scoped to their project and session", () => {
  const auto = createAutomaticRecovery(), data = pending();
  assert.equal(auto.claim(data, true), true);
  assert.equal(auto.claim({ ...data, session_id: "two" }, true), true);
  assert.equal(auto.claim({ ...data, project_id: "another" }, true), true);
});

test("rewriting history admits a new interrupted turn with the same number", () => {
  const auto = createAutomaticRecovery(), data = pending();
  assert.equal(auto.claim(data, true), true);
  assert.equal(auto.claim({ ...data, history_epoch: "b".repeat(64) }, true), true);
  assert.equal(auto.claim({ ...data, history_epoch: "b".repeat(64), recovery_token: "c" }, true), false);
});

test("missing or unstable history identity never enables an automatic loop", () => {
  for (const epoch of [undefined, "", "a", "g".repeat(64), "a".repeat(65)])
    assert.equal(createAutomaticRecovery().claim({ ...pending(), history_epoch: epoch }, true), false);
  for (const turn of [0, -1, 1.5, Number.MAX_SAFE_INTEGER + 1])
    assert.equal(createAutomaticRecovery().claim({ ...pending(), items: [{ ...pending().items[0], turn }] }, true), false);
});

test("an appended result or changed inspection token keeps the same attempt limit", () => {
  const auto = createAutomaticRecovery(), data = pending();
  assert.equal(auto.claim(data, true), true);
  assert.equal(auto.claim({ ...data, revision: 17, last_sequence: 90, recovery_token: "later",
    items: [...data.items, { ...data.items[0], tool_call_id: "another-call" }] }, true), false);
});
