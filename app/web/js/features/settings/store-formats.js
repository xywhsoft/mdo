export const STORE_FORMAT = "mdo.extension.v1";
export const STORE_KINDS = ["agents", "subagents", "tools", "skills", "mcp", "commands"];
export const STORE_PLATFORMS = ["windows-x86_64", "android-arm64-v8a", "linux-x86_64", "macos-arm64"];
export const containsCode = resources => resources.some(r => r.kind === "tools" || r.kind.startsWith("c-"));

// Publication includes only explicitly selected resource files. Credentials
// are also redacted by the native exporter, before reaching this UI.
export function makeStorePackage(fields, resources) {
  if (!/^[a-z0-9][a-z0-9._-]{0,63}$/.test(fields.slug) || fields.slug.includes(".."))
    throw new Error("Invalid package ID");
  if (!/^[A-Za-z0-9][A-Za-z0-9._-]{0,31}$/.test(fields.version)) throw new Error("Invalid version");
  if (!fields.name.trim() || !fields.description.trim() || !fields.readme.trim() || !fields.license.trim())
    throw new Error("Name, description, documentation and license are required");
  if (!resources.length || resources.length > 16 || !fields.platforms.length ||
      fields.platforms.some(p => !STORE_PLATFORMS.includes(p))) throw new Error("Select resources and supported platforms");
  const result = { format: STORE_FORMAT, manifest: {
    slug: fields.slug, name: fields.name.trim(), version: fields.version,
    description: fields.description.trim(), readme: fields.readme.trim(),
    license: fields.license.trim(), platforms: fields.platforms,
    ...(fields.changelog?.trim() ? { changelog: fields.changelog.trim() } : {}),
  }, resources };
  if (new TextEncoder().encode(JSON.stringify(result)).length > 1024 * 1024)
    throw new Error("Package exceeds 1 MiB");
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
  return Object.fromEntries(["slug","name","version","description","readme","license","changelog"].map(k => [k,manifest[k] || (k === "version" ? "1.0.0" : k === "license" ? "MIT" : "")] ).concat([["platforms",[...(manifest.platforms || [])]]]));
}
