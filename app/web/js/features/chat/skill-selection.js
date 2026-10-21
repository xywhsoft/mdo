import { api } from "../../api/client.js";

export function parseSkillSelection(value) {
  const match = /^(?:\$|\/skill:)([a-z0-9][a-z0-9_.:-]{0,95})(?:\s+([\s\S]*))?$/.exec(value);
  return match ? { id: match[1], arguments: match[2] || "" } : null;
}

export async function loadSelectableSkills() {
  const response = await api.get("/skills");
  return (response.data.items || []).filter(item => item.user_invocable !== false).map(item => ({ ...item, skill: true,
    name: `$${item.id}`, custom: true }));
}

export async function expandSkillSelection(selection) {
  return (await api.post("/skills/invoke", selection)).data.text;
}
