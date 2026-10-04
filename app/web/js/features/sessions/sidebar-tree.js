// The sidebar is a view of the existing catalog. Pinning never duplicates a
// session, and removing a project definition must not hide its saved history.
export function recentSessions(sessions) {
  return [...sessions].sort((a, b) =>
    Number(b.updated_at || 0) - Number(a.updated_at || 0) ||
    `${a.project_id}/${a.id}`.localeCompare(`${b.project_id}/${b.id}`));
}

export function sidebarGroups({ sessions = [], projects = [], status = "active",
  query = "", locale = "zh-CN", projectSort = "name", defaultProjectName = "default" }) {
  const catalog = new Map(projects.map((project) => [project.id, project]));
  const needle = query.trim().toLocaleLowerCase(locale);
  const matches = (value) => String(value || "").toLocaleLowerCase(locale).includes(needle);
  const visible = recentSessions(sessions).filter((session) =>
    (status === "all" || session.status === status) && (!needle || matches(
      `${session.title} ${session.project_id} ${session.project_id === "default" ? defaultProjectName :
        catalog.get(session.project_id)?.name || ""} ${session.agent_id}`)));
  const pinned = visible.filter((session) => session.status === "active" && session.pinned);
  const ordinary = visible.filter((session) => session.status !== "active" || !session.pinned);
  // A pinned task still contributes to its project's recent activity.
  const activity = new Map();
  for (const session of visible) {
    if (!activity.has(session.project_id)) activity.set(session.project_id, Number(session.updated_at || 0));
  }
  const groups = new Map();
  for (const project of projects) {
    if (project.id === "default" || status !== "active" ||
      (needle && !matches(`${project.id} ${project.name}`))) continue;
    groups.set(project.id, { id: project.id, name: project.name || project.id,
      project, sessions: [] });
  }
  for (const session of ordinary) {
    if (!session.project_id || session.project_id === "default") continue;
    if (!groups.has(session.project_id)) {
      const project = catalog.get(session.project_id);
      groups.set(session.project_id, { id: session.project_id,
        name: project?.name || session.project_id, project, sessions: [] });
    }
    groups.get(session.project_id).sessions.push(session);
  }
  const ordered = [...groups.values()].sort((a, b) => {
    if (projectSort === "recent") {
      const difference = (activity.get(b.id) || 0) - (activity.get(a.id) || 0);
      if (difference) return difference;
    }
    return a.name.localeCompare(b.name, locale) || a.id.localeCompare(b.id);
  });
  return { pinned, projects: ordered,
    tasks: ordinary.filter((session) => session.project_id === "default"),
    total: visible.length };
}

export function sidebarWindow(sessions, limit, selectedKey = "", searching = false) {
  if (searching) return { items: sessions, remaining: 0 };
  const items = sessions.slice(0, limit);
  const selected = sessions.find((session) =>
    `${session.project_id}/${session.id}` === selectedKey);
  // A task opened from search or another panel remains visible even when it
  // falls outside the recent window. Do not render the entire older prefix.
  if (selected && !items.includes(selected)) items.push(selected);
  return { items, remaining: sessions.length - items.length };
}
