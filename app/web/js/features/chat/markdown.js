import { element } from "../../utils/dom.js";
import { t } from "../../i18n.js";

const INLINE = /(`[^`\n]+`|\*\*[^*\n]+\*\*|~~[^~\n]+~~|\*[^*\n]+\*|!\[[^\]\n]*\]\([^\s)]+\)|\[[^\]\n]+\]\([^\s)]+\))/g;
const LIST = /^\s{0,3}([-*]|\d+[.)])\s+(.*)$/;
const HEADING = /^(#{1,4})\s+(.*)$/;

function safeLink(value) {
  try {
    const url = new URL(value);
    return url.protocol === "http:" || url.protocol === "https:" ? url.href : null;
  } catch { return null; }
}

function safeImage(value) {
  const remote = safeLink(value);
  if (remote) return remote;
  if (value.length > 1024 * 1024 ||
      !/^data:image\/(?:png|jpeg|gif|webp);base64,[A-Za-z0-9+/]+={0,2}$/i.test(value))
    return null;
  return value;
}

function inline(text) {
  const nodes = [];
  let start = 0;
  for (const match of text.matchAll(INLINE)) {
    if (match.index > start) nodes.push(document.createTextNode(text.slice(start, match.index)));
    const token = match[0];
    let node = null;
    if (token.startsWith("`")) node = element("code", { text: token.slice(1, -1) });
    else if (token.startsWith("**")) node = element("strong", { text: token.slice(2, -2) });
    else if (token.startsWith("~~")) node = element("del", { text: token.slice(2, -2) });
    else if (token.startsWith("*")) node = element("em", { text: token.slice(1, -1) });
    else if (token.startsWith("![")) {
      const bracket = token.indexOf("](");
      const alt = token.slice(2, bracket);
      const src = safeImage(token.slice(bracket + 2, -1));
      if (src) node = element("button", { className: "md-image-preview",
        attrs: { type: "button", "data-image-preview": "",
          "aria-label": t("markdown.viewImage", { alt: alt || t("markdown.image", {}, "Markdown 图片") },
            `查看图片：${alt || "Markdown 图片"}`) },
      }, [element("img", { attrs: { src, alt, loading: "lazy",
        decoding: "async", referrerpolicy: "no-referrer" } })]);
    }
    else if (token.startsWith("[")) {
      const bracket = token.indexOf("](");
      const label = token.slice(1, bracket);
      const href = safeLink(token.slice(bracket + 2, -1));
      if (href) node = element("a", { text: label,
        attrs: { href, target: "_blank", rel: "noopener noreferrer" } });
    }
    nodes.push(node ?? document.createTextNode(token));
    start = match.index + token.length;
  }
  if (start < text.length) nodes.push(document.createTextNode(text.slice(start)));
  return nodes;
}

function appendInline(parent, text) { parent.append(...inline(text)); }

function blockStart(line) {
  return /^```/.test(line) || HEADING.test(line) || LIST.test(line) ||
    /^>\s?/.test(line) || /^\s*(-{3,}|\*{3,})\s*$/.test(line);
}

function cells(line) {
  return line.trim().replace(/^\||\|$/g, "").split("|").map((part) => part.trim());
}

export function renderMarkdown(source) {
  const lines = String(source ?? "").replace(/\r\n?/g, "\n").split("\n");
  const output = document.createDocumentFragment();
  let index = 0;
  while (index < lines.length) {
    const line = lines[index];
    if (!line.trim()) { index++; continue; }
    const fence = /^```([\w+-]*)\s*$/.exec(line);
    if (fence) {
      const code = [];
      index++;
      while (index < lines.length && !/^```\s*$/.test(lines[index]))
        code.push(lines[index++]);
      if (index < lines.length) index++;
      const block = element("div", { className: "md-codeblock" });
      const head = element("div", { className: "md-code-head" });
      head.append(element("span", { text: fence[1] || "text" }));
      const copy = element("button", { text: t("markdown.copyCode", {}, "复制代码"),
        attrs: { type: "button" } });
      copy.addEventListener("click", async () => {
        try { await navigator.clipboard.writeText(code.join("\n"));
          copy.textContent = t("markdown.copied", {}, "已复制"); }
        catch { copy.textContent = t("markdown.copyFailed", {}, "复制失败"); }
      });
      head.append(copy);
      block.append(head, element("pre", {}, [element("code", { text: code.join("\n") })]));
      output.append(block);
      continue;
    }
    const heading = HEADING.exec(line);
    if (heading) {
      const title = element(`h${heading[1].length}`);
      appendInline(title, heading[2]);
      output.append(title);
      index++;
      continue;
    }
    if (/^\s*(-{3,}|\*{3,})\s*$/.test(line)) {
      output.append(element("hr")); index++; continue;
    }
    if (/^>\s?/.test(line)) {
      const quote = [];
      while (index < lines.length && /^>\s?/.test(lines[index]))
        quote.push(lines[index++].replace(/^>\s?/, ""));
      const block = element("blockquote");
      appendInline(block, quote.join(" "));
      output.append(block);
      continue;
    }
    if (/^\s*\|.*\|\s*$/.test(line) &&
        /^\s*\|[\s:|-]+\|\s*$/.test(lines[index + 1] ?? "")) {
      const table = element("table");
      const head = element("tr");
      for (const value of cells(line)) {
        const cell = element("th"); appendInline(cell, value); head.append(cell);
      }
      table.append(element("thead", {}, [head]));
      index += 2;
      const body = element("tbody");
      while (index < lines.length && /^\s*\|.*\|\s*$/.test(lines[index])) {
        const row = element("tr");
        for (const value of cells(lines[index++])) {
          const cell = element("td"); appendInline(cell, value); row.append(cell);
        }
        body.append(row);
      }
      table.append(body);
      output.append(element("div", { className: "md-table-wrap" }, [table]));
      continue;
    }
    const list = LIST.exec(line);
    if (list) {
      const ordered = /\d/.test(list[1][0]);
      const group = element(ordered ? "ol" : "ul");
      while (index < lines.length) {
        const item = LIST.exec(lines[index]);
        if (!item || /\d/.test(item[1][0]) !== ordered) break;
        index++;
        const li = element("li");
        const task = /^\[([ xX])\]\s+(.*)$/.exec(item[2]);
        if (task) li.append(element("input", { attrs: {
          type: "checkbox", disabled: "", ...(task[1].toLowerCase() === "x" ? { checked: "" } : {}),
        } }));
        appendInline(li, task ? task[2] : item[2]);
        group.append(li);
      }
      output.append(group);
      continue;
    }
    const paragraph = [line];
    index++;
    while (index < lines.length && lines[index].trim() &&
           !blockStart(lines[index]) &&
           !(/^\s*\|.*\|\s*$/.test(lines[index]) &&
             /^\s*\|[\s:|-]+\|\s*$/.test(lines[index + 1] ?? "")))
      paragraph.push(lines[index++]);
    const p = element("p");
    appendInline(p, paragraph.join(" "));
    output.append(p);
  }
  return output;
}
