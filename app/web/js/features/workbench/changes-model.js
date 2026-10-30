import { t as uiText } from "../../i18n.js";
/** Keep source rows intact; pairing changes presentation, never line identity. */
export function pairDiffRows(rows) {
  const result = [];
  for (let at = 0; at < rows.length;) {
    const row = rows[at];
    if (row.kind === "removed" || row.kind === "added") {
      const removed = [], added = [];
      while (at < rows.length && rows[at].kind === "removed") removed.push(rows[at++]);
      while (at < rows.length && rows[at].kind === "added") added.push(rows[at++]);
      for (let i = 0; i < Math.max(removed.length, added.length); i++) result.push({ left: removed[i], right: added[i] });
    } else { result.push(row.kind === "context" ? { left: row, right: row } : { wide: row }); at++; }
  }
  return result;
}

export function changesTree(files, collapsed = new Set(), query = "") {
  const root = { children: new Map() }, result = [], match = query.trim().toLocaleLowerCase();
  for (const file of files) {
    if (match && !`${file.path} ${file.old_path || ""}`.toLocaleLowerCase().includes(match)) continue;
    const parts = file.path.split("/"); let parent = root, path = "";
    for (let at = 0; at < parts.length; at++) {
      path += (path ? "/" : "") + parts[at];
      if (!parent.children.has(parts[at])) parent.children.set(parts[at], { name: parts[at], path, children: new Map() });
      parent = parent.children.get(parts[at]); if (at === parts.length - 1) parent.file = file;
    }
  }
  const visit = (node, depth) => {
    const sorted = [...node.children.values()].sort((a, b) => Number(Boolean(a.file)) - Number(Boolean(b.file)) || a.name.localeCompare(b.name));
    for (const child of sorted) {
      result.push({ ...child, depth, directory: !child.file });
      if (!child.file && (match || !collapsed.has(child.path))) visit(child, depth + 1);
    }
  };
  visit(root, 0); return result;
}

export const changeStats = file => file.binary ? uiText("ui.changesModel.binary", {}, "二进制") : !file.stats_available ? uiText("ui.changesModel.notCounted", {}, "未统计") : `+${file.additions} −${file.deletions}`;
