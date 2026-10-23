// Portable context lives in the existing durable message, never browser storage.
// A strict, suffix-only envelope keeps ordinary Markdown/JSON untouched.
export const CONTEXT_START = "\n\n<!--mdo-context:v1-->\n";
const END = "\n<!--/mdo-context-->";
export const MAX_PROMPT_BYTES = 65535;
export const MAX_CONTEXTS = 16;
const TYPES = new Set(["file", "directory", "session", "quote", "text"]);
const bytes = text => new TextEncoder().encode(text).length;

export function validPromptContext(item) {
  return item && typeof item === "object" && !Array.isArray(item) &&
    Object.keys(item).every(key => ["id", "kind", "label", "content", "path", "project", "session", "event", "epoch"].includes(key)) &&
    /^[0-9a-f]{32}$/.test(item.id) && TYPES.has(item.kind) &&
    typeof item.label === "string" && item.label.length > 0 && bytes(item.label) <= 512 &&
    ["content", "path", "project", "session", "epoch"].every(key => item[key] === undefined ||
      (typeof item[key] === "string" && !item[key].includes("\0"))) &&
    (item.event === undefined || (Number.isSafeInteger(item.event) && item.event > 0)) &&
    (["file", "directory"].includes(item.kind) ? Boolean(item.path) && bytes(item.path) <= 4096 :
      typeof item.content === "string" && bytes(item.content) <= MAX_PROMPT_BYTES) &&
    !item.label.includes("\0");
}

export function parsePromptContext(value = "") {
  const text = String(value), at = text.lastIndexOf(CONTEXT_START);
  if (at < 0 || !text.endsWith(END)) return { text, contexts: [] };
  try {
    const document = JSON.parse(text.slice(at + CONTEXT_START.length, -END.length));
    if (Object.keys(document).length !== 1 || !Array.isArray(document.contexts) ||
        !document.contexts.length || document.contexts.length > MAX_CONTEXTS ||
        !document.contexts.every(validPromptContext) ||
        new Set(document.contexts.map(item => item.id)).size !== document.contexts.length) return { text, contexts: [] };
    return { text: text.slice(0, at), contexts: document.contexts };
  } catch { return { text, contexts: [] }; }
}

export function serializePromptContext(text, contexts = []) {
  if (!contexts.length) return text;
  if (contexts.length > MAX_CONTEXTS || !contexts.every(validPromptContext) ||
      new Set(contexts.map(item => item.id)).size !== contexts.length) throw new TypeError("Invalid prompt contexts");
  return `${text}${CONTEXT_START}${JSON.stringify({ contexts })}${END}`;
}

export function promptContextText(value) {
  const { text, contexts } = parsePromptContext(value);
  return [text, ...contexts.map(item => `${item.label}\n${item.content ?? `@${JSON.stringify(item.path)}`}`)].filter(Boolean).join("\n\n");
}

export function promptContextSummary(value) {
  const { text, contexts } = parsePromptContext(value);
  return [text, ...contexts.map(item => `[${item.label}]`)].filter(Boolean).join(" ");
}

export function newPromptContext(kind, label, properties = {}) {
  const data = crypto.getRandomValues(new Uint8Array(16));
  return { id: [...data].map(byte => byte.toString(16).padStart(2, "0")).join(""), kind, label, ...properties };
}

export function canAddPromptContext(text, contexts, next) {
  return validPromptContext(next) && contexts.length < MAX_CONTEXTS &&
    bytes(serializePromptContext(text, [...contexts, next])) <= MAX_PROMPT_BYTES;
}
