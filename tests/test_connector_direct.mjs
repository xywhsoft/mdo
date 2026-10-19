import assert from "node:assert/strict";
import { connectorDirectSocket } from "../app/web/js/api/connector.js";

const original = { fetch: globalThis.fetch, WebSocket: globalThis.WebSocket, location: globalThis.location };
const offer = { transport: "xrudp", offer: { version: 2, peer_id: "1".repeat(32), owner_id: "7", nonce: "2".repeat(32) } };
let opened = 0;
globalThis.location = { href: "http://127.0.0.1:5390/" };
globalThis.WebSocket = class { constructor() { ++opened; } };
try {
  const abort = new AbortController();
  globalThis.fetch = (path, options) => {
    assert.equal(path, "/api/v1/project-purge-intent"); assert.equal(options.signal, abort.signal);
    return new Promise((_, reject) => options.signal.addEventListener("abort", () => reject(options.signal.reason), { once: true }));
  };
  const pending = connectorDirectSocket(offer, { signal: abort.signal }); abort.abort();
  await assert.rejects(pending, error => error.name === "AbortError"); assert.equal(opened, 0);

  // A fetch implementation/body that completes concurrently with cancellation
  // must not admit a new native WebSocket after the attempt was retired.
  const late = new AbortController();
  globalThis.fetch = async () => {
    late.abort();
    return Response.json({ ok: true, data: {} }, { headers: { "X-Mdo-Write-Token": "a".repeat(32) + "-0" } });
  };
  await assert.rejects(connectorDirectSocket(offer, { signal: late.signal }), error => error.name === "AbortError");
  assert.equal(opened, 0);

  let fetches = 0, sent;
  globalThis.fetch = async () => {
    ++fetches;
    return Response.json({ ok: true }, { headers: { "X-Mdo-Write-Token": "a".repeat(32) + "-0" } });
  };
  globalThis.WebSocket = class {
    constructor() { ++opened; }
    send(value) { sent = JSON.parse(value); }
  };
  for (const version of [1, 3]) await assert.rejects(
    connectorDirectSocket({ ...offer, offer: { ...offer.offer, version } }), /Invalid direct offer/);
  assert.equal(fetches, 0); assert.equal(opened, 0);
  const active = await connectorDirectSocket(offer);
  active.onopen();
  assert.equal(opened, 1); assert.equal(fetches, 1); assert.deepEqual(sent, offer);
} finally {
  Object.assign(globalThis, original);
}
console.log("PASS direct connector admits v2 offers, rejects other versions and cancels late native admission");
