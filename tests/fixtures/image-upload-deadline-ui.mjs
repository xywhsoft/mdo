// Production image recovery and API adapter with one held PUT body. Every
// request reaches the real owned service; no second upload or model starts.
import assert from "node:assert/strict";
import { readFileSync, writeFileSync } from "node:fs";
import { api, hasPendingApiWrites, setApiNetworkErrorHandler } from "../../app/web/js/api/client.js";
import { uploadImageWithRecovery } from "../../app/web/js/api/image-upload.js";

const [base, sessionId, input, output] = process.argv.slice(2);
const origin = new URL(base).origin, fetchNative = globalThis.fetch, calls = [];
const uploadId = "d".repeat(32);
let heldSignal, heldAt, confirmedAt, complete, notices = 0;
setApiNetworkErrorHandler(() => notices++);
globalThis.fetch = async (path, options = {}) => {
  const url = new URL(path, base); assert.equal(url.origin, origin);
  calls.push({ path: url.pathname, method: options.method || "GET" });
  const response = await fetchNative(url, options);
  if (url.pathname.endsWith(`/attachments/${uploadId}`) && options.method === "PUT") {
    assert.equal(heldSignal, undefined, "same image was uploaded twice");
    assert.equal(response.status, 201);
    await response.arrayBuffer(); heldSignal = options.signal; heldAt = Date.now();
    return { ok: true, status: 201, headers: response.headers,
      json: () => new Promise(resolve => { complete = resolve; }) };
  }
  if (url.pathname.endsWith(`/attachments/${uploadId}/info`)) {
    confirmedAt = Date.now();
    assert.equal(heldSignal.aborted, true);
    assert.equal(hasPendingApiWrites(), false, "the old write still blocks the editor");
  }
  return response;
};
await api.get("/project-purge-intent");
const file = new Blob([readFileSync(input)], { type: "image/png" }); file.name = "pixel.png";
const result = await uploadImageWithRecovery("default", sessionId, file, file.type, { id: uploadId });
assert.equal(result.id, uploadId); assert.equal(result.size, file.size); assert.equal(result.file_name, file.name);
assert.equal(hasPendingApiWrites(), false); assert.equal(notices, 0);
assert(confirmedAt - heldAt >= 30000 && confirmedAt - heldAt < 90000);
assert.equal(calls.filter(call => call.method === "PUT").length, 1);
assert.equal(calls.filter(call => call.path.endsWith("/info")).length, 1);
complete({ ok: false, error: { code: "write_token_conflict", message: "late ignored reply" } });
await new Promise(resolve => setImmediate(resolve));
assert.equal(hasPendingApiWrites(), false); assert.equal(notices, 0);
writeFileSync(output, JSON.stringify({ result, calls, held_body_ms: confirmedAt - heldAt,
  pending_writes: hasPendingApiWrites(), network_notices: notices }));
setApiNetworkErrorHandler(null);
console.log("production image upload: held body/deadline/same-ID confirmation/write release PASS");
