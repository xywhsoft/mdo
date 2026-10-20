import assert from "node:assert/strict";
import test from "node:test";
import { createHash, webcrypto } from "node:crypto";
import { File } from "node:buffer";
import { importShare } from "../app/web/js/features/sharing/import-share.js";

globalThis.crypto ||= webcrypto;
globalThis.File ||= File;
const clone = value => structuredClone(value);
const id = char => char.repeat(32);
const owner = { project_id: "default", session_id: "conversation", create: false };

function setup({ text = "Shared text", files = [], before = { text: "Existing draft", attachments: [] } } = {}) {
  let draft = clone(before), durable = true, acknowledge = true;
  let inbox = { id: id("a"), text, files: files.map(file => ({ name: "file.txt", mime: "text/plain", sha256: "0".repeat(64), ...clone(file) })) };
  const calls = [], uploads = [], plans = [];
  const env = { destination: owner, share: inbox,
    bridge: {
      bind(key, project_id, session_id, create) { inbox.destination = { project_id, session_id, create }; return clone(inbox); },
      plan(key, plan, replace) { inbox.delivery = clone(plan); plans.push({ plan: clone(plan), replace }); return true; },
      acknowledge() { calls.push("ack"); return acknowledge; },
    },
    api: {
      async get() { return { data: { id: owner.session_id, project_id: owner.project_id, status: "active", attachments: 1 } }; },
      async post(path, body) { calls.push([path, body]); return { data: { path: `shared-files/${inbox.id}/${body.file_id}-file.txt`, bytes: 10 } }; },
      async uploadImage(project, session, file, mime, options) { uploads.push({ project, session, file, mime, options }); return { id: options.uploadId }; },
    },
    async createSession() { throw new Error("must reuse conversation"); },
    draftStore: {
      async ensureLoaded() { return true; }, snapshot() { return clone(draft); },
      edit(key, text, attachments) { calls.push("edit"); draft = { text, attachments: clone(attachments) }; },
      async flush() { calls.push("flush"); return durable; },
      isDraftDurable(key, text, attachments) { return durable && draft.text === text && JSON.stringify(draft.attachments) === JSON.stringify(attachments); },
    },
    calls, uploads, plans, draft: () => draft, inbox: () => clone(inbox),
    changeDraft(value) { draft = clone(value); }, setDurable(value) { durable = value; }, setAcknowledge(value) { acknowledge = value; },
  };
  return env;
}

test("shared text appends to an existing draft and acknowledges only after durability", async () => {
  const env = setup(); await importShare(env);
  assert.equal(env.draft().text, "Existing draft\n\nShared text");
  assert.deepEqual(env.calls.slice(-3), ["edit", "flush", "ack"]);
});

test("a lost native acknowledgement reuses the saved plan without duplicating text or files", async () => {
  const env = setup({ files: [{ id: id("b"), name: "novel.txt", bytes: 10, mime: "text/plain" }] });
  env.setAcknowledge(false); await assert.rejects(importShare(env));
  const saved = clone(env.draft()); env.setAcknowledge(true);
  await importShare({ ...env, share: env.inbox() });
  assert.deepEqual(env.draft(), saved);
  assert.equal(env.calls.filter(value => Array.isArray(value)).length, 1);
  assert.equal(env.calls.filter(value => value === "edit").length, 1);
});

test("failed draft persistence retains the share and an exact replay plan", async () => {
  const env = setup(); env.setDurable(false);
  await assert.rejects(importShare(env)); assert.ok(env.inbox().delivery);
  assert.ok(!env.calls.includes("ack")); env.setDurable(true);
  await importShare({ ...env, share: env.inbox() });
  assert.equal(env.calls.filter(value => value === "edit").length, 1);
});

test("an edited draft is never overwritten by a delayed import", async () => {
  const env = setup({ files: [{ id: id("b"), bytes: 3 }] });
  const post = env.api.post;
  env.api.post = async (...args) => { const reply = await post(...args); env.changeDraft({ text: "Newer work", attachments: [] }); return reply; };
  await assert.rejects(importShare(env));
  assert.equal(env.draft().text, "Newer work"); assert.ok(!env.calls.includes("ack"));
  await importShare({ ...env, onConflict: async () => true });
  assert.ok(env.draft().text.startsWith("Newer work\n\n"));
});

