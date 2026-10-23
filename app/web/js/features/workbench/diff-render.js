import { element as make } from "./controls.js";
import { sourceTokens } from "./source-view.js";
import { pairDiffRows } from "./changes-model.js";

/** One renderer preserves source identities in single-file and continuous views. */
export function renderDiffRows(target, page, { split, path, select }) {
  const cell = (row, side) => {
    const node = make("div", `wb-change-cell${row ? ` wb-change-${row.kind}` : " wb-change-blank"}`); if (!row) return node;
    node.dataset.offset = String(row.offset);
    const number = make("button", "wb-change-number", String(side === "old" ? row.old_line || "" : row.new_line || "")); number.type = "button";
    number.title = row.kind === "hunk" ? "选择此差异块（当前页）" : "选择差异行（Shift 扩展）";
    number.setAttribute("aria-label", row.kind === "hunk" ? "选择差异块" : `选择差异第 ${row.offset + 1} 行`); number.onclick = event => select(row, event.shiftKey);
    const code = make("span", "wb-change-code"), text = row.text ?? `[非 UTF-8 数据，Base64：${row.base64 || ""}]`;
    const content = ["context", "removed", "added"].includes(row.kind) ? text.slice(1) : text;
    for (const token of sourceTokens(content, path)) code.append(make("span", token.kind ? `wb-token-${token.kind}` : "", token.text));
    if (row.truncated) code.append(make("span", "wb-change-truncated", ` …（该行 ${row.bytes} 字节，显示前 16 KiB）`));
    node.append(number, code); return node;
  };
  for (const item of split ? pairDiffRows(page.rows) : page.rows.map(wide => ({ wide }))) {
    const row = make("div", `wb-change-row${item.wide ? " wb-change-wide" : ""}`);
    if (item.wide) { const node = cell(item.wide, "new"); node.prepend(make("span", "wb-change-old-number", String(item.wide.old_line || ""))); row.append(node); }
    else row.append(cell(item.left, "old"), cell(item.right, "new"));
    target.append(row);
  }
}

export function diffLabels(scope) {
  return [scope === "unstaged" ? "原版本：暂存区" : scope === "branch" ? "原版本：选定提交" : scope === "session" ? "原版本：会话基线" : "原版本：HEAD",
    scope === "staged" ? "新版本：暂存区" : scope === "branch" ? "新版本：HEAD" : "新版本：工作区"];
}

export function diffProvenance(state, path, page, rows, range) {
  return `仓库：${state.repository}\n文件：${path}\n比较范围：${range}\n${state.scope === "session" ? "会话基线树" : "基线提交"}：${state.base || "空仓库"}\n${state.scope === "session" ? "表示：原始工作区字节，显示启动后净变化，含其他程序的修改。\n" : ""}HEAD：${state.head || "空仓库"}\n比较修订：${page.revision}\n差异行：${rows[0]?.offset + 1}–${rows.at(-1)?.offset + 1}`;
}
