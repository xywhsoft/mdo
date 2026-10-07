// The server owns membership and token accounting. This view only formats
// remaining allowance; expired or stale facts are never presented as current.
export function allowanceView(value, now = Date.now() / 1000) {
  if (!value || !Array.isArray(value.daily_quotas)) return null;
  const vip = value.is_vip === true && Number.isSafeInteger(value.plan_expires_at) && value.plan_expires_at > now;
  const group = vip ? "vip" : value.group_id === "vip" ? "free" : value.group_id || "free";
  const stale = Boolean(value.plan_id && value.plan_expires_at <= now) ||
    !Number.isSafeInteger(value.updated_at) || now - value.updated_at > 90 || value.updated_at > now + 60;
  const quotas = value.daily_quotas.flatMap(q => {
    if (!q || typeof q.model_id !== "string" || typeof q.title !== "string") return [];
    if (![q.used_tokens, q.reserved_tokens, q.resets_at].every(n => Number.isSafeInteger(n) && n >= 0)) return [];
    const unlimited = q.unlimited === true;
    if (!unlimited && (!Number.isSafeInteger(q.limit_tokens) || q.limit_tokens <= 0 ||
        !Number.isSafeInteger(q.remaining_tokens) || q.remaining_tokens < 0 || q.remaining_tokens > q.limit_tokens)) return [];
    const ratio = unlimited ? null : q.remaining_tokens / q.limit_tokens * 100;
    return [{ ...q, unlimited, percentage: ratio === null ? null : Math.max(0, Math.min(100, ratio)),
      percentText: ratio === null ? "∞" : ratio > 0 && ratio < 1 ? "<1%" : `${Math.floor(ratio)}%`,
      stale: stale || q.resets_at <= now }];
  });
  return { vip, group, quotas, stale };
}

// Search has its own server reset time; do not reuse the model's Beijing day.
export function searchAllowanceView(value, now = Date.now() / 1000) {
  if (!value || ![value.daily_used, value.daily_limit, value.daily_reset_at]
    .every(n => Number.isSafeInteger(n) && n >= 0) || value.daily_limit === 0 ||
    value.daily_reset_at === 0 || value.daily_reset_at > 8640000000000) return null;
  const remaining = Math.max(0, value.daily_limit - value.daily_used);
  const percentage = remaining / value.daily_limit * 100;
  return { used: value.daily_used, limit: value.daily_limit, remaining, percentage,
    percentText: percentage > 0 && percentage < 1 ? "<1%" : `${Math.floor(percentage)}%`,
    resets_at: value.daily_reset_at, stale: value.daily_reset_at <= now, unlimited: false };
}
