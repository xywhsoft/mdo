import assert from "node:assert/strict";
import test from "node:test";
import { api, attachmentFileName } from "../app/web/js/api/client.js";
import { createImageNameCache, labelImageName, previewImageName } from
  "../app/web/js/features/chat/image-names.js";

const id = "a".repeat(32);
const owner = { projectId: "demo", sessionId: "one" };
const envelope = (name, schema = 2) => ({ data: { id, schema_version: schema,
  ...(schema === 2 ? { file_name: name } : {}) } });
const deferred = () => {
  let resolve;
  const promise = new Promise((done) => { resolve = done; });
  return { promise, resolve };
};

test("image filenames are bounded by UTF-8 bytes and exclude path/control/malformed text", () => {
  assert.equal(attachmentFileName("界".repeat(340) + ".png"), "界".repeat(340) + ".png");
  for (const name of ["界".repeat(341) + ".png", "a/b.png", "a\\b.png",
    "a\0.png", "a\r\n.png", "a\x7f.png", "\ud800.png", null])
    assert.equal(attachmentFileName(name), "");
  assert.equal(attachmentFileName('截图 "1" 100%.png'), '截图 "1" 100%.png');
});

test("uploads encode names in one header and preserve the original binary body", async () => {
  const original = globalThis.fetch;
  const calls = [];
  globalThis.fetch = async (path, options) => {
    calls.push({ path, ...options });
    return Response.json({ ok: true, data: { id } });
  };
  try {
    const file = Object.assign(new Blob(["image"]), { name: '截图 "1" 100%.png' });
    await api.uploadImage(owner.projectId, owner.sessionId, file, "image/png");
    assert.equal(calls[0].body, file);
    assert.equal(new Headers(calls[0].headers).get("X-Mdo-File-Name"),
      encodeURIComponent(file.name));
    await api.uploadImage(owner.projectId, owner.sessionId, new Blob(["image"]), "image/png");
    assert.equal(new Headers(calls[1].headers).get("X-Mdo-File-Name"), null);
    for (const name of ["a/b.png", "\ud800.png", "x".repeat(1025)])
      await assert.rejects(api.uploadImage(owner.projectId, owner.sessionId,
        Object.assign(new Blob(["image"]), { name }), "image/png"), { code: "image_name_invalid" });
    assert.equal(calls.length, 2);
  } finally { globalThis.fetch = original; }
});

test("draft and history share pending name reads but session and project scopes stay separate", async () => {
  const pending = deferred();
  const paths = [];
  const cache = createImageNameCache({ read: (path) => { paths.push(path); return pending.promise; } });
  const draft = cache.get(owner, id);
  assert.equal(cache.get(owner, id), draft);
  const otherSession = cache.get({ ...owner, sessionId: "two" }, id);
  const otherProject = cache.get({ ...owner, projectId: "other" }, id);
  await Promise.resolve();
  assert.equal(new Set(paths).size, 3);
  pending.resolve(envelope("capture.png"));
  assert.deepEqual(await Promise.all([draft, otherSession, otherProject]),
    ["capture.png", "capture.png", "capture.png"]);
  assert.equal(await cache.get(owner, id), "capture.png");
  assert.equal(paths.length, 3);
});

test("legacy metadata keeps generic labels while malformed/failed reads retry after a bound", async () => {
  let clock = 0;
  let reads = 0;
  const cache = createImageNameCache({ retryMs: 10, now: () => clock,
    read: async () => { reads += 1; return reads === 1 ? envelope("bad/path.png") : envelope("ok.png"); } });
  assert.equal(await cache.get(owner, id), "");
  assert.equal(await cache.get(owner, id), "");
  assert.equal(reads, 1);
  clock = 10;
  assert.equal(await cache.get(owner, id), "ok.png");
  assert.equal(reads, 2);
  let legacyReads = 0;
  const legacy = createImageNameCache({ read: async () => { legacyReads += 1; return envelope("", 1); } });
  assert.equal(await legacy.get(owner, id), "");
  assert.equal(await legacy.get(owner, id), "");
  assert.equal(legacyReads, 1);
});

