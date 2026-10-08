// Production import/controller over real loopback HTTP/TLS. Only the first
// apply response is deliberately lost after reaching the owned server.
import assert from "node:assert/strict";
import { readFileSync, writeFileSync } from "node:fs";
import { api } from "../../app/web/js/api/client.js";
import { createBackupImportController, createRestoreBookmark } from "../../app/web/js/features/sessions/backup-import-controller.js";

const [base, input, output, faultMode] = process.argv.slice(2);
const readFaults = faultMode === "--read-faults";
const faults = [];
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
  if (readFaults && lost && /\/session-backups\/restores\/[0-9a-f]{32}$/.test(url.pathname) &&
      (options.method || "GET") === "GET" && faults.length < 3) {
    await response.arrayBuffer();
    const fault = { kind: faults.length ? "503" : "hung-body", at: Date.now(),
      path: url.pathname, signal: options.signal };
    faults.push(fault);
    if (fault.kind === "hung-body") {
      // The real GET succeeded, but its delivery/body ignores abort. The
      // controller must release its own wait before another read can recover.
      return { status: 200, ok: true, json: () => new Promise(() => {}) };
    }
    return new Response(JSON.stringify({ error: { code: "temporary_unavailable", message: "owned read fault" } }),
      { status: 503, headers: { "Content-Type": "application/json" } });
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
const observations = [];
second.subscribe((state) => { phases.push(state.phase);
  observations.push({ phase: state.phase, busy: state.busy, error: state.error?.code ?? null }); });
await second.inspect(); assert.equal(second.get().phase, "committed", second.get().error?.stack);
const result = second.get().restore;
assert.equal(result.id, reviewed.id); assert.equal(result.source_sha256, reviewed.source_sha256);
assert.equal(result.project_id, "restore-target");
assert.equal(calls.filter((call) => call.path.endsWith("/apply")).length, 1);
assert.equal(calls.some((call) => /\/runs$/.test(call.path) && call.method === "POST"), false);
assert.equal(new URL(location.href).hash, "#/projects/default/new");
if (readFaults) {
  assert.deepEqual(faults.map(fault => fault.kind), ["hung-body", "503", "503"]);
  assert.equal(faults[0].signal.aborted, true);
  assert(faults[1].at - faults[0].at >= 10000, "body deadline did not bound recovery");
  assert(observations.every(state => !state.error), "transient read failure was published");
  assert(observations.some(state => state.phase === "resolving" && state.busy));
  assert.equal(observations.at(-1).busy, false);
}
writeFileSync(output, JSON.stringify({ result, reviewed, calls, phases, href: location.href,
  observations, faults: faults.map(({ signal, ...fault }) => ({ ...fault, aborted: signal.aborted })) }));
second.destroy();
console.log((readFaults ? "quiet hung-body/503 recovery: PASS; " : "") + "production import: bounded upload/SHA/preview/explicit target/lost apply/reload same ID/staged queue PASS");
