import { element, errorMessage, toast } from "../../utils/dom.js";
import { t } from "../../i18n.js";
import { copyText } from "../../utils/clipboard.js";
import { sourceTokens } from "../workbench/source-view.js";
import { parseMarkdown } from "./markdown-transport.js";
import { markdownAddress, markdownResourceUrl, markdownImage, openMarkdownFile, nativeExternalLink } from "./markdown-assets.js";

// Entity decoding is restricted to individual character references, never markup.
const entities = new Map();
export function decodeMarkdownEntities(value) {
  return String(value || "").replace(/&(?:#[0-9]{1,8}|#x[0-9a-f]{1,8}|[a-z][a-z0-9]{1,31});/gi, entity => {
    if (!entities.has(entity)) {
      if (entities.size >= 256) entities.clear();
      entities.set(entity, new DOMParser().parseFromString(entity, "text/html").body.textContent);
    }
    return entities.get(entity);
  });
}
const textOf = nodes => (nodes || []).map(node => node.t === "entity" ? decodeMarkdownEntities(node.s) : node.s ?? textOf(node.c)).join("");
const tags = new Set(["p", "blockquote", "ul", "ol", "li", "hr", "table", "thead", "tbody", "tr", "th", "td", "strong", "em", "del", "code", "br"]);
function reusable(root, selector, key) {
  const pool = new Map();
  for (const node of root?.querySelectorAll(selector) || []) {
    const id = key(node); if (!pool.has(id)) pool.set(id, []); pool.get(id).push(node);
  }
  return id => pool.get(id)?.shift();
}
function codeBlock(node, takeCode) {
  const code = textOf(node.c), language = decodeMarkdownEntities(node.lang).trim().split(/\s+/)[0];
  const previous = takeCode(JSON.stringify([language, code]));
  if (previous) return previous;
  const block = element("div", { className: "md-codeblock", attrs: { "data-md-code": JSON.stringify([language, code]) } });
  const copy = element("button", { text: t("markdown.copyCode"), attrs: { type: "button" } });
  copy.addEventListener("click", async () => {
    try { await copyText(code); copy.textContent = t("markdown.copied"); }
    catch { copy.textContent = t("markdown.copyFailed"); }
    setTimeout(() => { if (copy.isConnected) copy.textContent = t("markdown.copyCode"); }, 1200);
  });
  const header = element("div", { className: "md-code-head" }, [element("span", { text: language || "text" }), copy]);
  const content = element("code");
  const aliases = { javascript: "js", typescript: "ts", python: "py", shell: "sh", powershell: "ps1", csharp: "cs", cpp: "cpp" };
  if (code.length <= 65536) {
    const lines = code.split("\n");
    for (const [index, line] of lines.entries()) {
      for (const token of sourceTokens(line, `code.${aliases[language] || language}`))
        content.append(token.kind ? element("span", { className: `md-token-${token.kind}`, text: token.text }) : document.createTextNode(token.text));
      if (index < lines.length - 1) content.append(document.createTextNode("\n"));
    }
  } else content.textContent = code;
  block.append(header, element("pre", {}, [content])); return block;
}

