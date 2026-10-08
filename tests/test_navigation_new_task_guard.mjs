import assert from "node:assert/strict";
import test from "node:test";

test("a pending new task keeps new-task routes in its owning project", async () => {
  const saved = { window: globalThis.window, location: globalThis.location,
    history: globalThis.history };
  const location = { hash: "#/projects/default/new", pathname: "/",
    search: "" };
  const replacements = [];
  globalThis.location = location;
  globalThis.history = { state: null, replaceState(state, _title, url) {
    this.state = state;
    if (url?.includes("#")) {
      location.hash = url.slice(url.indexOf("#"));
      replacements.push(location.hash);
    }
  } };
  globalThis.window = { addEventListener() {} };
  try {
    const { navigation } = await import(
      "../app/web/js/state/navigation.js?new-task-guard-test");
    let pending = "default";
    let redirects = 0;
    navigation.setNewTaskGuard(() => pending, () => { redirects += 1; });

    // The home route has no explicit project. Saving its first task assigns
    // the default owner; canonicalizing that route is not a project conflict.
    location.hash = "#/";
    navigation.revalidate();
    assert.deepEqual([navigation.get().projectId, location.hash],
      ["default", "#/projects/default/new"]);
    assert.equal(redirects, 0);
    replacements.length = 0;

    navigation.newTask("other");
    assert.deepEqual([navigation.get().projectId, location.hash],
      ["default", "#/projects/default/new"]);
    assert.equal(redirects, 1);
    assert.deepEqual(replacements, ["#/projects/default/new"]);

    navigation.openSchedules();
    assert.equal(navigation.get().view, "schedules");
    assert.equal(redirects, 1);
    navigation.backToWorkspace();
    assert.equal(location.hash, "#/projects/default/new");
    assert.equal(redirects, 1);

    navigation.select("other", "existing-session");
    assert.equal(navigation.get().projectId, "other");
    assert.equal(navigation.get().sessionId, "existing-session");

    location.hash = "#/projects/other/new";
    navigation.revalidate();
    assert.equal(navigation.get().projectId, "default");
    assert.equal(location.hash, "#/projects/default/new");
    assert.equal(redirects, 2);

    pending = null;
    navigation.newTask("other");
    assert.equal(navigation.get().projectId, "other");
    assert.equal(location.hash, "#/projects/other/new");
    assert.equal(redirects, 2);

    pending = "other";
    location.hash = "#/";
    navigation.revalidate();
    assert.equal(location.hash, "#/projects/other/new");
    assert.equal(redirects, 2);
  } finally {
    globalThis.window = saved.window;
    globalThis.location = saved.location;
    globalThis.history = saved.history;
  }
});
