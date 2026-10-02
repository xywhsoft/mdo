// Consume actual C-transformed, disk-read-back sidecars through the live UI
// controllers. This verifies reload/admission logic, not physical DOM layout.
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { join } from "node:path";
import { createDraftStore } from "../../app/web/js/features/chat/draft-store.js";
import { createPromptQueue } from "../../app/web/js/features/chat/prompt-queue.js";
import { createSubmissionController } from "../../app/web/js/features/chat/submission-controller.js";

const stage = process.argv[2];
const queue = JSON.parse(await readFile(join(stage, "queue.json"), "utf8"));
const draft = JSON.parse(await readFile(join(stage, "draft.json"), "utf8"));
const meta = JSON.parse(await readFile(join(stage, "meta.json"), "utf8"));
const project = meta.project_id, session = meta.id;
const key = `${project}/${session}`;
const requests = [], reviews = [];
globalThis.window = { clearTimeout, setTimeout, addEventListener() {} };
globalThis.document = { activeElement: null };
globalThis.fetch = async (path, options = {}) => {
  const method = options.method ?? "GET";
  requests.push({ path, method });
  assert.equal(method, "GET", "restored inputs must not mutate or dispatch on reload");
  if (path.endsWith("/draft")) return Response.json({ ok: true, data: draft });
  if (path.endsWith("/queue")) return Response.json({ ok: true, data: queue });
  if (path.includes("/queue/")) return Response.json({ ok: false, error: {
    code: "queue_item_not_found", message: "No receipt for the fresh review ID" } }, { status: 404 });
  assert.fail(`unexpected request ${path}`);
};
const drafts = createDraftStore({ onRestore() {}, onError(error) { throw error; }, onSaved() {} });
// Queue rendering is idle while this background session is inspected. Its
// load/find/receipt/hasStaged paths are the actual production implementation.
const prompts = createPromptQueue({ container: { dataset: {}, hidden: true,
  contains: () => false, replaceChildren() {}, querySelector: () => null },
  navigation: { get: () => ({}), subscribe() {} }, isRunActive: () => false,
  onRetry() { assert.fail("unexpected queue dispatch"); }, onRemoved() {} });
const controller = createSubmissionController({ draftStore: drafts, promptQueue: prompts,
  onPersisted() { assert.fail("unexpected admission"); }, onPromoted() { assert.fail("unexpected promotion"); },
  onConsumed() { assert.fail("unexpected receipt consumption"); },
  onReview(_key, item) { reviews.push(item.id); }, onRestored() {}, onChange() {} });
await prompts.select(project, session);
await drafts.ensureLoaded(key);
await controller.reconcile(key);
await controller.pump(key);
assert.equal(prompts.hasStaged(project, session), queue.items.some(item => item.state === "staged"));
assert.deepEqual(drafts.submissions(key), draft.submissions);
if (draft.submissions.length) assert.equal(reviews[0], draft.submissions[0].id);
else assert.equal(reviews.length, 0, "composer-only drafts do not request submission review");
assert.equal(controller.hasInFlight(), false);
assert(requests.length > 0 && requests.every(({ method }) => method === "GET"));
// With the staged queue absent, rejected intents still request review; they
// must not become prepared or issue a queue POST during another reload.
queue.items = [];
await prompts.select(project, session);
await controller.reconcile(key);
await controller.pump(key);
assert.deepEqual(drafts.submissions(key), draft.submissions);
assert.equal(controller.hasInFlight(), false);
assert(requests.every(({ method }) => method === "GET"));
console.log("actual restored sidecars/live frontend reload controllers: PASS");