export function renderMarkdownNodes(nodes, { previous, owner, signal, onLayout, onOpenFile } = {}) {
  const takeImage = reusable(previous, "[data-md-image-key]", node => node.dataset.mdImageKey);
  const takeCode = reusable(previous, "[data-md-code]", node => node.dataset.mdCode);
  const headings = new Map(); let count = 0;
  function build(node, depth = 0, inLink = false) {
    if (!node || ++count > 32768 || depth > 96) throw new Error("Invalid Markdown node budget");
    if (node.t === "text" || node.t === "entity") return document.createTextNode(node.t === "entity" ? decodeMarkdownEntities(node.s) : String(node.s || ""));
    if (node.t === "codeblock") return codeBlock(node, takeCode);
    if (node.t === "img") {
      const url = decodeMarkdownEntities(node.url), alt = textOf(node.c), title = decodeMarkdownEntities(node.title);
      const address = markdownAddress(url, true);
      const src = address?.kind === "file" ? markdownResourceUrl(owner, address.path) : address?.url;
      const key = JSON.stringify([src, alt, title, inLink]);
      return takeImage(key) || markdownImage(address, alt, title, owner, { inLink, onLayout });
    }
    if (node.t === "a") {
      const address = markdownAddress(decodeMarkdownEntities(node.url));
      const href = address?.kind === "file" ? markdownResourceUrl(owner, address.path, "file") : address?.kind === "anchor" ? `#${address.url}` : address?.url;
      const link = element(href ? "a" : "span", { attrs: href ? { href, title: decodeMarkdownEntities(node.title),
        ...(address.kind === "web" ? { target: "_blank", rel: "noopener noreferrer" } : {}) } : {} });
      for (const child of node.c || []) link.append(build(child, depth + 1, true));
      if (address?.kind === "web") link.addEventListener("click", event => nativeExternalLink(event, address.url));
      if (address?.kind === "file" && href) link.addEventListener("click", event => {
        event.preventDefault(); if (signal?.aborted) return;
        openMarkdownFile(address, owner, { signal, onOpenFile }).catch(error => { if (error.name !== "AbortError") toast(errorMessage(error), "error"); });
      });
      if (address?.kind === "anchor") link.addEventListener("click", event => {
        event.preventDefault(); let key; try { key = decodeURIComponent(address.url); } catch { return; }
        headings.get(key)?.scrollIntoView({ block: "start" });
      });
      return link;
    }
    const tag = node.t === "heading" && Number.isInteger(node.level) && node.level >= 1 && node.level <= 6 ? `h${node.level}` : tags.has(node.t) ? node.t : "span";
    const result = element(tag);
    if (node.t === "ol" && Number.isSafeInteger(node.start) && node.start > 0) result.start = node.start;
    if ((node.t === "ol" || node.t === "ul") && node.tight) result.classList.add("md-tight-list");
    if ((node.t === "th" || node.t === "td") && ["left", "center", "right"].includes(node.align)) result.style.textAlign = node.align;
    if (node.t === "li" && ["checked", "unchecked"].includes(node.task)) result.append(element("input", { attrs: { type: "checkbox", disabled: "", ...(node.task === "checked" ? { checked: "" } : {}) } }));
    for (const child of node.c || []) result.append(build(child, depth + 1, inLink));
    if (node.t === "heading") {
      const id = textOf(node.c).trim().toLowerCase().replace(/[^\p{L}\p{N}\s_-]/gu, "").replace(/\s+/g, "-");
      if (!headings.has(id)) headings.set(id, result);
    }
    return node.t === "table" ? element("div", { className: "md-table-wrap" }, [result]) : result;
  }
  const fragment = document.createDocumentFragment();
  for (const node of nodes) fragment.append(build(node)); return fragment;
}

