// The server owns membership and token accounting. This view only formats
// remaining allowance; expired or stale facts are never presented as current.
export function allowanceView(value, now = Date.now() / 1000) {
  if (!value || !Array.isArray(value.daily_quotas)) return null;
  const vip = value.is_vip === true && Number.isSafeInteger(value.plan_expires_at) && value.plan_expires_at > now;
  const group = vip ? "vip" : value.group_id === "vip" ? "free" : value.group_id || "free";
  const stale = Boolean(value.plan_id && value.plan_expires_at <= now) ||
    !Number.isSafeInteger(value.updated_at) || now - value.updated_at > 90 || value.updated_at > now + 60;
  const modelQuotas = value.daily_quotas.flatMap(q => {
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
  // Render each shared pot once. Model equivalents describe the SAME balance,
  // never independent grants. Unweighted servers retain their previous view.
  const quotas = [], pools = new Map();
  for (const q of modelQuotas) {
    if (!q.token_pool) { quotas.push(q); continue; }
    if (typeof q.token_pool !== "string" || ![q.token_weight_bps, q.base_token_weight_bps, q.token_peak_multiplier_bps]
      .every(n => Number.isSafeInteger(n) && n > 0) || !q.unlimited && (!Number.isSafeInteger(q.available_model_tokens) || q.available_model_tokens < 0)) continue;
    const previous = pools.get(q.token_pool);
    if (previous) { previous.models.push(q); previous.stale ||= q.stale; }
    else { const shared = { ...q, models: [q] }; pools.set(q.token_pool, shared); quotas.push(shared); }
  }
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
