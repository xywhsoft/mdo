import assert from "node:assert/strict";
import { balanceView, formatBalance } from "../app/web/js/features/account/balance.js";

const value = { currency: "CNY", amount_scale: 1000000, cash_micros: 12345000,
  credit_micros: 5000000, cash_reserved_micros: 1000000, credit_reserved_micros: 500000,
  available_micros: 15845000, updated_at: 1000 };
assert.deepEqual(balanceView(value, 200, 1000), { available: 15845000, cash: 12345000,
  credit: 5000000, reserved: 1500000, stale: false });
assert.equal(formatBalance(value.available_micros, "zh-CN"), "¥15.85");
assert.equal(formatBalance(0, "zh-CN"), "¥0.00");
assert.equal(formatBalance(10050, "zh-CN"), "¥0.01");
assert.equal(balanceView(value, 503, 1000).stale, true);
assert.equal(balanceView(value, 200, 1091).stale, true);
assert.equal(balanceView(null), null);
for (const change of [{ currency: "USD" }, { amount_scale: 100 }, { cash_micros: -1 },
  { cash_micros: Number.MAX_SAFE_INTEGER + 1 }, { available_micros: 1 },
  { credit_reserved_micros: 6000000 }, { updated_at: NaN }])
  assert.equal(balanceView({ ...value, ...change }, 200, 1000), null);
assert.equal(formatBalance(NaN, "zh-CN"), "—");
console.log("PASS CNY micro-unit formatting, reservations, zero, stale and unavailable balance");
