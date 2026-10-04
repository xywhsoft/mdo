import assert from "node:assert/strict";
import test from "node:test";
import { sidebarGroups, sidebarWindow, recentSessions } from "../app/web/js/features/sessions/sidebar-tree.js";

const session = (id, project_id = "default", changes = {}) => ({ id, project_id,
  title: id, status: "active", pinned: false, updated_at: 1, ...changes });
const projects = [{ id: "default", name: "Default" },
  { id: "alpha", name: "Alpha" }, { id: "empty", name: "Empty" }];

test("pinning moves a task out of its project while default tasks remain separate", () => {
  const source = [session("pinned", "alpha", { pinned: true }), session("child", "alpha"), session("ordinary")];
  const tree = sidebarGroups({ sessions: source, projects });
  assert.deepEqual(tree.pinned.map(s => s.id), ["pinned"]);
  assert.deepEqual(tree.projects.map(p => [p.id, p.sessions.map(s => s.id)]), [["alpha", ["child"]], ["empty", []]]);
  assert.deepEqual(tree.tasks.map(s => s.id), ["ordinary"]);
  assert.equal(tree.total, 3);
  assert.equal(source.length, 3);
});

test("removing a definition keeps its saved history visible, without management privileges", () => {
  const tree = sidebarGroups({ sessions: [session("saved", "unregistered")], projects: [] });
  assert.equal(tree.projects[0].id, "unregistered");
  assert.equal(tree.projects[0].project, undefined);
  assert.equal(tree.projects[0].sessions[0].id, "saved");
});

test("status views never lose archived pinned tasks or mix them into active pins", () => {
  const sessions = [session("live", "alpha", { pinned: true }),
    session("old", "alpha", { status: "archived", pinned: true }),
    session("deleted", "default", { status: "trash" })];
  const archived = sidebarGroups({ sessions, projects, status: "archived" });
  assert.equal(archived.pinned.length, 0);
  assert.deepEqual(archived.projects.map(p => p.sessions.map(s => s.id)), [["old"]]);
  const all = sidebarGroups({ sessions, projects, status: "all" });
  assert.equal(all.total, 3);
  assert.equal(all.tasks[0].id, "deleted");
});

test("search filters empty projects and matches localized default project names", () => {
  const sessions = [session("work", "alpha"), session("free")];
  const found = sidebarGroups({ sessions, projects, query: "ALPHA" });
  assert.deepEqual(found.projects.map(p => p.id), ["alpha"]);
  assert.equal(found.projects[0].sessions[0].id, "work");
  assert.equal(found.tasks.length, 0);
  assert.equal(sidebarGroups({ sessions, projects, query: "默认项目", defaultProjectName: "默认项目" }).tasks[0].id, "free");
  assert.equal(sidebarGroups({ sessions, projects, query: "missing" }).total, 0);
});

test("project sorting uses activity or names without changing catalog data", () => {
  const sessions = [session("new", "empty", { updated_at: 3 }), session("old", "alpha", { updated_at: 2 })];
  assert.deepEqual(sidebarGroups({ sessions, projects }).projects.map(p => p.id), ["alpha", "empty"]);
  assert.deepEqual(sidebarGroups({ sessions, projects, projectSort: "recent" }).projects.map(p => p.id), ["empty", "alpha"]);
  assert.deepEqual(projects.map(p => p.id), ["default", "alpha", "empty"]);
  assert.deepEqual(recentSessions(sessions).map(s => s.id), ["new", "old"]);
  const withPin = [...sessions, session("pin", "alpha", { updated_at: 10, pinned: true })];
  assert.deepEqual(sidebarGroups({ sessions: withPin, projects, projectSort: "recent" }).projects.map(p => p.id), ["alpha", "empty"]);
});

test("older selected tasks remain visible without rendering the whole hidden prefix", () => {
  const sessions = Array.from({ length: 12 }, (_, i) => session(String(i)));
  const limited = sidebarWindow(sessions, 5, "default/11");
  assert.deepEqual(limited.items.map(s => s.id), ["0", "1", "2", "3", "4", "11"]);
  assert.equal(limited.remaining, 6);
  assert.equal(sidebarWindow(sessions, 5, "default/1").items.length, 5);
  assert.equal(sidebarWindow(sessions, 5, "", true).items.length, 12);
});
