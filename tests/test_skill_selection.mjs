import assert from "node:assert/strict";
import test from "node:test";
import { parseSkillSelection, expandSkillSelection, loadSelectableSkills } from "../app/web/js/features/chat/skill-selection.js";

test("explicit Skill selectors preserve literal arguments and plugin IDs", () => {
  assert.deepEqual(parseSkillSelection("$review fix $1 and $&"), {id:"review",arguments:"fix $1 and $&"});
  assert.deepEqual(parseSkillSelection("/skill:plugin:review a\nb"), {id:"plugin:review",arguments:"a\nb"});
  assert.equal(parseSkillSelection("Explain $review"), null);
  assert.equal(parseSkillSelection("/stop"), null);
  assert.equal(parseSkillSelection("$../escape"), null);
});

test("Skill selection preserves quoted arguments through the shared API and keeps hints optional", async context => {
  const raw = String.raw`"C:\new folder\a.txt" "检查 $0 和 $focus" ""`;
  context.mock.method(globalThis, "fetch", async (url, options) => {
    if (options?.method === "POST") {
      assert.equal(url, "/api/v1/skills/invoke?project=work");
      assert.deepEqual(JSON.parse(options.body), { id: "review", arguments: raw });
      return Response.json({ ok: true, data: { text: "Expanded by the shared server renderer" } });
    }
    return Response.json({ ok: true, data: { items: [
      { id: "review", argument_hint: '"file" "focus"', arguments: ["file", "focus"] },
      { id: "plain" }, { id: "private", user_invocable: false },
    ] } });
  });
  const items = await loadSelectableSkills("work");
  assert.equal(items.length, 2); assert.equal(items[0].argument_hint, '"file" "focus"');
  assert.deepEqual(items[0].arguments, ["file", "focus"]);
  assert.equal(await expandSkillSelection(parseSkillSelection(`$review ${raw}`), "work"), "Expanded by the shared server renderer");
});

test("oversized inline instructions are rejected before replacing a durable composer draft", async context => {
  context.mock.method(globalThis, "fetch", async url => {
    assert.equal(url,"/api/v1/skills/invoke?project=work");
    return Response.json({ok:true,data:{text:"x".repeat(65537)}});
  });
  await assert.rejects(expandSkillSelection({id:"large"},"work"),/64 KiB/);
});
