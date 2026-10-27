import assert from "node:assert/strict";
import test from "node:test";
import { skillAuthoringPrompt, createSkillAuthoringDraft, expandSkillAuthoringDraft } from "../app/web/js/features/settings/skill-authoring.js";

function fixture() {
  const entries = new Map([["project:existing", { text: "Unsent task", attachments: ["image"] }]]);
  return { entries, draftStore: {
    ensureLoaded: async key => { if (!entries.has(key)) entries.set(key, { text: "", attachments: [] }); return true; },
    snapshot: key => entries.get(key),
    edit: (key, text, attachments) => entries.set(key, { text, attachments }),
    flush: async () => true,
  } };
}

test("authoring uses a dedicated durable draft and preserves existing content", async () => {
  const env = fixture(); let calls = 0;
  const session = await createSkillAuthoringDraft({ ...env, projectId: "work", text: "Create a Skill",
    createSession: async input => { calls++; assert.equal(input.project_id, "work"); return { project_id: "work", id: "new" }; } });
  assert.equal(calls, 1); assert.equal(session.id, "new");
  assert.deepEqual(env.entries.get("project:existing"), { text: "Unsent task", attachments: ["image"] });
  assert.equal(env.entries.get("work/new").text, "Create a Skill");
});

test("target changes and nonempty destination drafts cannot overwrite content", async () => {
  const env = fixture(); let checks = 0;
  await assert.rejects(createSkillAuthoringDraft({ ...env, projectId: "work", text: "Task",
    createSession: async () => ({ project_id: "work", id: "new" }),
    check() { if (++checks > 1) throw new Error("target changed"); } }), /target changed/);
  assert.equal(env.entries.has("work/new"), false);
  env.entries.set("work/new", { text: "Existing", attachments: [] });
  await assert.rejects(createSkillAuthoringDraft({ ...env, projectId: "work", text: "Task",
    createSession: async () => ({ project_id: "work", id: "new" }) }), /already contains/);
  assert.equal(env.entries.get("work/new").text, "Existing");
});

test("authoring prompts retain literal directory variables and declare supported controls", () => {
  const prompt = skillAuthoringPrompt({ directory: "D:/some path/skills", scope: "user" });
  assert.ok(prompt.includes('"D:/some path/skills"')); assert.ok(prompt.includes("${MDO_SKILL_DIR}"));
  assert.ok(prompt.includes("disable-model-invocation")); assert.ok(prompt.includes("不要覆盖"));
  assert.ok(prompt.includes("$ARGUMENTS[0]")); assert.ok(prompt.includes("arguments: [file, focus]"));
  assert.ok(prompt.includes("argument-hint 只提供输入提示")); assert.ok(prompt.includes("不要因此终止任务"));
  assert.throws(() => skillAuthoringPrompt({ directory: "" }), /unavailable/);
});

test("builder modes preserve explicit targets and make reviews read-only", () => {
  const settings = { directory: "D:/mdo/skills", scope: "user", id: "reviewer", request: "Improve the output format" };
  const improve = skillAuthoringPrompt({ ...settings, mode: "improve" });
  assert.ok(improve.includes("改进已有")); assert.ok(improve.includes('"reviewer"'));
  assert.ok(improve.includes("保留原 ID")); assert.ok(improve.includes("保留可恢复副本"));
  assert.ok(improve.includes("Improve the output format"));
  const review = skillAuthoringPrompt({ ...settings, mode: "review" });
  assert.ok(review.includes("不修改文件，不运行目标脚本"));
  assert.throws(() => skillAuthoringPrompt({ ...settings, id: "", mode: "improve" }), /ID/);
  for (const id of ["con", "name..bad", "name.", "../other"])
    assert.throws(() => skillAuthoringPrompt({ ...settings, id, mode: "improve" }), /ID/);
  assert.throws(() => skillAuthoringPrompt({ ...settings, request: "x".repeat(8193) }), /8 KiB/);
  assert.throws(() => skillAuthoringPrompt({ ...settings, mode: "execute" }), /mode/);
});

test("builder prepares the actual installed creator as an unsent bounded draft", async context => {
  const settings = { directory: "D:/mdo-home/skills", scope: "user", projectId: "work", request: "Build a review checklist" };
  context.mock.method(globalThis, "fetch", async (url, options) => {
    assert.equal(url, "/api/v1/skills/invoke?project=work");
    assert.equal(JSON.parse(options.body).id, "skill-creator");
    assert.ok(JSON.parse(options.body).arguments.includes(settings.request));
    return Response.json({ ok: true, data: { text: "Installed creator instructions and task" } });
  });
  assert.equal(await expandSkillAuthoringDraft(settings), "Installed creator instructions and task");
});

test("failed or oversized creator expansion never falls back to an unverified draft", async context => {
  const settings = { directory: "D:/mdo-home/skills", scope: "user" };
  context.mock.method(globalThis, "fetch", async () => Response.json({ ok: true, data: { text: "x".repeat(65537) } }));
  await assert.rejects(expandSkillAuthoringDraft(settings), /64 KiB/);
  context.mock.method(globalThis, "fetch", async () => Response.json({ ok: false, error: { code: "disabled", message: "Creator disabled" } }, { status: 403 }));
  await assert.rejects(expandSkillAuthoringDraft(settings), /Creator disabled/);
});
