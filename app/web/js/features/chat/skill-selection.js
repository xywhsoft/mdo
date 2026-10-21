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
  return (await api.post(`/skills/invoke?project=${encodeURIComponent(project)}`, selection)).data.text;
}
