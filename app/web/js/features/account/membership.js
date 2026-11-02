// Membership expiry comes from the plan, never the login token's lifetime.
export function membershipView(value, now = Date.now() / 1000) {
  if (!value || typeof value.is_vip !== "boolean" || !Number.isFinite(now)) return null;
  const expiresAt = value.plan_expires_at;
  if (!Number.isSafeInteger(expiresAt) || expiresAt < 0 || expiresAt > 8640000000000 ||
      value.is_vip && expiresAt === 0) return null;
  const expired = expiresAt > 0 && expiresAt <= now && Boolean(value.plan_id || value.is_vip);
  const state = value.is_vip && expiresAt > now ? "active" : expired ? "expired" : "free";
  const remaining = state === "active" ? Math.max(0, expiresAt - now) : 0;
  return {
    state, expiresAt,
    days: Math.floor(remaining / 86400),
    hours: Math.floor(remaining % 86400 / 3600),
    minutes: Math.floor(remaining % 3600 / 60),
    stale: !Number.isSafeInteger(value.updated_at) || now - value.updated_at > 90 || value.updated_at > now + 60,
  };
}