test("a saved delivery plan is rebased only after explicit conflict review", async () => {
  const env = setup(); env.setAcknowledge(false); await assert.rejects(importShare(env));
  env.changeDraft({ text: "Other work", attachments: [] }); env.setAcknowledge(true);
  await assert.rejects(importShare({ ...env, share: env.inbox() }));
  assert.equal(env.draft().text, "Other work");
  await importShare({ ...env, share: env.inbox(), onConflict: async () => true });
  assert.equal(env.draft().text, "Other work\n\nShared text"); assert.equal(env.plans.at(-1).replace, true);
});

test("new conversations use the durable destination identity", async () => {
  const env = setup({ before: { text: "", attachments: [] } });
  env.destination = { ...owner, session_id: id("a"), create: true };
  env.api.get = async () => { throw Object.assign(new Error("missing"), { status: 404 }); };
  env.createSession = async value => {
    assert.equal(value.client_session_id, id("a"));
    return { id: value.client_session_id, project_id: value.project_id, status: "active" };
  };
  assert.equal((await importShare(env)).session_id, id("a"));
});

test("image uploads use verified bytes and stable PUT identities", async () => {
  const bytes = new Uint8Array(70000).fill(17);
  const file = { id: id("b"), name: "image.png", mime: "image/png", bytes: bytes.length,
    sha256: createHash("sha256").update(bytes).digest("hex") };
  const env = setup({ files: [file] });
  env.bridge.read = (_share, _file, offset) => Buffer.from(bytes.slice(offset, offset + 65536)).toString("base64");
  await importShare(env);
  assert.equal(env.uploads.length, 1); assert.equal(env.uploads[0].options.uploadId, file.id);
  assert.deepEqual(new Uint8Array(await env.uploads[0].file.arrayBuffer()), bytes);
  assert.deepEqual(env.draft().attachments, [file.id]);
});

test("changed image bytes stop before attachment upload and keep the inbox", async () => {
  const env = setup({ files: [{ id: id("b"), name: "bad.png", mime: "image/png", bytes: 1, sha256: "0".repeat(64) }] });
  env.bridge.read = () => "YQ==";
  await assert.rejects(importShare(env)); assert.equal(env.uploads.length, 0); assert.ok(!env.calls.includes("ack"));
});

test("a mislabeled image remains a usable workspace file without blocking text import", async () => {
  const data = Buffer.from("this is text");
  const env = setup({ files: [{ id: id("b"), name: "not-an-image.png", mime: "image/png", bytes: data.length,
    sha256: createHash("sha256").update(data).digest("hex") }] });
  env.bridge.read = () => data.toString("base64");
  env.api.uploadImage = async () => { throw Object.assign(new Error("not an image"), { code: "image_type_invalid" }); };
  await importShare(env);
  assert.ok(env.draft().text.includes(`shared-files/${env.share.id}/`));
  assert.deepEqual(env.draft().attachments, []); assert.ok(env.calls.includes("ack"));
});

test("a full draft and a device/runtime change do not acknowledge or send a model request", async () => {
  const env = setup({ text: "new", before: { text: "x".repeat(65536), attachments: [] } });
  await assert.rejects(importShare(env)); assert.equal(env.plans.length, 0); assert.ok(!env.calls.includes("ack"));
  await assert.rejects(importShare({ ...setup(), check() { throw new Error("device changed"); } }), /device changed/);
});


test("corrupt inbox metadata or a corrupt replay plan is retained without draft writes", async () => {
  const env = setup({ files: [{ id: id("b"), bytes: 1 }] });
  env.share.files[0].sha256 = "invalid";
  await assert.rejects(importShare(env)); assert.equal(env.calls.length, 0);
  const corrupt = setup(); corrupt.setAcknowledge(false); await assert.rejects(importShare(corrupt));
  const inbox = corrupt.inbox(); inbox.delivery.after.attachments = ["invalid"];
  corrupt.bridge.bind = () => inbox;
  const count = corrupt.calls.length;
  await assert.rejects(importShare({ ...corrupt, share: inbox })); assert.equal(corrupt.calls.length, count);
});
