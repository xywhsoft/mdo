export function element(tag, options = {}, children = []) {
  const node = document.createElement(tag);
  if (options.className) node.className = options.className;
  if (options.text !== undefined) node.textContent = String(options.text);
  for (const [name, value] of Object.entries(options.attrs ?? {})) {
    if (value !== null && value !== undefined) node.setAttribute(name, String(value));
  }
  for (const child of children) {
    if (child !== null && child !== undefined) node.append(child);
  }
  return node;
}

export function clear(node) {
  node.replaceChildren();
}

export function formatRelativeTime(microseconds) {
  const time = Number(microseconds) / 1000;
  if (!Number.isFinite(time) || time <= 0) return "";
  const seconds = Math.round((time - Date.now()) / 1000);
  const formatter = new Intl.RelativeTimeFormat("zh-CN", { numeric: "auto" });
  if (Math.abs(seconds) < 60) return formatter.format(seconds, "second");
  const minutes = Math.round(seconds / 60);
  if (Math.abs(minutes) < 60) return formatter.format(minutes, "minute");
  const hours = Math.round(minutes / 60);
  if (Math.abs(hours) < 24) return formatter.format(hours, "hour");
  const days = Math.round(hours / 24);
  if (Math.abs(days) < 14) return formatter.format(days, "day");
  return new Intl.DateTimeFormat("zh-CN", { month: "short", day: "numeric" }).format(time);
}

export function formatClock(microseconds) {
  const time = Number(microseconds) / 1000;
  if (!Number.isFinite(time) || time <= 0) return "";
  return new Intl.DateTimeFormat("zh-CN", { hour: "2-digit", minute: "2-digit" }).format(time);
}

export function errorMessage(error) {
  if (error?.code === "network_error") return "无法连接本地服务，请确认 mdo 仍在运行。";
  if (error?.code === "session_busy") return "这个会话仍有任务在运行。";
  if (error?.code === "session_not_found") return "会话已经不存在，请刷新列表。";
  if (error?.code === "revision_conflict") return "内容已在其他窗口更新，请刷新后重试。";
  if (error?.code === "recovery_state_conflict") return "中断状态已经变化，请核对刷新后的调用再决定。";
  return error?.message || "操作未完成，请重试。";
}

export function toast(message, tone = "neutral") {
  const region = document.querySelector("#toast-region");
  if (!region) return;
  const item = element("div", { className: "toast", text: message, attrs: { "data-tone": tone, role: "status" } });
  region.append(item);
  window.setTimeout(() => item.remove(), 4200);
}
