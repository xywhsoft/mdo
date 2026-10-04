import { t } from "../../i18n.js";

const EFFECTS = Object.freeze({
  read: ["dock.effect.read", "读取"],
  workspace_write: ["dock.effect.workspaceWrite", "修改工作区"],
  process: ["dock.effect.process", "运行进程"],
  network: ["dock.effect.network", "访问网络"],
  external_service: ["dock.effect.externalService", "外部服务"],
  secrets: ["dock.effect.secrets", "使用凭据"],
  schedule: ["dock.effect.schedule", "计划任务"],
  agent_delegation: ["dock.effect.agentDelegation", "启动子 Agent"],
});

export function effectLabel(value) {
  const [key, fallback] = EFFECTS[value] ?? [];
  return key ? t(key, {}, fallback) : String(value ?? "");
}

export function effectList(values) {
  return values.map(effectLabel).join(t("decision.effectSeparator", {}, "、"));
}

export function formatArguments(source) {
  if (!source) return "{}";
  try { return JSON.stringify(JSON.parse(source), null, 2); }
  catch { return source; }
}
