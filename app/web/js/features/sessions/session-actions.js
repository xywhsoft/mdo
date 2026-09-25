// Keep the sidebar and conversation header in sync as session states change.
export function sessionActionItems(session) {
  if (session.status === "trash")
    return [{ name: "restore", label: "恢复" }];

  const actions = [{ name: "rename", label: "重命名" }];
  if (session.status === "active") {
    actions.push(
      { name: "pin", label: session.pinned ? "取消置顶" : "置顶" },
      { name: "archive", label: "归档" },
      { name: "fork", label: "创建分支" },
      { name: "truncate", label: "截断历史", tone: "danger" },
      { name: "clear", label: "清空历史", tone: "danger" },
      { name: "export", label: "导出 Markdown" },
      { name: "export_json", label: "导出 JSON 备份" },
    );
  } else actions.push({ name: "unarchive", label: "移回进行中" });
  actions.push({ name: "trash", label: "移到回收站", tone: "danger" });
  return actions;
}
