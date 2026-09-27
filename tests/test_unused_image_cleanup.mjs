import assert from "node:assert/strict";
import test from "node:test";

import { createUnusedImageCleanup } from
  "../app/web/js/features/chat/unused-image-cleanup.js";

const FIRST = "a".repeat(32);
const SECOND = "b".repeat(32);

function clock() {
  const pending = new Map();
  let nextId = 0;
  return {
    pending,
    setTimer(callback, delay) {
      const id = ++nextId;
      pending.set(id, { callback, delay });
      return id;
    },
    clearTimer(id) { pending.delete(id); },
    fire() {
      const [id, timer] = pending.entries().next().value ?? [];
      assert.ok(timer, "a retry should be scheduled");
      pending.delete(id);
      timer.callback();
      return timer.delay;
    },
  };
}

test("a removed queue image is deleted after its session run settles", async () => {
  const active = new Set(["project/session"]);
  const deleted = [];
  const cleanup = createUnusedImageCleanup({
    isRunActive: (key) => active.has(key),
    async deleteImage(project, session, id) { deleted.push([project, session, id]); },
  });
  cleanup.remember("project/session", [FIRST]);
  await cleanup.flush();
  assert.deepEqual(deleted, [], "an open runtime must keep its images");
  active.delete("project/session");
  await cleanup.flush();
  await cleanup.flush();
  assert.deepEqual(deleted, [["project", "session", FIRST]],
    "the image is discarded once after the run settles");
});

test("a transient in-use response leaves an image for a later retry", async () => {
  let attempts = 0;
  const cleanup = createUnusedImageCleanup({
    isRunActive: () => false,
    async deleteImage(_project, _session, id) {
      attempts += 1;
      assert.equal(id, SECOND);
      if (attempts === 1)
        throw Object.assign(new Error("still referenced"), {
          code: "attachment_in_use",
        });
    },
  });
  cleanup.remember("project/session", [SECOND]);
  await cleanup.flush();
  await cleanup.flush();
  assert.equal(attempts, 2);
  await cleanup.flush();
  assert.equal(attempts, 2, "successful cleanup leaves nothing to retry");
});

test("a transient storage error retries while the page remains open", async () => {
  const timer = clock();
  let attempts = 0;
  const cleanup = createUnusedImageCleanup({
    ...timer,
    isRunActive: () => false,
    async deleteImage() {
      if (++attempts <= 4)
        throw Object.assign(new Error("storage temporarily unavailable"), {
          code: "attachment_unavailable",
        });
    },
  });
  cleanup.remember("project/session", [FIRST]);
  await cleanup.flush();
  assert.equal(attempts, 1);
  assert.equal(timer.pending.size, 1);
  for (const delay of [15000, 30000, 60000, 60000]) {
    assert.equal(timer.fire(), delay);
    await cleanup.flush();
  }
  assert.equal(attempts, 5);
  assert.equal(timer.pending.size, 0, "success cancels the retry");
});

test("a missed run-state notification cannot strand a removed image", async () => {
  const timer = clock();
  let active = true;
  const deleted = [];
  const cleanup = createUnusedImageCleanup({
    ...timer,
    isRunActive: () => active,
    async deleteImage(_project, _session, id) { deleted.push(id); },
  });
  cleanup.remember("project/session", [FIRST]);
  assert.equal(timer.pending.size, 1);
  active = false;
  assert.equal(timer.fire(), 15000);
  await cleanup.flush();
  assert.deepEqual(deleted, [FIRST]);
  assert.equal(timer.pending.size, 0);
});

test("a lasting reference waits for a state change instead of polling", async () => {
  const timer = clock();
  let attempts = 0;
  const cleanup = createUnusedImageCleanup({
    ...timer,
    isRunActive: () => false,
    async deleteImage() {
      attempts += 1;
      throw Object.assign(new Error("referenced"), {
        code: "attachment_in_use",
      });
    },
  });
  cleanup.remember("project/session", [SECOND]);
  await cleanup.flush();
  assert.equal(timer.pending.size, 0);
  assert.equal(attempts, 1);
  await cleanup.flush();
  assert.equal(attempts, 2, "an explicit state refresh still retries");
});

test("an image added during deletion receives a later cleanup pass", async () => {
  const timer = clock();
  const deleted = [];
  let finishFirst;
  const first = new Promise((resolve) => { finishFirst = resolve; });
  const cleanup = createUnusedImageCleanup({
    ...timer,
    isRunActive: () => false,
    async deleteImage(_project, _session, id) {
      deleted.push(id);
      if (id === FIRST) await first;
    },
  });
  cleanup.remember("project/session", [FIRST]);
  cleanup.remember("project/session", [SECOND]);
  finishFirst();
  await cleanup.flush();
  assert.deepEqual(deleted, [FIRST]);
  assert.equal(timer.fire(), 15000);
  await cleanup.flush();
  assert.deepEqual(deleted, [FIRST, SECOND]);
  assert.equal(timer.pending.size, 0);
});
