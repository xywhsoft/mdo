import assert from "node:assert/strict";
import test from "node:test";
import { skillAuthoringPrompt, createSkillAuthoringDraft } from "../app/web/js/features/settings/skill-authoring.js";

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
  const prompt = skillAuthoringPrompt({ directory: "D:/some path/skills", scope: "project" });
  assert.ok(prompt.includes('"D:/some path/skills"')); assert.ok(prompt.includes("${MDO_SKILL_DIR}"));
  assert.ok(prompt.includes("disable-model-invocation")); assert.ok(prompt.includes("不要覆盖"));
  assert.ok(prompt.includes("$ARGUMENTS[0]")); assert.ok(prompt.includes("arguments: [file, focus]"));
  assert.ok(prompt.includes("argument-hint 只提供输入提示")); assert.ok(prompt.includes("不要因此终止任务"));
  assert.throws(() => skillAuthoringPrompt({ directory: "" }), /unavailable/);
});
