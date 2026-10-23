import assert from "node:assert/strict";
import test from "node:test";
import { emptySidebarLayout, validSidebarLayout, moveSidebarItem, organizedSidebar, timelineSidebar,
  createSidebarLayoutStore } from "../app/web/js/features/sessions/sidebar-layout.js";
import { sidebarGroups } from "../app/web/js/features/sessions/sidebar-tree.js";
const a = "a".repeat(32), b = "b".repeat(32);
const initial = () => ({ ...emptySidebarLayout(), groups: [{ id: a, name: "Work" }, { id: b, name: "Later" }] });

test("sidebar layout rejects unknown fields, duplicate identities and invalid destinations", () => {
  const value = initial(); assert.equal(Boolean(validSidebarLayout(value)), true);
  for (const invalid of [{ ...value, extra: true }, { ...value, groups: [...value.groups, value.groups[0]] },
    { ...value, placements: [{ key: "s:../escape", group: a }] }, { ...value, placements: [{ key: "p:alpha", group: "missing" }] },
    { ...value, groups: [{ id: a, name: "\ninvalid" }] }]) assert.equal(Boolean(validSidebarLayout(invalid)), false);
});

test("group assignments preserve project ownership, pins, filtering and stable manual order", () => {
  const sessions = [{ id: "one", project_id: "alpha", status: "active", title: "one", updated_at: 3 },
    { id: "two", project_id: "alpha", status: "active", title: "two", updated_at: 2 },
    { id: "pin", project_id: "alpha", status: "active", pinned: true, updated_at: 4 }];
  const base = sidebarGroups({ sessions, projects: [{ id: "alpha", name: "Alpha" }] });
  let layout = moveSidebarItem(initial(), "p:alpha", a);
  layout = moveSidebarItem(layout, "s:alpha/two", b);
  layout = moveSidebarItem(layout, "s:alpha/pin", b);
  const tree = organizedSidebar(base, layout);
  assert.equal(tree.projects.length, 0); assert.equal(tree.custom[0].entries[0].item.id, "alpha");
  assert.deepEqual(tree.custom[0].entries[0].item.sessions.map(item => item.id), ["one"]);
  assert.equal(tree.custom[1].entries[0].item.project_id, "alpha"); assert.equal(tree.pinned[0].id, "pin");
  assert.equal(tree.custom[1].entries.length, 1); assert.equal(sessions[1].project_id, "alpha");
  layout = moveSidebarItem(initial(), "s:alpha/two", "", "s:alpha/one", ["s:alpha/one", "s:alpha/two"]);
  assert.deepEqual(organizedSidebar(base, layout).projects[0].sessions.map(item => item.id), ["two", "one"]);
});

test("timeline buckets use calendar days, and orphaned placements do not hide new sessions", () => {
  const now = new Date(2026, 9, 23, 12).getTime();
  const source = [0, 1, 5, 20].map((days, i) => ({ id: String(i), updated_at: (now - days * 86400000) / 1000 }));
  assert.deepEqual(timelineSidebar(source, now).map(item => [item.id, item.sessions.length]), [["today",1],["yesterday",1],["week",1],["older",1]]);
  const layout = moveSidebarItem(initial(), "s:removed/old", a);
  const tree = organizedSidebar(sidebarGroups({ sessions: [{ id: "new", project_id: "default", status: "active" }] }), layout);
  assert.equal(tree.tasks[0].id, "new");
});

test("lost layout acknowledgement confirms by read without replaying PUT; peer conflict is retained", async () => {
  let saved = emptySidebarLayout(), writes = 0, reads = 0, fail = false;
  const store = createSidebarLayoutStore({ read: async () => { ++reads; return { data: structuredClone(saved) }; },
    write: async (_path, next) => { ++writes;
      saved = fail ? { ...next, revision: next.revision + 1, view: "workspace" } : { ...next, revision: next.revision + 1 };
      throw new Error("ACK lost or peer conflict"); }, recovery: () => ({ request: fn => fn(), dispose() {} }) });
  await new Promise(resolve => setTimeout(resolve, 0));
  assert.equal(await store.update(value => ({ ...value, view: "timeline" })), true);
  assert.equal(writes, 1); assert.equal(reads, 2);
  fail = true;
  await assert.rejects(store.update(value => ({ ...value, groups: initial().groups, view: "timeline" })), /peer conflict/);
  assert.equal(store.get().view, "workspace"); assert.equal(writes, 2); store.destroy();
});

test("destroy cancels an unresponsive write and never applies its late reply", async () => {
  let release, reads = 0, writes = 0;
  const store = createSidebarLayoutStore({ read: async () => { ++reads; return { data: emptySidebarLayout() }; },
    write: async (_path, next) => { ++writes; await new Promise(resolve => { release = resolve; }); return { data: { ...next, revision: 1 } }; } });
  await new Promise(resolve => setTimeout(resolve, 0));
  const mutation = store.update(value => ({ ...value, view: "timeline" }));
  await Promise.resolve(); await Promise.resolve(); store.destroy();
  await assert.rejects(mutation, { name: "AbortError" }); release(); await Promise.resolve();
  assert.equal(store.get().view, "workspace"); assert.equal(writes, 1); assert.equal(reads, 1);
  let coldReads = 0;
  const cold = createSidebarLayoutStore({ read: async () => { ++coldReads; return { data: emptySidebarLayout() }; } });
  cold.destroy(); await Promise.resolve(); assert.equal(coldReads, 0);
});