test("recent attachment names stay in a bounded cache and evicted entries can reload", async () => {
  let reads = 0;
  const cache = createImageNameCache({ limit: 2, read: async () => { reads += 1; return envelope("ok.png"); } });
  const two = { ...owner, sessionId: "two" };
  const three = { ...owner, sessionId: "three" };
  await cache.get(owner, id);
  await cache.get(two, id);
  await cache.get(owner, id); // keep this entry most recently used
  await cache.get(three, id);
  assert.equal(cache.peek(two, id), undefined);
  assert.equal(cache.peek(owner, id), "ok.png");
  await cache.get(two, id);
  assert.equal(reads, 4);
});

function thumbnail() {
  const image = { alt: "generic" };
  const preview = { isConnected: true, dataset: { imageRef: "original" },
    attrs: {}, setAttribute(name, value) { this.attrs[name] = value; },
    querySelector: () => image };
  return { preview, image, caption: { textContent: "generic" },
    remove: { attrs: {}, setAttribute(name, value) { this.attrs[name] = value; } } };
}
function label(nodes, cache) {
  return labelImageName({ ...nodes, cache, owner, id,
    viewLabel: (name) => `View ${name}`, removeLabel: (name) => `Remove ${name}` });
}

test("cached upload names label detached new cards before reconciliation and use literal text", async () => {
  let reads = 0;
  const cache = createImageNameCache({ read: async () => { reads += 1; throw new Error("unused"); } });
  const name = "<img src=x onerror=alert(1)>.png";
  cache.remember(owner, id, name);
  const nodes = thumbnail();
  nodes.preview.isConnected = false;
  const completed = label(nodes, cache);
  assert.equal(nodes.caption.textContent, name);
  assert.equal(nodes.image.alt, name);
  assert.equal(nodes.preview.attrs["aria-label"], `View ${name}`);
  assert.equal(nodes.remove.attrs["aria-label"], `Remove ${name}`);
  await completed;
  assert.equal(reads, 0);
});

test("late reads update only the still connected thumbnail with its original reference", async () => {
  for (const change of ["detached", "reused", "connected"]) {
    const pending = deferred();
    const cache = createImageNameCache({ read: () => pending.promise });
    const nodes = thumbnail();
    const done = label(nodes, cache);
    if (change === "detached") nodes.preview.isConnected = false;
    if (change === "reused") nodes.preview.dataset.imageRef = "another-session";
    pending.resolve(envelope("capture.png"));
    await done;
    assert.equal(nodes.caption.textContent, change === "connected" ? "capture.png" : "generic");
  }
});

test("an open preview retains its name read after the original thumbnail is detached", async () => {
  const pending = deferred();
  const cache = createImageNameCache({ read: () => pending.promise });
  const nodes = thumbnail();
  const labels = label(nodes, cache);
  const previewName = previewImageName(nodes.preview);
  nodes.preview.isConnected = false;
  pending.resolve(envelope("original.png"));
  assert.equal(await previewName, "original.png");
  await labels;
  assert.equal(nodes.image.alt, "generic"); // the detached DOM still stays untouched
});

test("reusing a thumbnail binds a new name read without changing a captured preview", async () => {
  const nodes = thumbnail();
  const first = deferred();
  const second = deferred();
  const firstCache = createImageNameCache({ read: () => first.promise });
  const secondCache = createImageNameCache({ read: () => second.promise });
  const oldLabels = label(nodes, firstCache);
  const originalPreview = previewImageName(nodes.preview);
  nodes.preview.dataset.imageRef = "replacement";
  const newLabels = label(nodes, secondCache);
  const replacementPreview = previewImageName(nodes.preview);
  first.resolve(envelope("original.png"));
  second.resolve(envelope("replacement.png"));
  assert.equal(await originalPreview, "original.png");
  assert.equal(await replacementPreview, "replacement.png");
  await Promise.all([oldLabels, newLabels]);
  assert.equal(nodes.image.alt, "replacement.png");
  assert.equal(await previewImageName({}), "");
});
