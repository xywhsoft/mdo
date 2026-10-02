import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import test from "node:test";
import { uploadBackupFile } from "../app/web/js/api/backup-import.js";
import { api, hasPendingApiWrites, setApiWriteGuard, uploadBackupChunk } from "../app/web/js/api/client.js";

test("plain HTTP-compatible incremental hash/upload uses bounded binary chunks and shared write admission", async () => {
  const previous = globalThis.fetch, calls = [], chunks = [];
  const raw = Buffer.alloc(524391, 97), checksum = createHash("sha256").update(raw).digest("hex");
  const token = "a".repeat(32) + "-0";
  let identity;
  globalThis.fetch = async (path, options = {}) => {
    calls.push({ path, options });
    let data = {};
    if (path.endsWith("/uploads")) data = JSON.parse(options.body);
    else if (path.includes("/chunks/")) {
      assert.equal(hasPendingApiWrites(), true);
      assert.equal(options.headers["X-Mdo-Write-Token"], token);
      assert.ok(options.body instanceof Uint8Array);
      assert.ok(options.body.byteLength <= 262144);
      chunks.push(Buffer.from(options.body));
      data = { received_bytes: chunks.reduce((sum, item) => sum + item.length, 0) };
    } else if (path.endsWith("/seal")) data = { sha256: checksum, bytes: raw.length };
    return new Response(JSON.stringify({ ok: true, data }), { headers: { "X-Mdo-Write-Token": token } });
  };
  try {
    await api.get("/project-purge-intent");
    const progress = [], file = new Blob([raw]);
    const result = await uploadBackupFile(file, { onIdentity(value) { identity = value; },
      onProgress(value) { progress.push(value); } });
    assert.equal(result.sha256, checksum); assert.equal(result.id, identity.id);
    assert.deepEqual(Buffer.concat(chunks), raw);
    assert.deepEqual(progress.map((item) => item.phase), ["hashing", "hashing", "hashing", "uploading", "uploading", "uploading"]);
    const clear = setApiWriteGuard(() => false), before = calls.length;
    try { await assert.rejects(uploadBackupChunk(result.id, 0, new Uint8Array([1])), { code: "purge_review_required" }); }
    finally { clear(); }
    assert.equal(calls.length, before); assert.equal(hasPendingApiWrites(), false);
  } finally { globalThis.fetch = previous; }
});

test("file size, late hash cancellation and corrupt seal cannot silently return a verified upload", async () => {
  const previous = globalThis.fetch;
  await assert.rejects(uploadBackupFile({ size: 100663297 }), { code: "backup_upload_limit" });
  const controller = new AbortController();
  const file = { size: 1, slice() { return { async arrayBuffer() { controller.abort(); return new Uint8Array([1]).buffer; } }; } };
  let calls = 0;
  globalThis.fetch = async () => { calls++;
    return new Response(JSON.stringify({ ok: true, data: { received_bytes: 1, sha256: "wrong", bytes: 1 } })); };
  try {
    await assert.rejects(uploadBackupFile(file, { signal: controller.signal }), { name: "AbortError" });
    assert.equal(calls, 0);
    await assert.rejects(uploadBackupFile(new Blob([new Uint8Array([1])])), { code: "backup_upload_checksum" });
    assert.equal(calls, 3); assert.equal(hasPendingApiWrites(), false);
  } finally { globalThis.fetch = previous; }
});
