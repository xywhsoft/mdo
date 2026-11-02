import assert from "node:assert/strict";
import test from "node:test";
import { membershipView } from "../app/web/js/features/account/membership.js";

const now = 1762200000;
const plan = { is_vip: true, plan_id: "vip", plan_expires_at: now + 3 * 86400 + 7 * 3600 + 12 * 60, updated_at: now };

test("VIP duration uses plan expiry and separates days, hours and minutes", () => {
  assert.deepEqual(membershipView(plan, now), {
    state: "active", expiresAt: plan.plan_expires_at, days: 3, hours: 7, minutes: 12, stale: false,
  });
  const short = membershipView({ ...plan, plan_expires_at: now + 3599 }, now);
  assert.equal(short.hours, 0); assert.equal(short.minutes, 59);
  const ending = membershipView({ ...plan, plan_expires_at: now + 1 }, now);
  assert.equal(ending.state, "active"); assert.equal(ending.minutes, 0);
});

test("exact expiry and expired plans cannot have a positive VIP countdown", () => {
  for (const expiry of [now, now - 1, now - 86400]) {
    const view = membershipView({ ...plan, plan_expires_at: expiry }, now);
    assert.equal(view.state, "expired");
    assert.equal(view.days + view.hours + view.minutes, 0);
  }
  assert.equal(membershipView({ ...plan, is_vip: false, plan_expires_at: now - 1 }, now).state, "expired");
});

test("free membership and missing or malformed expiry never imply perpetual VIP", () => {
  assert.equal(membershipView({ ...plan, is_vip: false, plan_id: "", plan_expires_at: 0 }, now).state, "free");
  assert.equal(membershipView({ ...plan, is_vip: false }, now).state, "free");
  for (const expiry of [undefined, 0, -1, NaN, Infinity, "2000000000", 1.5, 8640000000001])
    assert.equal(membershipView({ ...plan, plan_expires_at: expiry }, now), null);
  assert.equal(membershipView(null, now), null);
  assert.equal(membershipView({ expires_in: 900, expires_at: now + 900 }, now), null);
});

test("stale facts and future clock skew are marked for refresh", () => {
  assert.equal(membershipView(plan, now + 90).stale, false);
  assert.equal(membershipView(plan, now + 91).stale, true);
  assert.equal(membershipView({ ...plan, updated_at: now + 61 }, now).stale, true);
  assert.equal(membershipView({ ...plan, updated_at: undefined }, now).stale, true);
});
