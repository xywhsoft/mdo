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
  actions.push(item("import_json", "action.importJson", "导入会话备份"),
    item("trash", "action.trash", "移到回收站", "danger"));
  return actions;
}

export function sessionActionDialogCopy(action, session) {
  const title = session.title || t("sessionAction.untitled", {}, "未命名任务");
  const copy = {
    rename: [
      t("sessionAction.renameTitle", {}, "重命名会话"),
      t("sessionAction.renameDescription", {}, "新标题会同步写入会话元数据。"),
      t("sessionAction.save", {}, "保存"),
    ],
    trash: [
      t("sessionAction.trashTitle", {}, "移到回收站"),
      t("sessionAction.trashDescription", { title }, `“${title}”可从回收站恢复。`),
      t("sessionAction.trashConfirm", {}, "移到回收站"),
    ],
    fork: [
      t("sessionAction.forkTitle", {}, "创建会话分支"),
      t("sessionAction.forkDescription", {}, "从指定消息序列创建独立会话。原会话不会改变。"),
      t("sessionAction.forkConfirm", {}, "创建分支"),
    ],
    truncate: [
      t("sessionAction.truncateTitle", {}, "截断会话历史"),
      t("sessionAction.truncateDescription", {}, "指定序列之后的模型账本将被永久移除。"),
      t("sessionAction.truncateConfirm", {}, "截断历史"),
    ],
    clear: [
      t("sessionAction.clearTitle", {}, "清空会话历史"),
      t("sessionAction.clearDescription", {}, "模型账本将被永久清空，并重新注入当前系统提示词。"),
      t("sessionAction.clearConfirm", {}, "清空历史"),
    ],
  }[action];
  if (!copy) throw new TypeError("unknown session action");
  return copy;
}

export function sessionForkTitle(session) {
  const title = session.title || t("sessionAction.untitled", {}, "未命名任务");
  return t("sessionAction.defaultForkTitle", { title }, `${title}（分支）`);
}

export function sessionActionToast(action, session) {
  const copy = {
    rename: ["sessionAction.renamed", "会话已重命名"],
    pin: session.pinned ? ["sessionAction.pinned", "会话已置顶"] :
      ["sessionAction.unpinned", "已取消置顶"],
    archive: ["sessionAction.archived", "会话已归档"],
    unarchive: ["sessionAction.unarchived", "会话已移回进行中"],
    trash: ["sessionAction.trashed", "会话已移到回收站"],
    restore: ["sessionAction.restored", "会话已恢复"],
    fork: ["sessionAction.forked", "已创建会话分支"],
    truncate: ["sessionAction.truncated", "会话历史已截断"],
    clear: ["sessionAction.cleared", "会话历史已清空"],
  }[action];
  if (!copy) throw new TypeError("unknown session action");
  return t(copy[0], {}, copy[1]);
}
