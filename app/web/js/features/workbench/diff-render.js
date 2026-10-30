import { setUiAttribute as writeUiAttribute, t as uiText, uiMessage } from "../../i18n.js";

import { element as make } from "./controls.js";
import { sourceTokens } from "./source-view.js";
import { pairDiffRows } from "./changes-model.js";

/** One renderer preserves source identities in single-file and continuous views. */
export function renderDiffRows(target, page, { split, path, select }) {
  const cell = (row, side) => {
    const node = make("div", `wb-change-cell${row ? ` wb-change-${row.kind}` : " wb-change-blank"}`); if (!row) return node;
    node.dataset.offset = String(row.offset);
    const number = make("button", "wb-change-number", String(side === "old" ? row.old_line || "" : row.new_line || "")); number.type = "button";
    writeUiAttribute(number, "title", row.kind === "hunk" ? uiMessage("ui.diffRender.selectThisDiffBlockCurrentPage", {}, "选择此差异块（当前页）") : uiMessage("ui.diffRender.selectDiffLinesShiftToExtend", {}, "选择差异行（Shift 扩展）"));
    writeUiAttribute(number, "aria-label", row.kind === "hunk" ? uiMessage("ui.diffRender.selectDiffBlock", {}, "选择差异块") : uiMessage("ui.diffRender.selectDiffLineValue1", { value1: row.offset + 1 }, "选择差异第 {value1} 行")); number.onclick = event => select(row, event.shiftKey);
    const code = make("span", "wb-change-code"), text = row.text ?? uiText("ui.diffRender.nonUtf8DataBase64Value1", { value1: row.base64 || "" }, "[非 UTF-8 数据，Base64：{value1}]");
    const content = ["context", "removed", "added"].includes(row.kind) ? text.slice(1) : text;
    for (const token of sourceTokens(content, path)) code.append(make("span", token.kind ? `wb-token-${token.kind}` : "", token.text));
    if (row.truncated) code.append(make("span", "wb-change-truncated", uiMessage("ui.diffRender.lineIsValue1BytesShowingTheFirst16Kib", { value1: row.bytes }, " …（该行 {value1} 字节，显示前 16 KiB）")));
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
  return [scope === "unstaged" ? uiText("ui.diffRender.oldVersionIndex", {}, "原版本：暂存区") : scope === "branch" ? uiText("ui.diffRender.oldVersionSelectedCommit", {}, "原版本：选定提交") : scope === "session" ? uiText("ui.diffRender.oldVersionConversationBaseline", {}, "原版本：会话基线") : uiText("ui.diffRender.oldVersionHead", {}, "原版本：HEAD"),
    scope === "staged" ? uiText("ui.diffRender.newVersionIndex", {}, "新版本：暂存区") : scope === "branch" ? uiText("ui.diffRender.newVersionHead", {}, "新版本：HEAD") : uiText("ui.diffRender.newVersionWorkspace", {}, "新版本：工作区")];
}

export function diffProvenance(state, path, page, rows, range) {
  return uiText("ui.diffRender.repositoryValue1FileValue2ScopeValue3Value4Value5Value6", { value1: state.repository, value2: path, value3: range, value4: state.scope === "session" ? uiText("ui.diffRender.conversationBaselineTree", {}, "会话基线树") : uiText("ui.diffRender.baselineCommit", {}, "基线提交"), value5: state.base || uiText("ui.diffRender.emptyRepository", {}, "空仓库"), value6: state.scope === "session" ? uiText("ui.diffRender.representsOriginalWorkspaceBytesAndNetChangesSinceStartup", {}, "表示：原始工作区字节，显示启动后净变化，含其他程序的修改。\n") : "", value7: state.head || uiText("ui.diffRender.emptyRepository", {}, "空仓库"), value8: page.revision, value9: rows[0]?.offset + 1, value10: rows.at(-1)?.offset + 1 }, "仓库：{value1}\n文件：{value2}\n比较范围：{value3}\n{value4}：{value5}\n{value6}HEAD：{value7}\n比较修订：{value8}\n差异行：{value9}–{value10}");
}
