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

const RISKS = Object.freeze({
  low: ["dock.risk.low", "低风险"],
  medium: ["dock.risk.medium", "中风险"],
  high: ["dock.risk.high", "高风险"],
});

const RESOURCES = Object.freeze({
  path: "dock.resource.path", command: "dock.resource.command",
  process: "dock.resource.process", network: "dock.resource.network",
  external_service: "dock.resource.externalService", secret: "dock.resource.secret",
  schedule: "dock.resource.schedule", agent: "dock.resource.agent",
});

const ACCESS = Object.freeze({
  read: "读取", write: "写入", execute: "执行", control: "控制",
  connect: "连接", use: "使用",
});

export function effectLabel(value) {
  const [key, fallback] = EFFECTS[value] ?? [];
  return key ? t(key, {}, fallback) : String(value ?? "");
}

export function effectList(values) {
  return values.map(effectLabel).join(t("decision.effectSeparator", {}, "、"));
}

export function riskLabel(value) {
  const [key, fallback] = RISKS[value] ?? [];
  return key ? t(key, {}, fallback) : String(value ?? "");
}

export function resourceKindLabel(value) {
  return value ? t(RESOURCES[value] || "", {}, value)
    : t("decision.resource", {}, "资源");
}

export function resourceText(resource) {
  const access = (resource.access ?? []).map((value) =>
    t(`decision.access.${value}`, {}, ACCESS[value] ?? value))
    .join(t("decision.effectSeparator", {}, "、"));
  return `${access || t("decision.access.default", {}, "访问")} · ${resource.resource}`;
}

export function formatArguments(source) {
  if (!source) return "{}";
  try { return JSON.stringify(JSON.parse(source), null, 2); }
  catch { return source; }
}
