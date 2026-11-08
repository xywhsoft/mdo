import assert from 'node:assert/strict';
import test from 'node:test';
import { createUiPollPump } from '../../app/web/js/features/shell/ui-poll-pump.js';

const flush = () => new Promise(resolve => setImmediate(resolve));
function clock() {
  let next = 0; const pending = new Map(), callbacks = new Map();
  return { pending, callbacks,
    setTimer(callback, delay) { const id = ++next; pending.set(id, { callback, delay }); callbacks.set(id, callback); return id; },
    clearTimer(id) { pending.delete(id); },
    fire(id) { const entry = pending.get(id); pending.delete(id); entry.callback(); },
  };
}

test('an authenticated hint bypasses the ten-second hidden fallback without changing its idle delay', async () => {
  const timers = clock(); let calls = 0;
  const pump = createUiPollPump({ poll: () => { calls++; }, delay: () => 10000, ...timers });
  pump.wake(); await flush(); assert.equal(calls, 1);
  assert.equal([...timers.pending.values()][0].delay, 10000);
  pump.wake(); assert.equal(calls, 2); await flush();
  assert.equal(timers.pending.size, 1); pump.stop(); assert.equal(timers.pending.size, 0);
});

test('a burst during an in-flight drain coalesces into one serialized reread', async () => {
  const timers = clock(); let calls = 0, active = 0, maximum = 0, finish;
  const pump = createUiPollPump({ delay: () => 1500, ...timers, async poll() {
    calls++; active++; maximum = Math.max(maximum, active);
    if (calls === 1) await new Promise(resolve => { finish = resolve; });
    active--;
  } });
  pump.wake(); for (let i = 0; i < 100; i++) pump.wake();
  assert.equal(calls, 1); assert.equal(timers.pending.size, 0);
  finish(); await flush(); assert.equal(calls, 2); assert.equal(maximum, 1);
  assert.equal(timers.pending.size, 1); pump.stop();
});

test('a cancelled timer arriving late cannot create another idle polling chain', async () => {
  const timers = clock(); let calls = 0;
  const pump = createUiPollPump({ poll: () => { calls++; }, delay: () => 1500, ...timers });
  pump.wake(); await flush(); const old = [...timers.pending.keys()][0];
  pump.wake(); await flush(); assert.equal(calls, 2);
  const current = [...timers.pending.keys()][0];
  timers.callbacks.get(old)(); await flush();
  assert.equal(calls, 2); assert.equal(timers.pending.size, 1); assert.ok(timers.pending.has(current));
  timers.fire(current); await flush(); assert.equal(calls, 3); pump.stop();
});

test('dispose during a drain suppresses queued hints and late completion', async () => {
  const timers = clock(); let calls = 0, finish;
  const pump = createUiPollPump({ poll: () => { calls++; return new Promise(resolve => { finish = resolve; }); }, delay: () => 1500, ...timers });
  pump.wake(); pump.wake(); pump.stop(); pump.wake(); finish(); await flush();
  assert.equal(calls, 1); assert.equal(timers.pending.size, 0);
});

test('a failed drain keeps the fallback alive without a busy retry loop', async () => {
  const timers = clock(); let calls = 0, errors = 0;
  const pump = createUiPollPump({ poll: () => { calls++; throw Error('read failed'); }, onError: () => { errors++; }, delay: () => 1500, ...timers });
  pump.wake(); await flush(); assert.equal(calls, 1); assert.equal(errors, 1);
  assert.equal(timers.pending.size, 1); assert.equal([...timers.pending.values()][0].delay, 1500);
  timers.fire([...timers.pending.keys()][0]); await flush(); assert.equal(calls, 2); pump.stop();
});
