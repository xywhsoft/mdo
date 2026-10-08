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

const quickRecovery = { now: () => 0, random: () => 0,
  setTimer: fn => setImmediate(fn), clearTimer: clearImmediate };
const networkFailure = () => Object.assign(new Error("lost acknowledgement"), { code: "network_error" });

test("a lost cut acknowledgement repeats one guarded edit and starts just one run", async () => {
  const requests = []; let saved = 0, accepted = 0;
  const probe = harness({ recoveryOptions: quickRecovery, editId: "e".repeat(32),
    preserveInput: async () => { ++saved; return true; },
    truncate: async (value, through, source, options) => {
      assert.equal(saved, 1, "input must be durable before the history cut");
      requests.push([value.etag, through, source, options.editId]);
      assert.ok(options.signal instanceof AbortSignal);
      if (requests.length === 1) throw networkFailure();
      return updated;
    },
    onAccepted() { ++accepted; },
  });
  await runMessageReplacement(probe.args);
  assert.deepEqual(requests, Array(2).fill(['"source-v4"', 2, 17, "e".repeat(32)]));
  assert.equal(accepted, 1);
  assert.equal(probe.events.filter(([kind]) => kind === "start").length, 1);
  assert.equal(probe.events.filter(([kind]) => kind === "visible-truncate").length, 1);
});

test("lost draft acknowledgements are read back before history changes", async () => {
  const phases = [];
  const probe = harness({ preserveInput: async () => { phases.push("PUT draft"); return false; },
    confirmInput: async ({ signal }) => { assert.ok(signal instanceof AbortSignal);
      phases.push("GET draft"); return true; },
    truncate: async () => { phases.push("cut"); return updated; },
  });
  await runMessageReplacement(probe.args);
  assert.deepEqual(phases, ["PUT draft", "GET draft", "cut"]);
});

test("an unsaved draft prevents any history cut and returns the input", async () => {
  const failures = [];
  const probe = harness({ preserveInput: async () => false, confirmInput: async () => false,
    onPreserveFailure: (error, visible) => failures.push([error.code, visible]),
  });
  await assert.rejects(runMessageReplacement(probe.args), { code: "session_edit_draft_unsaved" });
  assert.deepEqual(probe.events, []);
  assert.deepEqual(failures, [["session_edit_draft_unsaved", true]]);
});

test("exhausted edit confirmation leaves the saved input and one accurate final failure", async () => {
  let cuts = 0; const failures = [];
  const probe = harness({ recoveryOptions: quickRecovery,
    truncate: async () => { ++cuts; throw networkFailure(); },
    onPreserveFailure: (error, visible) => failures.push([error.code, visible]),
  });
  await assert.rejects(runMessageReplacement(probe.args), error =>
    error.code === "session_edit_unconfirmed" && error.cause.code === "network_error");
  assert.equal(cuts, 6); assert.deepEqual(probe.events, []);
  assert.deepEqual(failures, [["session_edit_unconfirmed", true]]);
});

test("changing routes while saving input cannot cut history", async () => {
  const save = deferred(), failures = [];
  const probe = harness({ preserveInput: () => save.promise,
    onPreserveFailure: (_error, visible) => failures.push(visible),
  });
  const action = runMessageReplacement(probe.args);
  await new Promise(resolve => setImmediate(resolve));
  probe.switchRoute(); save.resolve(true);
  await assert.rejects(action, /会话已切换/);
  assert.deepEqual(probe.events, []); assert.deepEqual(failures, [false]);
});

test("leaving the page after a confirmed cut cannot start a hidden model run", async () => {
  const page = new EventTarget();
  const probe = harness({ recoveryOptions: { eventTarget: page },
    onTruncated() { page.dispatchEvent(new Event("pagehide")); },
  });
  await assert.rejects(runMessageReplacement(probe.args), { name: "AbortError" });
  assert.deepEqual(probe.events.map(([kind]) => kind), ["truncate"]);
});

test("a failed model start is never replayed by edit recovery", async () => {
  let starts = 0;
  const probe = harness({ recoveryOptions: quickRecovery,
    startRun: async () => { ++starts; throw networkFailure(); },
  });
  await assert.rejects(runMessageReplacement(probe.args), { code: "network_error" });
  assert.equal(starts, 1);
  assert.deepEqual(probe.events.map(([kind]) => kind), ["truncate", "visible-truncate", "draft"]);
});
