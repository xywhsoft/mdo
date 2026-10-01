import assert from "node:assert/strict";
import test from "node:test";

import { runMessageReplacement } from
  "../app/web/js/features/chat/message-replacement.js";

const session = Object.freeze({ project_id: "source", id: "old", revision: 4 });
const updated = Object.freeze({ project_id: "source", id: "old", revision: 5 });
const attachments = Object.freeze(["a".repeat(32)]);

function deferred() {
  let resolve;
  let reject;
  const promise = new Promise((done, fail) => { resolve = done; reject = fail; });
  return { promise, resolve, reject };
}

function harness(overrides = {}) {
  const events = [];
  let current = true;
  return {
    events,
    switchRoute() { current = false; },
    args: {
      session, sequence: 3, sourceEventId: 17, text: " edited ", attachments,
      isCurrent: () => current,
      loadHistory: async () => ({ last_sequence: 6, etag: '"source-v4"',
        revision: 4 }),
      truncate: async (value, through, sourceEventId) => {
        events.push(["truncate", value, through, sourceEventId]);
        return updated;
      },
      startRun: async (...args) => {
        events.push(["start", ...args]);
        return { id: "run-old" };
      },
      onTruncated: (value) => events.push(["visible-truncate", value]),
      onStartFailure: (value, error, visible) =>
        events.push(["draft", value, error.message, visible]),
      onStarted: (run) => events.push(["visible-run", run.id]),
      ...overrides,
    },
  };
}

test("message edit stays bound to its original session", async () => {
  const probe = harness();
  const result = await runMessageReplacement(probe.args);
  assert.equal(result.current, true);
  assert.deepEqual(probe.events.map(([kind]) => kind),
    ["truncate", "visible-truncate", "start", "visible-run"]);
  assert.equal(probe.events[0][1].id, "old");
  assert.equal(probe.events[0][2], 2);
  assert.equal(probe.events[0][3], 17);
  assert.deepEqual(probe.events[2].slice(1), ["source", "old", "edited", attachments]);
});

test("profile validation rejects image retry before changing history", async () => {
  const probe = harness({ validateBeforeTruncate() {
    throw new Error("image model unsupported");
  } });
  await assert.rejects(runMessageReplacement(probe.args),
    /image model unsupported/);
  assert.deepEqual(probe.events, []);
});

test("an unsettled submission found during preflight cannot truncate history", async () => {
  const check = deferred();
  const probe = harness({ validateBeforeTruncate: () => check.promise });
  const action = runMessageReplacement(probe.args);
  await new Promise((resolve) => setImmediate(resolve));
  assert.deepEqual(probe.events, []);
  check.reject(new Error("pending submission"));
  await assert.rejects(action, /pending submission/);
  assert.deepEqual(probe.events, []);
});

test("navigation during asynchronous preflight cannot truncate the old session", async () => {
  const check = deferred();
  const probe = harness({ validateBeforeTruncate: () => check.promise });
  const action = runMessageReplacement(probe.args);
  await new Promise((resolve) => setImmediate(resolve));
  probe.switchRoute();
  check.resolve();
  await assert.rejects(action, /会话已切换/);
  assert.deepEqual(probe.events, []);
});

test("navigation before truncation prevents changing the new session", async () => {
  const history = deferred();
  const probe = harness({ loadHistory: () => history.promise });
  const action = runMessageReplacement(probe.args);
  probe.switchRoute();
  history.resolve({ last_sequence: 6, etag: '"source-v4"', revision: 4 });
  await assert.rejects(action, /会话已切换/);
  assert.deepEqual(probe.events, []);
});

test("navigation during truncation finishes the old run without changing new UI", async () => {
  const truncate = deferred();
  const probe = harness({ truncate: (...args) => {
    probe.events.push(["truncate", ...args]);
    return truncate.promise;
  } });
  const action = runMessageReplacement(probe.args);
  await new Promise((resolve) => setImmediate(resolve));
  probe.switchRoute();
  truncate.resolve(updated);
  const result = await action;
  assert.equal(result.current, false);
  assert.deepEqual(probe.events.map(([kind]) => kind), ["truncate", "start"]);
  assert.deepEqual(probe.events[1].slice(1), ["source", "old", "edited", attachments]);
});

test("a refused history transaction never announces a visible commit", async () => {
  const probe = harness({ truncate: async () => { throw new Error("history conflict"); } });
  await assert.rejects(runMessageReplacement(probe.args), /history conflict/);
  assert.deepEqual(probe.events, []);
});

test("an obsolete message boundary cannot commit or reveal a replacement turn", async () => {
  const probe = harness({ loadHistory: async () => ({ last_sequence: 2 }) });
  await assert.rejects(runMessageReplacement(probe.args), /消息已不在/);
  assert.deepEqual(probe.events, []);
});

test("missing original event identity cannot load or mutate history", async () => {
  for (const sourceEventId of [undefined, 0, -1, NaN, 1.5]) {
    const probe = harness({ sourceEventId, loadHistory() {
      throw new Error("history must not be read");
    } });
    await assert.rejects(runMessageReplacement(probe.args), /消息已不在/);
    assert.deepEqual(probe.events, []);
  }
});

test("a reused sequence with a fresh revision still submits the original event guard", async () => {
  const probe = harness({
    loadHistory: async () => ({ last_sequence: 6, etag: '"source-v8"', revision: 8 }),
    truncate: async (value, through, sourceEventId) => {
      assert.equal(value.revision, 8);
      assert.equal(through, 2);
      assert.equal(sourceEventId, 17);
      throw Object.assign(new Error("original message replaced"), { code: "session_message_changed" });
    },
  });
  await assert.rejects(runMessageReplacement(probe.args), /original message replaced/);
  assert.deepEqual(probe.events, [], "a refused source guard must not announce a commit or start a run");
});

test("failed background resend saves the old draft without touching visible UI", async () => {
  const start = deferred();
  const probe = harness({ startRun: (...args) => {
    probe.events.push(["start", ...args]);
    return start.promise;
  } });
  const action = runMessageReplacement(probe.args);
  await new Promise((resolve) => setImmediate(resolve));
  probe.switchRoute();
  start.reject(new Error("offline"));
  await assert.rejects(action, /offline/);
  assert.deepEqual(probe.events.map(([kind]) => kind),
    ["truncate", "visible-truncate", "start", "draft"]);
  assert.deepEqual(probe.events[3].slice(1), [updated, "offline", false]);
});
