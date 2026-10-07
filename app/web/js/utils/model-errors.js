import { t } from "../i18n.js";

const kinds = new Set([
  "daily_token_limit", "insufficient_balance", "quota_exceeded",
  "membership_required", "login_required", "authentication_failed",
  "permission_denied", "account_restricted", "request_pending", "model_changed",
  "model_unavailable", "context_limit", "request_too_large", "rate_limited",
  "timeout", "network", "service_unavailable", "invalid_response", "invalid_request",
  "output_limit",
  "service_quota_exceeded", "service_configuration",
]);

// Classifications come from the server; unknown/older records retain their
// original explanation. Provider prose is never guessed to be an account error.
export function modelErrorMessage(kind, fallback) {
  return kinds.has(kind) ? t(`model.error.${kind}`, {}, fallback) : fallback;
}
