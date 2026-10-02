import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import test from "node:test";
import { createSha256 } from "../app/web/js/utils/sha256.js";
import { downloadSessionBackup } from "../app/web/js/api/backup-download.js";

const bytes = (text) => new TextEncoder().encode(text);
const sha = (value) => createHash("sha256").update(value).digest("hex");
const body = bytes('{"export_schema":2,"test":"中文"}');
function response(input = body, headers = {}, chunks = [input], status = 200) {
  const stream = new ReadableStream({ start(controller) {
    for (const chunk of chunks) controller.enqueue(chunk);
    controller.close();
  } });
  return new Response(stream, { status, headers: {
    "Content-Type": "application/json; charset=utf-8", "Content-Length": String(input.length),
    ETag: `"mdo-backup-sha256-${sha(input)}"`,
    "Content-Disposition": 'attachment; filename="mdo-session-one.backup.json"', ...headers,
  } });
}
async function withFetch(fetcher, run) {
  const original = globalThis.fetch;
  globalThis.fetch = fetcher;
  try { return await run(); } finally { globalThis.fetch = original; }
}

test("incremental SHA-256 agrees with independent native hashing at padding and chunk boundaries", () => {
  assert.equal(createSha256().hex(), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  const abc = createSha256(); abc.update(bytes("abc"));
  assert.equal(abc.hex(), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  assert.equal(abc.hex(), abc.hex()); assert.throws(() => abc.update(bytes("x")));
  for (const size of [1, 55, 56, 63, 64, 65, 127, 129, 1048576]) {
    const input = Uint8Array.from({ length: size }, (_, i) => i % 251);
    for (const chunk of [1, 31, 64, 4093].filter((value) => size < 1000 || value > 1000)) {
      const digest = createSha256();
      for (let offset = 0; offset < size; offset += chunk) digest.update(input.subarray(offset, offset + chunk));
      assert.equal(digest.hex(), sha(input), `${size}/${chunk}`);
    }
  }
});

test("v2 downloads verify exact bytes/hash, report progress and use the pinned owner route", async () => {
  const phases = [], reply = response(body, {}, [body.subarray(0, 7), body.subarray(7)]);
  reply.blob = () => { throw new Error("unbounded blob() must not be used"); };
  const file = await withFetch(async (url, options) => {
    assert.equal(url, "/api/v1/projects/project/sessions/one/backup");
    assert.equal(options.redirect, "error"); assert.equal(options.credentials, "same-origin");
    return reply;
  }, () => downloadSessionBackup("project", "one", { onProgress(value) { phases.push(value); } }));
  assert.deepEqual(new Uint8Array(await file.blob.arrayBuffer()), body);
  assert.equal(file.sha256, sha(body)); assert.equal(file.bytes, body.length);
  assert.equal(file.filename, "mdo-session-one.backup.json");
  assert.deepEqual(phases.map((value) => value.phase), ["preparing", "receiving", "receiving", "receiving", "verifying"]);
  assert.equal(reply.body.locked, false);
});

test("invalid lengths/type/hash, short/overflow/empty chunks never create a file", async () => {
  const cases = [
    response(body, { "Content-Length": "0" }), response(body, { "Content-Length": "01" }),
    response(body, { "Content-Length": "100663297" }), response(body, { "Content-Type": "text/html" }),
    response(body, { ETag: "wrong" }), response(body, {}, [body], 206),
    response(body, {}, [body.subarray(1)]), response(body, {}, [body, bytes("x")]),
    response(body, {}, [new Uint8Array(), body]),
  ];
  for (const reply of cases) {
    await withFetch(async () => reply, () => assert.rejects(downloadSessionBackup("p", "s"),
      (error) => error.code === "backup_download_invalid"));
    assert.equal(reply.body.locked, false);
  }
  const corrupt = body.slice(); corrupt[5] ^= 1;
  await withFetch(async () => response(body, {}, [corrupt]), () =>
    assert.rejects(downloadSessionBackup("p", "s"), (error) => error.code === "backup_download_checksum"));
  await withFetch(async () => response(), () => assert.rejects(downloadSessionBackup("p", "s", { maxBytes: 4 }),
    (error) => error.code === "backup_download_invalid"));
});

test("server envelopes are bounded and preserve stable error codes for localized retry", async () => {
  const envelope = bytes('{"ok":false,"error":{"code":"session_capture_busy","message":"busy"}}');
  await withFetch(async () => response(envelope, {}, [envelope], 409), () =>
    assert.rejects(downloadSessionBackup("p", "s"), (error) => error.status === 409 && error.code === "session_capture_busy"));
  const oversized = new Uint8Array(16385);
  await withFetch(async () => response(oversized, {}, [oversized], 503), () =>
    assert.rejects(downloadSessionBackup("p", "s"), (error) => error.code === "backup_download_failed"));
});

test("pre-cancel, progress cancellation and deadline release readers without a download", async () => {
  const before = new AbortController(); before.abort();
  await withFetch(() => { throw new Error("pre-cancel must not fetch"); }, () =>
    assert.rejects(downloadSessionBackup("p", "s", { signal: before.signal }), { name: "AbortError" }));
  const cancel = new AbortController(), reply = response();
  await withFetch(async () => reply, () => assert.rejects(downloadSessionBackup("p", "s", {
    signal: cancel.signal, onProgress(state) { if (state.received) cancel.abort(); },
  }), { name: "AbortError" }));
  assert.equal(reply.body.locked, false);
  await withFetch((_url, options) => new Promise((_resolve, reject) => {
    options.signal.addEventListener("abort", () => reject(new DOMException("cancelled", "AbortError")));
  }), () => assert.rejects(downloadSessionBackup("p", "s", { timeoutMs: 10 }),
    (error) => error.code === "backup_download_timeout"));
});

test("large chunks yield to cancellation without relying on secure-context Web Crypto", async () => {
  const input = new Uint8Array(524288), cancel = new AbortController();
  await withFetch(async () => {
    setTimeout(() => cancel.abort(), 0);
    return response(input);
  }, () => assert.rejects(downloadSessionBackup("p", "s", { signal: cancel.signal }), { name: "AbortError" }));
});
