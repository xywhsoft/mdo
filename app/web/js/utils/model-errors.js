import { t } from "../i18n.js";

const kinds = new Set([
  "daily_token_limit", "insufficient_balance", "quota_exceeded",
  "membership_required", "login_required", "authentication_failed",
  "permission_denied", "account_restricted", "request_pending", "model_changed",
  "model_unavailable", "context_limit", "request_too_large", "rate_limited",
  "timeout", "network", "service_unavailable", "invalid_response", "invalid_request",
  "output_limit", "context_compaction",
  "service_quota_exceeded", "service_configuration", "request_configuration",
]);

// Classifications come from the server; unknown/older records retain their
// original explanation. Provider prose is never guessed to be an account error.
export function modelErrorMessage(kind, fallback) {
  return kinds.has(kind) ? t(`model.error.${kind}`, {}, fallback) : fallback;
}

const titles = {
  daily_token_limit: "allowance", quota_exceeded: "allowance",
  service_quota_exceeded: "allowance", insufficient_balance: "balance",
  login_required: "signIn", authentication_failed: "access",
  membership_required: "access", permission_denied: "access",
  account_restricted: "access", service_configuration: "access",
  request_pending: "pending", context_limit: "requestLimit",
  request_too_large: "requestLimit", output_limit: "outputLimit",
  context_compaction: "compaction",
  model_changed: "configuration", model_unavailable: "configuration",
  invalid_request: "configuration", request_configuration: "configuration",
};

export function modelErrorTitle(kind, fallback) {
  return kinds.has(kind)
    ? t(`model.errorTitle.${titles[kind] || "temporary"}`, {}, fallback)
    : fallback;
}
