import { t } from "../../i18n.js";
import { element } from "../../utils/dom.js";
import { formatArguments } from "./labels.js";

// A preview describes the requested action; it never infers that a shell
// command is safe or changes the permission/recovery policy.
export function toolCallPreview(item) {
  let args = {};
  try {
    const value = JSON.parse(item?.arguments_json || "{}");
    if (value && typeof value === "object" && !Array.isArray(value)) args = value;
  } catch { /* Custom tools may have arguments that cannot be summarized. */ }
  const string = (...values) => values.find(value => typeof value === "string" && value.trim()) || "";
  const effects = new Set(item?.effects ?? []);
  const tool = String(item?.tool || "");
  const executable = ["exec", "shell", "spawn"].includes(tool);
  const argv = executable && Array.isArray(args.argv) && args.argv.length &&
    args.argv.every(value => typeof value === "string") ? args.argv : null;
  const command = executable ? string(args.command, args.cmd) || (argv ?
    argv.map(value => /^[\w./:\\-]+$/.test(value) ? value : JSON.stringify(value)).join(" ") : "") : "";
  const path = string(args.path, args.file_path);
  const cwd = string(args.cwd, args.workdir, item?.workspace_root);
  const kind = executable ? "command" : effects.has("process") ? "process" :
    effects.has("workspace_write") ? "file" :
    effects.has("schedule") ? "schedule" :
    effects.has("agent_delegation") ? "agent" :
    effects.has("network") || effects.has("external_service") ? "network" : "tool";
  return { kind, tool, command, path, cwd };
}

export function renderToolPreview(item) {
  const preview = toolCallPreview(item);
  const root = element("div", { className: "interaction-preview" });
  const labels = { command: "Run a command", process: "Manage a process", file: "Change files",
    schedule: "Manage a schedule", agent: "Start a subagent", network: "Access a network or external service",
    tool: "Use tool: {tool}" };
  root.append(element("p", { className: "interaction-operation",
    text: t(`interaction.action.${preview.kind}`, { tool: preview.tool }, labels[preview.kind]) }));
  if (preview.command) root.append(element("pre", {
    className: "interaction-command", text: preview.command,
  }));
  if (preview.path) root.append(element("p", { className: "interaction-target",
    text: t("interaction.file", { path: preview.path }, "File: {path}") }));
  if (preview.cwd) root.append(element("p", { className: "interaction-target",
    text: t("interaction.cwd", { path: preview.cwd }, "Working directory: {path}") }));
  const shown = new Set([preview.command, preview.path, preview.cwd]);
  for (const resource of item?.resources ?? []) {
    if (typeof resource.resource !== "string" || !resource.resource || shown.has(resource.resource)) continue;
    // The native permission descriptor also includes a flattened command.
    // Its whitespace/quoting may differ from argv; do not show it twice.
    if (preview.command && resource.kind === "command") continue;
    shown.add(resource.resource);
    root.append(element("p", { className: "interaction-target", text: resource.resource }));
  }
  return root;
}

export function renderToolArguments(item, attrs = {}, summaryAttrs = {}) {
  return element("details", { className: "approval-arguments", attrs }, [
    element("summary", { text: t("interaction.details", {}, "View operation details"), attrs: summaryAttrs }),
    element("pre", { text: formatArguments(item?.arguments_json) }),
  ]);
}
