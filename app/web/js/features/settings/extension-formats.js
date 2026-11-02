import { t as uiErrorText } from "../../i18n.js";
// These editors preserve unknown frontmatter. This is a bounded field editor,
// not a YAML engine: native validation remains authoritative for imported files.
export const EXTENSION_KINDS = Object.freeze(["agents", "subagents", "tools", "skills", "mcp", "commands", "knowledge"]);
export function mcpHttpEndpointValid(value) {
  const text = String(value ?? "");
  if (!text || text.length > 4096 || /[\x00-\x20\x7f\\]/.test(text) || text.includes("#")) return false;
  let url;
  try { url = new URL(text); } catch { return false; }
  if (!url.hostname || url.username || url.password || url.port === "0") return false;
  const authority = text.match(/^https?:\/\/([^/?#]+)/i)?.[1];
  if (!authority || authority.includes("@") || /[^\x21-\x7e]/.test(authority)) return false;
  if (url.protocol === "https:") return true;
  if (url.protocol !== "http:") return false;
  // Inspect the original authority: URL() canonicalizes e.g. 127.1 and integer
  // IPv4 hosts. Accept only the same explicit loopback spellings as the host.
  return /^(?:localhost|127\.0\.0\.1|\[::1\])(?::[0-9]+)?$/i.test(authority ?? "");
}
export function portableId(id) {
  return /^[A-Za-z0-9_-][A-Za-z0-9._-]{0,63}$/.test(id) && !id.endsWith(".") && !id.includes("..") &&
    !/^(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\.|$)/i.test(id);
}
export function splitPrompt(source) {
  const text = String(source).replace(/^\uFEFF/, "").replace(/\r\n/g, "\n");
  if (!text.startsWith("---\n")) return { header: "", body: text };
  const end = text.indexOf("\n---", 4);
  if (end < 0 || !/^\n---(?:\n|$)/.test(text.slice(end))) throw new Error(uiErrorText("error.detail.unclosedFrontmatter", {}, "Unclosed frontmatter"));
  return { header: text.slice(4, end), body: text.slice(end + 4).replace(/^\n/, "") };
}
function scalar(text) {
  try { return JSON.parse(text); } catch { return text.replace(/^'(.*)'$/, "$1").replace(/''/g, "'"); }
}
export function promptFields(source) {
  if (String(source).trimStart().startsWith("{")) {
    try { const doc = JSON.parse(source); if (doc?.schema === "mdo.agent.v1") return { ...doc, prompt: doc.prompt || "" }; } catch { /* report malformed sources through the source editor */ }
  }
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
  if (String(source).trimStart().startsWith("{")) {
    const doc = JSON.parse(source);
    if (doc?.schema === "mdo.agent.v1") {
      for (const [key,value] of Object.entries(fields)) {
        if (value === undefined || value === "" && key !== "prompt") delete doc[key]; else doc[key] = value;
      }
      return JSON.stringify(doc, null, 2);
    }
  }
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