export function createMarkdownRenderer({ parse = parseMarkdown, onLayout = () => {}, decorate = () => {}, onOpenFile,
  delay = 120, concurrency = 2, timeoutMs = 8000 } = {}) {
  const entries = new Map(), cache = new Map(), waiting = new Set(); let running = 0, stopped = false, cacheBytes = 0;
  function drain() {
    for (const entry of waiting) {
      if (running >= concurrency) break;
      waiting.delete(entry); if (entry.dead || entry.inflight) continue;
      void request(entry);
    }
  }
  function schedule(entry, immediate = false) {
    if (entry.dead || stopped || entry.inflight) return;
    if (entry.timer) { if (!immediate) return; clearTimeout(entry.timer); }
    entry.timer = setTimeout(() => { entry.timer = 0; waiting.add(entry); drain(); }, immediate ? 0 : delay);
  }
  function paint(entry, tree, source) {
    const active = document.activeElement, hadFocus = active && entry.blocks.contains(active);
    const focusedAction = hadFocus ? active.dataset?.timelineAction : "";
    const fragment = renderMarkdownNodes(tree.nodes, { previous: entry.blocks, owner: entry.owner,
      signal: entry.life.signal, onLayout, onOpenFile });
    entry.blocks.replaceChildren(fragment);
    entry.tail.textContent = entry.source.startsWith(source) ? entry.source.slice(source.length) : "";
    entry.parsed = source; entry.tree = tree; decorate(entry.root, entry);
    if (hadFocus && !entry.blocks.contains(document.activeElement)) {
      const replacement = active.isConnected ? active : focusedAction &&
        [...entry.blocks.querySelectorAll("[data-timeline-action]")].find(node => node.dataset.timelineAction === focusedAction);
      replacement?.focus({ preventScroll: true });
    }
    onLayout();
  }
  async function request(entry) {
    running++; entry.inflight = true;
    const source = entry.source, version = entry.version, cancel = new AbortController(); entry.cancel = cancel;
    const timeout = setTimeout(() => cancel.abort(), timeoutMs);
    try {
      // A transport may ignore abort. Own its deadline as well so it cannot
      // permanently occupy a parsing slot or overwrite a newer reply later.
      const tree = cache.get(source)?.tree || await new Promise((resolve, reject) => {
        const aborted = () => reject(new DOMException("Markdown request cancelled", "AbortError"));
        cancel.signal.addEventListener("abort", aborted, { once: true });
        if (cancel.signal.aborted) return aborted();
        Promise.resolve().then(() => parse(source, { signal: cancel.signal }))
          .then(resolve, reject).finally(() => cancel.signal.removeEventListener("abort", aborted));
      });
      if (entry.dead || stopped || entry.version !== version) return;
      if (tree?.schema_version !== 1 || tree.engine !== "md4c" || !Array.isArray(tree.nodes)) throw new Error("Invalid Markdown response");
      paint(entry, tree, source);
      if (entry.final && !cache.has(source)) {
        const bytes = source.length + JSON.stringify(tree).length;
        if (bytes <= 4 * 1024 * 1024) {
          cache.set(source, { tree, bytes }); cacheBytes += bytes;
          while (cache.size > 32 || cacheBytes > 4 * 1024 * 1024) { const key = cache.keys().next().value; cacheBytes -= cache.get(key).bytes; cache.delete(key); }
        }
      }
    } catch (error) {
      if (!entry.dead && entry.version === version) {
        // Keep every reply readable if the parser/transport rejects a budget.
        if (!entry.tree) entry.tail.textContent = entry.source;
      }
    } finally {
      clearTimeout(timeout); running--; entry.inflight = false;
      if (!entry.dead && entry.version !== version) schedule(entry, entry.final);
      drain();
    }
  }
  function remove(entry) { entry.dead = true; clearTimeout(entry.timer); waiting.delete(entry); entry.cancel?.abort(); entry.life.abort(); }
  return {
    render(source, { key, owner, itemKey = key, final = false }) {
      let entry = entries.get(key);
      if (!entry) {
        const blocks = element("div", { className: "md-blocks" }), tail = element("span", { className: "md-pending" });
        entry = { key, owner, itemKey, blocks, tail, root: element("div", { className: "md-content" }, [blocks, tail]),
          source: null, parsed: "", version: 0, life: new AbortController() }; entries.set(key, entry);
      }
      entry.final = final;
      if (entry.source !== source) {
        entry.source = String(source || ""); entry.version++;
        if (entry.parsed && entry.source.startsWith(entry.parsed)) entry.tail.textContent = entry.source.slice(entry.parsed.length);
        else { entry.blocks.replaceChildren(); entry.tree = null; entry.parsed = ""; entry.tail.textContent = entry.source; }
        schedule(entry, final || !entry.parsed);
      }
      return entry.root;
    },
    retain(keys) { for (const [key, entry] of entries) if (!keys.has(key)) { remove(entry); entries.delete(key); } },
    relocalize() {
      for (const entry of entries.values()) {
        if (entry.tree) paint(entry, entry.tree, entry.parsed);
        for (const button of entry.blocks.querySelectorAll(".md-code-head button")) button.textContent = t("markdown.copyCode");
        for (const image of entry.blocks.querySelectorAll(".md-image")) {
          image.querySelector(".md-image-retry").textContent = t("image.previewRetry");
          const preview = image.querySelector("button.md-image-preview");
          if (preview) preview.setAttribute("aria-label", t("markdown.viewImage", { alt: image.querySelector("img").alt || t("markdown.image") }));
          const status = image.querySelector(".md-image-status");
          if (status.textContent) status.textContent = t("image.previewFailed");
        }
      }
    },
    destroy() { stopped = true; for (const entry of entries.values()) remove(entry); entries.clear(); cache.clear(); waiting.clear(); },
  };
}
