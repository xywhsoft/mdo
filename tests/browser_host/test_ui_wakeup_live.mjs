import { uiText } from './fixtures/i18n.mjs';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';
import test from 'node:test';

// Exercise the actual deployed live decoder. Replace its three imports only;
// inject the socket/timers, without another browser/application/source copy.
const path = new URL('../../app/web/js/api/live.js', import.meta.url);
let source = readFileSync(path, 'utf8').replace(/^import .+ from ["'](?:\.\.\/)+i18n\.js["'];\r?$/gm, '');
for (const line of [
  'import { targetLiveSocket } from "./target.js";',
  'import { api } from "./client.js";',
  'import { createRequestRecovery } from "./request-recovery.js";',
]) { assert.equal(source.split(line).length, 2); source = source.replace(line, ''); }
const end = source.indexOf('\nexport const liveConnection ='); assert.ok(end > 0);
source = source.slice(0, end).replace('export function createLiveConnection', 'function createLiveConnection');
const context = { uiText }; vm.runInNewContext(source + '\nglobalThis.factory = createLiveConnection;', context);

function fixture() {
  let next = 0; const sockets = [], timers = new Map(), events = [];
  const connection = context.factory({ url: () => 'ws://127.0.0.1/api/v1/live', random: () => 0,
    socket() { const value = { readyState: 1, sent: [], send(data) { this.sent.push(JSON.parse(data)); }, close() { this.readyState = 3; } }; sockets.push(value); return value; },
    setTimer(callback, delay) { timers.set(++next, { callback, delay }); return next; }, clearTimer(id) { timers.delete(id); },
  });
  const unsubscribe = connection.subscribe(event => events.push(event));
  const deliver = (socket, event) => socket.onmessage({ data: JSON.stringify(event) });
  connection.start('a'.repeat(32) + '-1');
  return { connection, sockets, timers, events, deliver, unsubscribe };
}

test('UI hints are admitted only after this live connection completes its ready handshake', () => {
  const f = fixture(), socket = f.sockets[0];
  f.deliver(socket, { type: 'ui_pending' }); assert.equal(f.events.length, 0);
  f.deliver(socket, { type: 'ready', version: 1 }); assert.ok(f.connection.isConnected());
  f.deliver(socket, { type: 'ui_pending' });
  assert.equal(f.events.filter(event => event.type === 'ui_pending').length, 1);
  assert.ok(socket.sent.every(event => event.type === 'subscribe'));
  f.unsubscribe(); f.deliver(socket, { type: 'ui_pending' });
  assert.equal(f.events.filter(event => event.type === 'ui_pending').length, 1);
  f.connection.stop(); assert.equal(f.timers.size, 0);
});

test('hints from retired sockets, incomplete reconnects and stopped owners cannot wake the consumer', () => {
  const f = fixture(), old = f.sockets[0]; f.deliver(old, { type: 'ready', version: 1 });
  f.connection.recheck(); f.deliver(old, { type: 'ui_pending' });
  assert.equal(f.events.filter(event => event.type === 'ui_pending').length, 0);
  const retry = [...f.timers.entries()].find(([, row]) => row.delay === 500); assert.ok(retry);
  f.timers.delete(retry[0]); retry[1].callback(); const current = f.sockets[1]; assert.ok(current);
  f.deliver(current, { type: 'ui_pending' }); assert.equal(f.events.filter(event => event.type === 'ui_pending').length, 0);
  f.deliver(current, { type: 'ready', version: 1 }); f.deliver(current, { type: 'ui_pending' });
  assert.equal(f.events.filter(event => event.type === 'ui_pending').length, 1);
  f.connection.stop(); f.deliver(current, { type: 'ui_pending' }); f.deliver(old, { type: 'ui_pending' });
  assert.equal(f.events.filter(event => event.type === 'ui_pending').length, 1); assert.equal(f.timers.size, 0);
});
