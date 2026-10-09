import { portableId, BUILTIN_COMMAND_NAMES, mcpHttpEndpointValid } from "./extension-formats.js";

// Catch common form mistakes before writing. Native validation remains the
// authority for advanced source metadata, resource paths and transport policy.
export function extensionFieldError(key, field, args = {}) {
  const error = new Error(key);
  error.extensionValidation = key; error.field = field; error.args = args;
  return error;
}
export function validateExtensionId(kind, id) {
  if (!portableId(id)) throw extensionFieldError("idInvalid", "extension_id");
  if (kind === "commands" && BUILTIN_COMMAND_NAMES.includes(id))
    throw extensionFieldError("commandReserved", "extension_id");
}
export function requiredExtensionFields(kind, transport = "stdio") {
  if (kind === "mcp") return ["name", "description", transport === "stdio" ? "program" : "endpoint"];
  if (kind === "commands") return ["prompt"];
  if (kind === "subagents") return ["name", "description", "prompt"];
  return ["agents", "skills"].includes(kind) ? ["name", "description"] : [];
}
export function parseExtensionJson(text, field = "source") {
  // JSON.parse messages can include a credential copied from the source.
  try { return JSON.parse(text); }
  catch { throw extensionFieldError("fieldJson", field); }
}
function credentialCollection(text, field) {
  const value = parseExtensionJson(text || "[]", field);
  if (!value || typeof value !== "object" || (!Array.isArray(value) &&
      !Object.values(value).every(item => typeof item === "string" && item && !item.includes("\0"))))
    throw extensionFieldError("fieldCredentials", field);
}
export function validateExtensionForm(kind, values) {
  for (const field of requiredExtensionFields(kind, values.transport))
    if (!String(values[field] ?? "").trim()) throw extensionFieldError("fieldRequired", field);
  const limits = kind === "commands" ? {} : { name: 256, description: kind === "mcp" ? 2048 : 4096 };
  if (["agents", "subagents"].includes(kind)) Object.assign(limits, { model: 256, reasoning_effort: 64 });
  for (const [field, limit] of Object.entries(limits)) {
    const text = String(values[field] ?? "");
    if (text.includes("\0")) throw extensionFieldError("fieldText", field);
    if (new TextEncoder().encode(text).length > limit)
      throw extensionFieldError("fieldLength", field, { limit });
  }
  if (kind !== "mcp") return;
  if (!["stdio", "streamable-http"].includes(values.transport)) throw extensionFieldError("fieldTransport", "transport");
  if (values.transport === "stdio") {
    const args = parseExtensionJson(values.arguments || "[]", "arguments");
    if (!Array.isArray(args) || !args.every(item => typeof item === "string"))
      throw extensionFieldError("fieldStringArray", "arguments");
    credentialCollection(values.environment, "environment");
  } else {
    if (!mcpHttpEndpointValid(values.endpoint)) throw extensionFieldError("fieldHttps", "endpoint");
    credentialCollection(values.headers, "headers");
  }
}
