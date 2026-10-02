// Production import/controller over real loopback HTTP/TLS. Only the first
// apply response is deliberately lost after reaching the owned server.
import assert from "node:assert/strict";
import { readFileSync, writeFileSync } from "node:fs";
import { api } from "../../app/web/js/api/client.js";
import { createBackupImportController, createRestoreBookmark } from "../../app/web/js/features/sessions/backup-import-controller.js";

const [base, input, output] = process.argv.slice(2);
const origin = new URL(base).origin, nativeFetch = globalThis.fetch;
let lost = false;
const calls = [];
globalThis.fetch = async (path, options = {}) => {
  const url = new URL(path, base); assert.equal(url.origin, origin);
  calls.push({ path: url.pathname, method: options.method || "GET" });
  const response = await nativeFetch(url, options);
  if (url.pathname.endsWith("/apply") && !lost) {
    lost = true; await response.arrayBuffer(); throw new Error("deliberately lost response");
  }
  return response;
};
await api.get("/project-purge-intent");
const location = { href: base + "?keep=1#/projects/default/new" };
const history = { state: { localDraft: "retained" }, replaceState(state, title, url) {
  assert.equal(state.localDraft, "retained"); location.href = new URL(url, location.href).href;
} };
const bookmark = createRestoreBookmark({ location, history });
const phases = [], first = createBackupImportController({ bookmark, pollMs: 20 });
first.subscribe((state) => phases.push(state.phase));
const file = new Blob([readFileSync(input)]); file.name = "owned-complete.json";
await first.choose(file);
assert.equal(first.get().phase, "preview", first.get().error?.stack);
assert.equal(calls.some((call) => call.path.endsWith("/apply")), false);
await first.review("restore-target"); assert.equal(first.get().phase, "review", first.get().error?.stack);
const reviewed = first.get().restore;
await first.confirm();
assert.equal(first.get().phase, "unknown"); assert.equal(first.get().error.code, "network_error");
assert.equal(bookmark.read().id, reviewed.id);
first.destroy();
const second = createBackupImportController({ bookmark, pollMs: 20 });
second.subscribe((state) => phases.push(state.phase));
await second.inspect(); assert.equal(second.get().phase, "committed", second.get().error?.stack);
const result = second.get().restore;
assert.equal(result.id, reviewed.id); assert.equal(result.source_sha256, reviewed.source_sha256);
assert.equal(result.project_id, "restore-target");
assert.equal(calls.filter((call) => call.path.endsWith("/apply")).length, 1);
assert.equal(calls.some((call) => /\/runs$/.test(call.path) && call.method === "POST"), false);
assert.equal(new URL(location.href).hash, "#/projects/default/new");
writeFileSync(output, JSON.stringify({ result, reviewed, calls, phases, href: location.href }));
second.destroy();
console.log("production import: bounded upload/SHA/preview/explicit target/lost apply/reload same ID/staged queue PASS");
