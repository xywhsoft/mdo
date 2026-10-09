// The production writer against a disposable native API, not browser deletion.
import assert from "node:assert/strict";
import { readFile, writeFile } from "node:fs/promises";
import { createScheduleWriter } from "../../../app/web/js/state/schedule-write.js";

const [directory, output] = process.argv.slice(2);
assert.ok(directory && output, "usage: delete_probe.mjs <owned fixture> <evidence output>");
const state = JSON.parse(await readFile(`${directory}/state.json`, "utf8"));
const native = `http://127.0.0.1:${state.upstream_port}/api/v1`;
const catalogResponse = await fetch(`${native}/schedules`);
const token = catalogResponse.headers.get("X-Mdo-Write-Token");
assert.ok(token);
const catalog = await catalogResponse.json();
assert.equal(catalog.data.total, 2);
const first = catalog.data.items.find(item => item.label === "确认丢失后仍可修改");
const second = catalog.data.items.find(item => item.label === "核对失败后只读取原计划");
assert.ok(first && second && !first.active_runs && !second.active_runs);
const calls = [];
async function request(method, path, options) {
  calls.push(method);
  let response;
  try { response = await fetch(`http://127.0.0.1:${state.port}/api/v1${path}`, {
    method, headers: { "X-Mdo-Write-Token": token, ...(options.ifMatch ? { "If-Match": options.ifMatch } : {}) },
    signal: options.signal }); }
  catch (cause) { throw Object.assign(new Error("Acknowledgement unavailable", { cause }), { code: "network_error" }); }
  const envelope = await response.json();
  if (!response.ok || !envelope.ok) throw Object.assign(new Error("Native refusal"),
    { code: envelope.error?.code, status: response.status });
  return { data: envelope.data, etag: response.headers.get("ETag"), writeToken: response.headers.get("X-Mdo-Write-Token") };
}
const writer = createScheduleWriter({ epoch: () => token, token: () => token,
  client: { get: (path, options) => request("GET", path, options),
    delete: (path, options) => request("DELETE", path, options) } });
await writeFile(`${directory}/fault.json`, JSON.stringify({ method: "DELETE",
  path: `/api/v1/schedules/${first.id}`, mode: "drop", phase: "delete-drop" }));
const result = await writer.remove(first.id, first.revision);
assert.deepEqual(calls, ["DELETE", "GET", "GET"]);
await writeFile(`${output}/delete-probe.json`, JSON.stringify({ result, calls }, null, 2) + "\n");
await writer.remove(second.id, second.revision);
const after = await (await fetch(`${native}/schedules`)).json();
assert.equal(after.data.total, 0);
await writeFile(`${output}/native-schedules-after-delete.json`, JSON.stringify(after, null, 2) + "\n");
console.log("PASS production writer confirms dropped DELETE from native absence, only one DELETE");
