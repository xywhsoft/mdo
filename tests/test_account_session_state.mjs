import assert from "node:assert/strict";
import test from "node:test";
import { isAccountSignedIn } from "../app/web/js/features/account/session-state.js";

test("an offline remembered account keeps its identity and switch-account action", () => {
  assert.equal(isAccountSignedIn({ state: "refreshing", refresh_available: true,
    remembered: true, message: "account_renewal_pending", profile: { username: "device" } }), true);
  assert.equal(isAccountSignedIn({ state: "refreshing", refresh_available: true,
    remembered: false, busy: true }), true);
});

test("a stale profile after explicit expiry or logout cannot count as signed in", () => {
  const profile = { username: "device" };
  assert.equal(isAccountSignedIn({ state: "expired", refresh_available: false, profile }), false);
  assert.equal(isAccountSignedIn({ state: "signed_out", profile }), false);
  assert.equal(isAccountSignedIn({ state: "refreshing", refresh_available: false, profile }), false);
  assert.equal(isAccountSignedIn(null), false);
});

test("a browser account switch preserves the previous identity until it succeeds", () => {
  assert.equal(isAccountSignedIn({ state: "signed_in" }), true);
  assert.equal(isAccountSignedIn({ state: "authorizing", profile: { username: "device" } }), true);
  assert.equal(isAccountSignedIn({ state: "authorizing" }), false);
});
