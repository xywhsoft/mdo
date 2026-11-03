// Prefer an exact user-defined identity before product compatibility aliases.
export function findModel(models, id) {
  return models.find((model) => model.id === id) ??
    models.find((model) => model.aliases?.includes(id)) ?? null;
}

// Online identities remain in saved sessions before their transient entries
// arrive. Exact personal models retain their own authentication contract.
export function modelLoginBlock(catalog, id) {
  const model = findModel(catalog?.models ?? [], id);
  const awaitingDefault = !id && Array.isArray(catalog?.models) && !catalog.models.length &&
    ["pending", "unavailable"].includes(catalog.login_state);
  const requiresLogin = awaitingDefault || (model ? model.requires_login === true
    : typeof id === "string" && (id.startsWith("mdo-online.") ||
      ["ornith-1.5-35b", "ling-3.0-tiny", "ling-gpu"].includes(id)));
  if (!requiresLogin || catalog?.login_state === "ready") return "";
  return catalog?.login_state === "signed_out" ? "profile_login_required"
    : catalog?.login_state === "unavailable" ? "profile_login_unavailable"
      : "profile_login_pending";
}
