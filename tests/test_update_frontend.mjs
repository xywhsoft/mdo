import test from "node:test";
import assert from "node:assert/strict";
import { updateActions } from "../app/web/js/features/update/update-panel.js";

test("installation is offered only for an enabled, idle, verified package", () => {
  assert.equal(updateActions({ enabled: true, ready: false }).install, false);
  assert.equal(updateActions({ enabled: false, ready: true }).install, false);
  assert.equal(updateActions({ enabled: true, ready: true, busy: true }).install, false);
  assert.equal(updateActions({ enabled: true, ready: true, busy: false }).install, true);
});
test("download and cancellation follow the current operation", () => {
  assert.equal(updateActions({ enabled: true, status: "current" }).download, false);
  assert.equal(updateActions({ enabled: true, status: "available" }).download, true);
  assert.equal(updateActions({ enabled: true, status: "available", busy: true }).download, false);
  assert.equal(updateActions({ enabled: true, status: "downloading", busy: true }).cancel, true);
  assert.equal(updateActions({ enabled: true, status: "installing", busy: true }).cancel, false);
});
