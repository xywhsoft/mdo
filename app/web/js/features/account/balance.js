// Keep money separate from token allowance. The native host checks ownership;
// this view validates units/totals and distinguishes stale data from zero.
export function balanceView(value, status = 200, now = Date.now() / 1000) {
  if (!value || value.currency !== "CNY" || value.amount_scale !== 1000000) return null;
  const fields = ["cash_micros", "cash_reserved_micros", "credit_micros", "credit_reserved_micros", "available_micros"];
  if (!fields.every(key => Number.isSafeInteger(value[key]) && value[key] >= 0) ||
      value.cash_reserved_micros > value.cash_micros || value.credit_reserved_micros > value.credit_micros ||
      value.available_micros !== value.cash_micros - value.cash_reserved_micros + value.credit_micros - value.credit_reserved_micros ||
      !Number.isSafeInteger(value.updated_at) || value.updated_at < 0) return null;
  return { available: value.available_micros, cash: value.cash_micros,
    credit: value.credit_micros, reserved: value.cash_reserved_micros + value.credit_reserved_micros,
    stale: status !== 200 || now - value.updated_at > 90 || value.updated_at > now + 60 };
}

export function formatBalance(micros, locale) {
  if (!Number.isSafeInteger(micros) || micros < 0) return "—";
  return new Intl.NumberFormat(locale, { style: "currency", currency: "CNY",
    minimumFractionDigits: 2, maximumFractionDigits: 2 }).format(micros / 1000000);
}
