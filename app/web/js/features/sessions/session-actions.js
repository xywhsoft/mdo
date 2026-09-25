import { t } from "../../i18n.js";

// Keep the sidebar and conversation header in sync as session states change.
export function sessionActionItems(session) {
  const item = (name, key, fallback, tone) =>
    ({ name, label: t(key, {}, fallback), ...(tone ? { tone } : {}) });
  if (session.status === "trash")
    return [item("restore", "action.restore", "恢复")];

  const actions = [item("rename", "action.rename", "重命名")];
  if (session.status === "active") {
    actions.push(
      session.pinned ? item("pin", "action.unpin", "取消置顶") :
        item("pin", "action.pin", "置顶"),
      item("archive", "action.archive", "归档"),
      item("fork", "action.fork", "创建分支"),
      item("truncate", "action.truncate", "截断历史", "danger"),
      item("clear", "action.clear", "清空历史", "danger"),
      item("export", "action.export", "导出 Markdown"),
      item("export_json", "action.exportJson", "导出 JSON 备份"),
    );
  } else actions.push(item("unarchive", "action.unarchive", "移回进行中"));
  actions.push(item("trash", "action.trash", "移到回收站", "danger"));
  return actions;
}
