// The production downloader uses actual HTTP/TLS and a real owned v2 backup.
import assert from "node:assert/strict";
import { writeFileSync } from "node:fs";
import { createHash } from "node:crypto";
import { downloadSessionBackup } from "../../app/web/js/api/backup-download.js";

const [base, session, output] = process.argv.slice(2);
const origin = new URL(base).origin, nativeFetch = globalThis.fetch;
globalThis.fetch = (path, options) => {
  const url = new URL(path, base);
  assert.equal(url.origin, origin); // This fixture never calls an external service.
  return nativeFetch(url, options);
};
const states = [];
const file = await downloadSessionBackup("default", session, { onProgress(state) { states.push(state); } });
const raw = Buffer.from(await file.blob.arrayBuffer());
assert.equal(raw.length, file.bytes);
assert.equal(createHash("sha256").update(raw).digest("hex"), file.sha256);
const document = JSON.parse(raw.toString("utf8"));
assert.equal(document.export_schema, 2); assert.equal(document.session_id, session);
assert.equal(document.scope, "retained-session-files");
assert.equal(document.queue_restore_policy, "require-user-confirmation");
assert.equal(document.restore_ready, false);
assert.ok(document.ui_records >= 6);
const files = new Map();
for (const file of document.files) {
  const bytes = Buffer.from(file.data, "base64");
  assert.equal(bytes.length, file.bytes);
  assert.equal(createHash("sha256").update(bytes).digest("hex"), file.sha256);
  files.set(file.path, bytes);
}
for (const name of ["meta.json", "snapshot.json", "ui-events.jsonl", "draft.json", "queue.json"])
  assert.ok(files.has(name), name);
assert.equal(files.get("artifacts/run-00000000000000000001/00000000000000000001-export.txt").length, 2097152);
const images = document.files.filter((entry) => /^attachments\/[a-f0-9]{32}\.bin$/.test(entry.path));
assert.equal(images.length, 1); assert.equal(images[0].bytes, 68);
const info = JSON.parse(files.get(images[0].path.replace(/\.bin$/, ".json")));
assert.equal(info.file_name, '截图 "1" 100%.png');
assert.equal(JSON.parse(files.get("draft.json")).text, "Export saved 草稿");
assert.equal(JSON.parse(files.get("queue.json")).items[0].state, "staged");
assert.equal(states[0].phase, "preparing"); assert.equal(states.at(-1).phase, "verifying");
assert.equal(states.at(-1).received, file.bytes);
writeFileSync(output, raw);
console.log("production frontend complete backup: HTTP/TLS bytes/hash, retained UI/model/images/name/draft/queue/artifact PASS");
