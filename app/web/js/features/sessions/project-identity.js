function pathName(path) {
  return path.trim().replace(/[\\/]+$/, "").split(/[\\/]/).at(-1) || "";
}

export function projectIdFromName(name) {
  const slug = name.toLowerCase().replace(/[^a-z0-9._-]+/g, "-")
    .replace(/^[._-]+|[._-]+$/g, "").slice(0, 64);
  return slug || `project-${Date.now().toString(36)}`;
}

export function projectDefaultsFromWorkspace(workspaceRoot) {
  const workspace_root = workspaceRoot.trim();
  const name = pathName(workspace_root);
  return { workspace_root, name, id: projectIdFromName(name) };
}
