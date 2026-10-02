import assert from "node:assert/strict";
import test from "node:test";
import { createSettingsAutosave, changedSettingsValues } from
  "../app/web/js/features/settings/settings-autosave.js";

function fixture() {
  const pending = new Map();
  const writes = [];
  const states = [];
  let timerId = 0;
  let local = "";
  let saved = "";
  let valid = true;
  const controller = createSettingsAutosave({
    timers: {
      setTimeout(fn) { const id = ++timerId; pending.set(id, fn); return id; },
      clearTimeout(id) { pending.delete(id); },
    },
    capture: () => valid && local !== saved ? { value: local } : null,
    write: (submitted) => new Promise((resolve, reject) => {
      writes.push({ value: submitted.value,
        resolve() { saved = submitted.value; resolve(); }, reject });
    }),
    onStart: () => states.push("saving"),
    onSettled: (error) => states.push(error ? "failed" : "saved"),
  });
  return { controller, writes, states, pending,
    edit(value) { local = value; controller.schedule(); },
    validity(value) { valid = value; },
    runTimer() {
      const work = [...pending.values()];
      pending.clear();
      for (const fn of work) fn();
    },
  };
}
const tick = () => new Promise(setImmediate);

test("quick edits coalesce; invalid and discarded input make no requests", async () => {
  const ctx = fixture();
  ctx.edit("light"); ctx.edit("dark");
  assert.equal(ctx.pending.size, 1);
  ctx.runTimer(); assert.deepEqual(ctx.writes.map((item) => item.value), ["dark"]);
  ctx.writes[0].resolve(); await tick();
  assert.equal(ctx.pending.size, 0);
  ctx.validity(false); ctx.edit("invalid"); ctx.runTimer();
  assert.equal(ctx.writes.length, 1);
  ctx.validity(true); ctx.edit("discarded"); ctx.controller.cancel(); ctx.runTimer();
  assert.equal(ctx.writes.length, 1);
});

test("edits during a pending save stay editable and save after it with their latest value", async () => {
  const ctx = fixture();
  ctx.edit("first"); ctx.runTimer();
  ctx.edit("second"); ctx.edit("third"); ctx.runTimer();
  assert.equal(ctx.writes.length, 1);
  ctx.writes[0].resolve(); await tick(); ctx.runTimer();
  assert.deepEqual(ctx.writes.map((item) => item.value), ["first", "third"]);
  ctx.writes[1].resolve(); await tick();
  assert.deepEqual(ctx.states, ["saving", "saved", "saving", "saved"]);
});

test("a failure keeps edits and stops automatic retries; Save retries once", async () => {
  const ctx = fixture();
  ctx.edit("first"); ctx.runTimer(); ctx.edit("latest");
  ctx.writes[0].reject(new Error("offline")); await tick();
  assert.equal(ctx.pending.size, 0);
  assert.deepEqual(ctx.states, ["saving", "failed"]);
  const retry = ctx.controller.flush();
  assert.equal(ctx.writes[1].value, "latest");
  assert.equal(await ctx.controller.flush(), false);
  ctx.writes[1].resolve(); assert.equal(await retry, true);
});

test("composition pauses pending saves and resumes only when the text is complete", async () => {
  const ctx = fixture();
  ctx.edit("n"); ctx.controller.composing(true); ctx.runTimer();
  ctx.edit("ni"); ctx.runTimer();
  assert.equal(await ctx.controller.flush(), false);
  assert.equal(ctx.writes.length, 0);
  ctx.edit("你好"); ctx.controller.composing(false); ctx.runTimer();
  assert.equal(ctx.writes[0].value, "你好");
  ctx.writes[0].resolve(); await tick();
});

test("teardown cancels delayed writes and does not act on a later response", async () => {
  const idle = fixture(); idle.edit("pending"); idle.controller.destroy(); idle.runTimer();
  assert.equal(idle.writes.length, 0);
  const busy = fixture(); busy.edit("sent"); busy.runTimer();
  busy.edit("new"); busy.controller.destroy(); busy.writes[0].resolve(); await tick();
  assert.equal(busy.pending.size, 0);
  assert.deepEqual(busy.states, ["saving"]);
});

test("a server echo rebases submitted fields while retaining newer text, select and switch values", () => {
  const submitted = { theme: "dark", size: "normal", text: "sent", enabled: false };
  const current = { theme: "dark", size: "large", text: "new input", enabled: true };
  assert.deepEqual(changedSettingsValues(submitted, current), {
    size: "large", text: "new input", enabled: true,
  });
  assert.deepEqual(changedSettingsValues(current, current), {});
});
