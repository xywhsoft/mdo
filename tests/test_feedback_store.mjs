import assert from "node:assert/strict";
import test, { beforeEach } from "node:test";

import { feedbackStore, selectFeedback, clearFeedback, setFeedback }
  from "../app/web/js/features/chat/feedback-store.js";

const response = (items = []) => Response.json({ ok: true, data: { items } });
const failed = () => Response.json({ ok: false,
  error: { code: "fixture_failure", message: "Temporary feedback failure" } }, { status: 503 });
function deferred() {
  let resolve;
  const promise = new Promise((done) => { resolve = done; });
  return { promise, resolve };
}
beforeEach(clearFeedback);

for (const staleFailure of [false, true]) {
  test(`a successful vote survives an earlier ${staleFailure ? "failed" : "empty"} feedback read`, async (context) => {
    const read = deferred();
    context.mock.method(globalThis, "fetch", async (_path, options) =>
      options.method === "GET" ? read.promise : response([{ event_id: 7, value: "good" }]));
    const selecting = selectFeedback("qa", "a");
    await setFeedback("qa", "a", 7, "good");
    assert.equal(feedbackStore.get().data.items.get(7), "good");
    read.resolve(staleFailure ? failed() : response());
    await selecting;
    assert.equal(feedbackStore.get().status, "ready");
    assert.equal(feedbackStore.get().error, null);
    assert.equal(feedbackStore.get().data.items.get(7), "good");
  });
}

test("a rejected vote still allows the pending read to restore saved feedback", async (context) => {
  const read = deferred();
  context.mock.method(globalThis, "fetch", async (_path, options) =>
    options.method === "GET" ? read.promise : failed());
  const selecting = selectFeedback("qa", "a");
  await assert.rejects(setFeedback("qa", "a", 7, "bad"), { code: "fixture_failure" });
  read.resolve(response([{ event_id: 7, value: "good" }]));
  await selecting;
  assert.equal(feedbackStore.get().data.items.get(7), "good");
});

test("a vote finishing after navigation does not replace the selected session's feedback", async (context) => {
  const write = deferred();
  context.mock.method(globalThis, "fetch", async (path, options) =>
    options.method === "PUT" ? write.promise :
      response(path.includes("/b/") ? [{ event_id: 9, value: "bad" }] : []));
  await selectFeedback("qa", "a");
  const voting = setFeedback("qa", "a", 7, "good");
  await selectFeedback("qa", "b");
  write.resolve(response([{ event_id: 7, value: "good" }]));
  await voting;
  assert.equal(feedbackStore.get().data.sessionId, "b");
  assert.deepEqual([...feedbackStore.get().data.items], [[9, "bad"]]);
});
