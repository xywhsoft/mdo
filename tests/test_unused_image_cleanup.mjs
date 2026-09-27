import assert from "node:assert/strict";
import test from "node:test";

import { createUnusedImageCleanup } from
  "../app/web/js/features/chat/unused-image-cleanup.js";

const FIRST = "a".repeat(32);
const SECOND = "b".repeat(32);

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
