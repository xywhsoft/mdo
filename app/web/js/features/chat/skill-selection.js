import { t as uiErrorText, t as uiText } from "../../i18n.js";

import { api } from "../../api/client.js";

export function parseSkillSelection(value) {
  const match = /^(?:\$|\/skill:)([a-z0-9][a-z0-9_.:-]{0,191})(?:\s+([\s\S]*))?$/.exec(value);
  return match ? { id: match[1], arguments: match[2] || "" } : null;
}

export async function loadSelectableSkills(project = "default") {
  const response = await api.get(`/skills?project=${encodeURIComponent(project)}`);
  return (response.data.items || []).filter(item => item.user_invocable !== false).map(item => ({ ...item, skill: true,
    name: `$${item.id}`, custom: true }));
}

export async function expandSkillSelection(selection, project = "default") {
  const text = (await api.post(`/skills/invoke?project=${encodeURIComponent(project)}`, selection)).data.text;
  if (typeof text !== "string") throw new Error(uiErrorText("error.detail.skillExpansionReturnedInvalidText", {}, "Skill expansion returned invalid text"));
  if (new TextEncoder().encode(text).length > 65536)
    throw new Error(uiText("ui.skillSelection.expandedSkillExceedsThe64KibDraftLimitMove", {}, "Skill 展开后超过 64 KiB 草稿上限；请将详细内容移到 references，或让模型通过 skill 工具分次读取。原草稿已保留。"));
  return text;
}
