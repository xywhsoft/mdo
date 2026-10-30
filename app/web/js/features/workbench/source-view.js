import { setUiAttribute as writeUiAttribute, uiMessage } from "../../i18n.js";

import { element as make } from "./controls.js";

const languages = /\.(?:c|h|cc|cpp|hpp|js|mjs|cjs|ts|tsx|jsx|json|py|sh|bash|ps1|css|java|rs|go)$/i;
const tokens = /("(?:[^"\\]|\\.)*"|'(?:[^'\\]|\\.)*'|`(?:[^`\\]|\\.)*`|\/\/.*$|\b(?:const|let|var|function|return|if|else|for|while|switch|case|break|continue|class|import|export|from|async|await|try|catch|throw|new|true|false|null|undefined|static|void|int|char|bool|struct|typedef|enum|sizeof|include|def|self|None|True|False|public|private|protected|package|using)\b|\b\d+(?:\.\d+)?\b)/g;
export function sourceTokens(text, path) {
  if (!languages.test(path) || text.length > 4096) return [{ text }];
  const result = []; let offset = 0;
  for (const match of text.matchAll(tokens)) {
    if (match.index > offset) result.push({ text: text.slice(offset, match.index) });
    const value = match[0], kind = /^["'`]/.test(value) ? "string" : value.startsWith("//") ? "comment" : /^\d/.test(value) ? "number" : "keyword";
    result.push({ text: value, kind }); offset = match.index + value.length;
  }
  if (offset < text.length) result.push({ text: text.slice(offset) }); return result;
}
/** Fixed-height source rows bound DOM size even for very long log files. */
export function createSourceView(text, path, { signal, line = 1 } = {}) {
  const root = make("div", "wb-source"), canvas = make("div", "wb-source-canvas"), lines = text.split("\n");
  root.tabIndex = 0; writeUiAttribute(root, "aria-label", uiMessage("ui.sourceView.fileContentsValue1", { value1: path }, "文件内容：{value1}")); root.append(canvas);
  const totalHeight = lines.length * 20, physicalHeight = Math.min(16000000, totalHeight);
  canvas.style.height = `${physicalHeight}px`;
  canvas.style.minWidth = `${Math.min(2000000, 68 + lines.reduce((longest, value) => Math.max(longest, value.length), 1) * 8)}px`;
  const scale = () => Math.max(1, (totalHeight - root.clientHeight) / Math.max(1, physicalHeight - root.clientHeight));
  let selected = null, start = -1, end = -1;
  const render = () => {
    const logicalTop = root.scrollTop * scale(), first = Math.max(0, Math.floor(logicalTop / 20) - 12), last = Math.min(lines.length, first + Math.ceil((root.clientHeight || 600) / 20) + 24);
    if (first === start && last === end) return; start = first; end = last; canvas.replaceChildren();
    for (let at = first; at < last; at++) {
      const row = make("div", "wb-source-line"), number = make("button", "wb-source-number", String(at + 1)), code = make("span", "wb-source-text");
      row.dataset.line = String(at + 1); row.style.top = `${at * 20 - logicalTop + root.scrollTop}px`; number.type = "button"; writeUiAttribute(number, "aria-label", uiMessage("ui.sourceView.selectLineValue1", { value1: at + 1 }, "选择第 {value1} 行"));
      row.classList.toggle("wb-source-selected", Boolean(selected && at + 1 >= selected[0] && at + 1 <= selected[1]));
      number.onclick = event => { selected = event.shiftKey && selected ? [Math.min(selected[0], at + 1), Math.max(selected[0], at + 1)] : [at + 1, at + 1]; start = -1; render(); };
      for (const token of sourceTokens(lines[at], path)) code.append(make("span", token.kind ? `wb-token-${token.kind}` : "", token.text));
      row.append(number, code); canvas.append(row);
    }
  };
  root.onscroll = render; const observer = new ResizeObserver(render); observer.observe(root);
  const go = (value, mark = true) => { const target = Math.max(1, Math.min(lines.length, Number(value) || 1)); root.scrollTop = (target - 1) * 20 / scale(); selected = mark ? [target, target] : null; start = -1; render(); };
  const selection = () => {
    const range = document.getSelection();
    if (range?.toString() && root.contains(range.anchorNode) && root.contains(range.focusNode)) {
      const a = range.anchorNode.nodeType === 1 ? range.anchorNode : range.anchorNode.parentElement, b = range.focusNode.nodeType === 1 ? range.focusNode : range.focusNode.parentElement;
      const first = Number(a.closest("[data-line]")?.dataset.line || 1), last = Number(b.closest("[data-line]")?.dataset.line || first);
      return { text: range.toString(), start: Math.min(first, last), end: Math.max(first, last) };
    }
    return selected ? { text: lines.slice(selected[0] - 1, selected[1]).join("\n"), start: selected[0], end: selected[1] } : null;
  };
  const dispose = () => observer.disconnect(); signal?.addEventListener("abort", dispose, { once: true });
  render(); requestAnimationFrame(() => { if (!signal?.aborted) go(line, line > 1); });
  return { root, go, selection, dispose };
}
