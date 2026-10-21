import assert from "node:assert/strict";
import test from "node:test";
import { parseSkillSelection } from "../app/web/js/features/chat/skill-selection.js";

test("explicit Skill selectors preserve literal arguments and plugin IDs", () => {
  assert.deepEqual(parseSkillSelection("$review fix $1 and $&"), {id:"review",arguments:"fix $1 and $&"});
  assert.deepEqual(parseSkillSelection("/skill:plugin:review a\nb"), {id:"plugin:review",arguments:"a\nb"});
  assert.equal(parseSkillSelection("Explain $review"), null);
  assert.equal(parseSkillSelection("/stop"), null);
  assert.equal(parseSkillSelection("$../escape"), null);
});
