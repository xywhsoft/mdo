export const STORE_FORMAT = "mdo.extension.v1";
export const STORE_EDITOR_KINDS = ["agents", "subagents", "tools", "skills", "mcp", "commands"];
export const STORE_KINDS = [...STORE_EDITOR_KINDS, "modules", "toolpacks", "plugins"];
export const STORE_PLATFORMS = ["windows-x86_64", "android-arm64-v8a", "linux-x86_64", "macos-arm64"];
export const containsCode = resources => resources.some(r => ["tools", "commands", "modules", "toolpacks", "plugins"].includes(r.kind) || r.kind.startsWith("c-"));

// Publication includes only explicitly selected resource files. Credentials
// are also redacted by the native exporter, before reaching this UI.
export function makeStorePackage(fields, resources) {
  const v2 = fields.min_build_id || resources.some(r => ["modules", "toolpacks", "plugins"].includes(r.kind));
  if (!/^[a-z0-9][a-z0-9._-]{0,63}$/.test(fields.slug) || fields.slug.includes(".."))
    throw new Error("Invalid package ID");
  if (!/^[A-Za-z0-9][A-Za-z0-9._-]{0,31}$/.test(fields.version)) throw new Error("Invalid version");
  if (v2 && (!/^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$/.test(fields.version) || fields.version.split(".").some(n => Number(n) > 4294967295))) throw new Error("Invalid version");
  if (!fields.name.trim() || !fields.description.trim() || !fields.readme.trim() || !fields.license.trim())
    throw new Error("Name, description, documentation and license are required");
  if (!resources.length || resources.length > 16 || !fields.platforms.length ||
      fields.platforms.some(p => !STORE_PLATFORMS.includes(p))) throw new Error("Select resources and supported platforms");
  const result = { format: v2 ? "mdo.extension.v2" : STORE_FORMAT, manifest: {
    slug: fields.slug, name: fields.name.trim(), version: fields.version,
    description: fields.description.trim(), readme: fields.readme.trim(),
    license: fields.license.trim(), platforms: fields.platforms,
    ...(v2 ? { min_build_id: Math.max(30000126, fields.min_build_id || 0) } : {}),
    ...(fields.changelog?.trim() ? { changelog: fields.changelog.trim() } : {}),
  }, resources };
  const bytes = new TextEncoder().encode(JSON.stringify(result)).length;
  if (bytes > 1024 * 1024) {
    result.format = "mdo.extension.v2";
    result.manifest.min_build_id = Math.max(bytes > 6 * 1024 * 1024 ? 50000009 : 30000126, fields.min_build_id || 0);
  }
  if (new TextEncoder().encode(JSON.stringify(result)).length > 100 * 1024 * 1024)
    throw new Error("Package exceeds 100 MiB");
  return result;
}

export const receiptKey = row => row.key || String(row.id);
export function resourceReferences(row) {
  if (Array.isArray(row.resources)) return row.resources.map(({kind,id}) => ({kind,id}));
  const refs = new Map();
  for (const file of row.files || []) {
    const match = /^(agents|subagents|tools|mcp|commands)\/([^/]+)\.(?:md|c|json)$/.exec(file.path);
    const skill = /^skills\/([^/]+)\/SKILL\.md$/.exec(file.path);
    const code = /^modules\/(agents|subagents)\/([^/]+)\.c$/.exec(file.path);
    const ref = match ? {kind:match[1],id:match[2]} : skill ? {kind:"skills",id:skill[1]} : code ? {kind:`c-${code[1]}`,id:code[2]} : null;
    if (ref) refs.set(`${ref.kind}/${ref.id}`,ref);
  }
  return [...refs.values()];
}
export function resourceOwner(records, path) {
  return Object.values(records || {}).find(row => (row.files || []).some(file => file.path === path)) || null;
}
export function packageFields(manifest = {}) {
  return { ...Object.fromEntries(["slug","name","version","description","readme","license","changelog"].map(k => [k,manifest[k] || (k === "version" ? "1.0.0" : k === "license" ? "MIT" : "")] ).concat([["platforms",[...(manifest.platforms || [])]]])), ...(manifest.min_build_id ? {min_build_id:manifest.min_build_id} : {}) };
}
