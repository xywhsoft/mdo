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
  }, resources };
  if (new TextEncoder().encode(JSON.stringify(result)).length > 1024 * 1024)
    throw new Error("Package exceeds 1 MiB");
  return result;
}
