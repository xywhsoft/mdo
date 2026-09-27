import test from "node:test";
import assert from "node:assert/strict";
import { projectDefaultsFromWorkspace, projectIdFromName } from
  "../app/web/js/features/sessions/project-identity.js";

test("quick project creation derives the same identity from Windows and Unix paths", () => {
  assert.deepEqual(projectDefaultsFromWorkspace(" D:\\work\\Alpha Project\\ "), {
    workspace_root: "D:\\work\\Alpha Project\\",
    name: "Alpha Project",
    id: "alpha-project",
  });
  assert.deepEqual(projectDefaultsFromWorkspace(" /work/Alpha Project/ "), {
    workspace_root: "/work/Alpha Project/",
    name: "Alpha Project",
    id: "alpha-project",
  });
  assert.equal(projectIdFromName("A / B"), "a-b");
});
