// These editors preserve unknown frontmatter. This is a bounded field editor,
// not a YAML engine: native validation remains authoritative for imported files.
export const EXTENSION_KINDS = Object.freeze(["agents", "subagents", "tools", "skills", "mcp", "commands"]);
export const BUILTIN_COMMAND_NAMES = Object.freeze(["new", "model", "fork", "export", "clear", "stop", "settings", "theme", "help"]);
export function portableId(id) {
  return /^[a-z0-9_-][a-z0-9._-]{0,63}$/.test(id) && !id.endsWith(".") && !id.includes("..") &&
    !/^(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\.|$)/.test(id);
}
export function splitPrompt(source) {
  const text = String(source).replace(/^\uFEFF/, "").replace(/\r\n/g, "\n");
  if (!text.startsWith("---\n")) return { header: "", body: text };
  const end = text.indexOf("\n---", 4);
  if (end < 0 || !/^\n---(?:\n|$)/.test(text.slice(end))) throw new Error("Unclosed frontmatter");
  return { header: text.slice(4, end), body: text.slice(end + 4).replace(/^\n/, "") };
}
function scalar(text) {
  try { return JSON.parse(text); } catch { return text.replace(/^'(.*)'$/, "$1").replace(/''/g, "'"); }
}
export function promptFields(source) {
  const { header, body } = splitPrompt(source);
  const fields = { prompt: body };
  const lines = header.split("\n");
  for (let i = 0; i < lines.length; i += 1) {
    const match = /^([\w-]+):\s*(.*)$/.exec(lines[i]);
    if (!match) continue;
    let value = match[2];
    if (value === "|" || value === ">") {
      const parts = [];
      while (i + 1 < lines.length && (/^\s+/.test(lines[i + 1]) || !lines[i + 1])) parts.push(lines[++i].trim());
      value = parts.join(value === ">" ? " " : "\n");
    }
    if (!value && /^\s+-\s+/.test(lines[i + 1] || "")) {
      const list = [];
      while (i + 1 < lines.length && /^\s+-\s+/.test(lines[i + 1])) list.push(scalar(lines[++i].replace(/^\s+-\s+/, "")));
      fields[match[1]] = list;
    } else fields[match[1]] = scalar(value);
  }
  return fields;
}
export function editPrompt(source, fields) {
  const { header } = splitPrompt(source);
  const lines = header ? header.split("\n") : [];
  const keys = new Set(Object.keys(fields).filter((key) => key !== "prompt"));
  const kept = [];
  for (let i = 0; i < lines.length; i += 1) {
    const key = /^([\w-]+):/.exec(lines[i])?.[1];
    if (!keys.has(key)) { kept.push(lines[i]); continue; }
    while (i + 1 < lines.length && (/^\s+/.test(lines[i + 1]) || !lines[i + 1])) i += 1;
  }
  for (const key of keys) {
    const value = fields[key];
    if (value !== "" && value !== undefined) kept.push(`${key}: ${JSON.stringify(value)}`);
  }
  return `---\n${kept.join("\n")}\n---\n${fields.prompt ?? splitPrompt(source).body}`;
}
export function newMcp(id = "server") {
  return { schema_version: 1, id, name: id, description: "MCP server", enabled: true,
    transport: { type: "stdio", program: "", arguments: [], working_directory: null,
      inherit_environment: true, environment: [] }, protocol_version: "2025-11-25",
    startup_timeout_ms: 10000, request_timeout_ms: 60000,
    limits: { message_bytes: 1048576, tools: 128 }, tools: { allow: [], deny: [] },
    security: { default_effects: ["external-service"], permission_profile: "balanced", trust_read_only_annotations: false },
    auto_reconnect: true };
}
// Forms accept either ordinary credential maps or native reference arrays.
// Convert maps only at save time so editing a key remains a visible draft change.
export function prepareMcpCredentials(document, inputs = []) {
  const doc = structuredClone(document), secrets = [...inputs];
  for (const key of ["environment", "headers"]) {
    const values = doc.transport?.[key];
    if (values == null || Array.isArray(values)) continue;
    if (typeof values !== "object") throw new Error(`Invalid MCP ${key}`);
    doc.transport[key] = Object.entries(values).map(([name, value]) => {
      if (typeof value !== "string" || !value || value.includes("\0")) throw new Error("Invalid MCP credential");
      const secret_ref = `input:${secrets.length}`; secrets.push(value);
      return { name, secret_ref };
    });
  }
  return { content: JSON.stringify(doc, null, 2), secrets };
}
export function mcpImportDocuments(text) {
  const root = JSON.parse(text);
  if (root.schema_version === 1 && root.transport) return [{ id: root.id, content: JSON.stringify(root, null, 2), secrets: [] }];
  const servers = root.mcpServers ?? root;
  if (!servers || typeof servers !== "object" || Array.isArray(servers)) throw new Error("Expected mcpServers object");
  const entries = Object.entries(servers);
  if (!entries.length || entries.length > 32) throw new Error("Import 1–32 MCP servers at a time");
  return entries.map(([id, server]) => {
    if (!portableId(id) || !server || typeof server !== "object") throw new Error(`Invalid MCP server: ${id}`);
    const doc = newMcp(id), secrets = [];
    const reference = value => {
      if (typeof value !== "string" || !value || value.includes("\0")) throw new Error("Invalid MCP credential");
      // The native endpoint seals inputs and returns only opaque references.
      const ref = `input:${secrets.length}`; secrets.push(value); return ref;
    };
    if (server.url) {
      if (server.type === "sse") throw new Error("Legacy SSE is unsupported; use Streamable HTTP");
      if (new URL(server.url).protocol !== "https:") throw new Error("MCP HTTP endpoint must use HTTPS");
      doc.transport = { type: "streamable-http", endpoint: server.url,
        headers: Object.entries(server.headers ?? {}).map(([name, value]) => ({ name, secret_ref: reference(value) })) };
    } else {
      if (typeof server.command !== "string" || !server.command ||
          !Array.isArray(server.args ?? []) || !(server.args ?? []).every(value => typeof value === "string")) throw new Error(`Invalid command/args: ${id}`);
      doc.transport.program = server.command; doc.transport.arguments = server.args ?? [];
      doc.transport.working_directory = server.cwd ?? null;
      doc.transport.environment = Object.entries(server.env ?? {}).map(([name, value]) => ({ name, secret_ref: reference(value) }));
    }
    doc.enabled = !server.disabled;
    return { id, content: JSON.stringify(doc, null, 2), secrets };
  });
}
export function expandCommand(prompt, args) {
  const text = String(prompt);
  return text.includes("$ARGUMENTS") ? text.replaceAll("$ARGUMENTS", () => String(args)) :
    args ? `${text}\n\n${args}` : text;
}
export function bytesToBase64(bytes) {
  let binary = "";
  for (let offset = 0; offset < bytes.length; offset += 8192) binary += String.fromCharCode(...bytes.subarray(offset, offset + 8192));
  return btoa(binary);
}
