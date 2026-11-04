import { element } from "../../utils/dom.js";
import { setUiText, setUiAttribute, uiMessage, t } from "../../i18n.js";
import { createApplicationClient } from "../../api/application.js";
import { readOperation } from "../workbench/read-operation.js";
import { isTransientReadError } from "../../api/read-recovery.js";

// Stable start-event identity, never a native run ID (which can be reused).
export function appendTurnChanges(items) {
  const last = new Map();
  items.forEach((item, index) => {
    if (!(item.agentDepth > 0) && item.runEpoch > 0 && ["done", "failed", "cancelled"].includes(item.turnState))
      last.set(`${item.runKey}:${item.runEpoch}`, index);
  });
  const positions = new Set(last.values());
  return items.flatMap((item, index) => positions.has(index) ? [item, {
    key: `changes-${item.runEpoch}`, kind: "changes", turn: item.runEpoch,
    state: "done", time: item.time, turnState: item.turnState,
  }] : [item]);
}

export function createTurnChanges({ read = (context, turn, signal) => readOperation(
  createApplicationClient(() => ({ project: context.projectId, session: context.sessionId })),
  "changes.list", { scope: "turn", turn }, signal), open, change = fn => fn() } = {}) {
  const entries = new Map();
  let reading = 0;
  const waiting = [];
  // Loading older history can reveal many replies at once. Keep those reads
  // from exhausting the shared inspection quota used by the right sidebar.
  async function inspect(entry) {
    if (reading < 2) reading++; else await new Promise(resolve => waiting.push(resolve));
    try {
      if (entry.controller.signal.aborted) throw new DOMException("Inspection cancelled", "AbortError");
      return await read(entry.context, entry.item.turn, entry.controller.signal);
    } finally {
      const next = waiting.shift(); if (next) next(); else reading--;
    }
  }
  const keyFor = (item, context) => JSON.stringify([context.projectId, context.sessionId, item.turn]);
  const button = (message, action, className = "turn-changes-button") => {
    const node = element("button", { className, text: message, attrs: { type: "button" } });
    node.onclick = () => Promise.resolve().then(action).catch(error => {
      if (error.name !== "AbortError") { setUiText(node, uiMessage("turnChanges.openFailed", {}, "打开失败，点击重试")); setUiAttribute(node, "title", error.message); }
    });
    return node;
  };
  function render(entry, data) {
    const { root, context, item } = entry; root.hidden = !data.files?.length;
    if (root.hidden) return;
    const go = path => open?.({ ...context, turn: item.turn, path, repository: data.repository });
    const heading = element("div", { className: "turn-changes-heading" });
    const total = button(uiMessage("turnChanges.files", { count: data.files.length }, "已修改 {count} 个文件"), () => go());
    const stats = element("span", { className: "turn-changes-stats" }, [
      element("span", { className: "turn-changes-added", text: `+${data.additions}` }),
      element("span", { className: "turn-changes-removed", text: `−${data.deletions}` }),
    ]);
    const view = button(uiMessage("turnChanges.view", {}, "查看变更"), () => go());
    heading.append(total, stats, view);
    const list = element("div", { className: "turn-changes-files" });
    for (const file of data.files.slice(0, entry.expanded ? undefined : 3)) {
      const row = button(file.path, () => go(file.path), "turn-changes-file");
      row.replaceChildren(element("span", { className: "turn-changes-path", text: file.path }));
      setUiAttribute(row, "title", file.old_path ? `${file.old_path} → ${file.path}` : file.path);
      if (file.binary) row.append(element("span", { className: "turn-changes-file-stats", text: uiMessage("turnChanges.binary", {}, "二进制") }));
      else if (file.stats_available) row.append(element("span", { className: "turn-changes-file-stats" }, [
        element("span", { className: "turn-changes-added", text: `+${file.additions}` }),
        element("span", { className: "turn-changes-removed", text: `−${file.deletions}` }),
      ]));
      list.append(row);
    }
    root.replaceChildren(heading, list);
    if (data.files.length > 3) root.append(button(entry.expanded ? uiMessage("turnChanges.less", {}, "收起文件") :
      uiMessage("turnChanges.more", { count: data.files.length - 3 }, "再显示 {count} 个文件"),
    () => change(() => { entry.expanded = !entry.expanded; render(entry, data); })));
    setUiAttribute(root, "aria-label", uiMessage("turnChanges.label", {}, "本轮文件变更"));
    setUiAttribute(root, "title", uiMessage("turnChanges.scope", {}, "本轮运行前后的工作区净变化，可能包含同期外部修改。"));
  }
  async function load(entry) {
    const { root, controller, item } = entry;
    try {
      for (let attempt = 0; ; attempt++) {
        try {
          const data = await inspect(entry);
          if (!controller.signal.aborted) change(() => { entry.data = data; render(entry, data); });
          return;
        } catch (error) {
          if (controller.signal.aborted) return;
          const missing = error.code === "git_turn_missing";
          // A terminal stream event precedes native settlement/capture. A
          // steering handoff must not flash a failure or permanently lose
          // the card just because its completed receipt is still publishing.
          if ((missing && attempt < 40 && Date.now() - Number(item.time) / 1000 < 30000) ||
              (!missing && attempt < 3 && isTransientReadError(error))) {
            await new Promise(resolve => { const timer = setTimeout(done, missing ? 750 : Math.min(1500, 250 * 2 ** attempt));
              function done() { clearTimeout(timer); controller.signal.removeEventListener("abort", done); resolve(); }
              controller.signal.addEventListener("abort", done, { once: true }); });
            if (controller.signal.aborted) return;
            continue;
          }
          if (missing) return; // Older replies have no invented comparison or backfill.
          if (["git_not_repository", "git_unavailable"].includes(error.code)) return;
          throw error;
        }
      }
    } catch (error) {
      if (controller.signal.aborted) return;
      change(() => { root.hidden = false; const retry = button(uiMessage("turnChanges.unavailable", {}, "本轮文件变更暂不可用，点击重试"), () => {
        root.hidden = true; return load(entry);
      }); setUiAttribute(retry, "title", error.message || error.code || ""); root.replaceChildren(retry); });
    }
  }
  return {
    node(item, context) {
      const key = keyFor(item, context); let entry = entries.get(key);
      if (!entry) {
        entry = { key, item, context, root: element("section", { className: "timeline-item turn-changes", attrs: { hidden: "" } }), controller: new AbortController(), expanded: false };
        entries.set(key, entry); void load(entry);
      } else if (entry.data) render(entry, entry.data);
      return entry.root;
    },
    retain(items, context) { const keys = new Set(items.filter(item => item.kind === "changes").map(item => keyFor(item, context)));
      for (const [key, entry] of entries) if (!keys.has(key)) { entry.controller.abort(); entries.delete(key); }
    },
    destroy() { for (const entry of entries.values()) entry.controller.abort(); entries.clear(); },
  };
}
