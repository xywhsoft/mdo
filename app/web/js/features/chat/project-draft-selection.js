import { t } from "../../i18n.js";
import { projectDraftKey } from "./draft-store.js";

// The global draft remains the write-ahead journal while a first session is
// created. Before that point, each project's unsent editor has its own file.
export function createProjectDraftSelection({ draftStore, navigation,
  onFailure, onChange, onMigrated = () => {} }) {
  let legacyOwner = "";
  let migrating = false;

  function owner() {
    const pending = draftStore.newTask();
    if (pending) return pending.project_id;
    if (legacyOwner) return legacyOwner;
    if (draftStore.isLoaded("") &&
        (draftStore.text("") || draftStore.submissions("").length))
      return navigation.get().projectId || navigation.preferredProject();
    return "";
  }

  function key(route = navigation.get()) {
    if (route.sessionId) return `${route.projectId}/${route.sessionId}`;
    if (migrating || !draftStore.isLoaded("") || owner()) return "";
    return projectDraftKey(route.projectId || navigation.preferredProject());
  }

  async function restoreLegacy() {
    if (migrating) return false;
    if (!await draftStore.ensureLoaded("")) return false;
    if (draftStore.newTask()) return true;
    const text = draftStore.text("");
    if (!text && !draftStore.submissions("").length) {
      navigation.revalidate();
      return true;
    }
    legacyOwner ||= navigation.get().projectId || navigation.preferredProject();
    if (draftStore.submissions("").length) {
      onFailure(new Error(t("draft.projectMigrationPending")));
      navigation.revalidate();
      return false;
    }
    migrating = true;
    onChange();
    try {
      const projectKey = projectDraftKey(legacyOwner);
      if (!await draftStore.ensureLoaded(projectKey))
        throw new Error(t("draft.projectMigrationFailed"));
      const existing = draftStore.text(projectKey);
      if (existing && existing !== text)
        throw new Error(t("draft.projectMigrationConflict"));
      if (!existing) draftStore.capture(projectKey, text);
      if (!await draftStore.flush(projectKey))
        throw new Error(t("draft.projectMigrationFailed"));
      // Never clear the only copy if an edit or first submission arrived
      // during the migration. The page can retry with the new snapshot.
      if (draftStore.newTask() || draftStore.text("") !== text)
        throw new Error(t("draft.projectMigrationConflict"));
      draftStore.clear("");
      if (!await draftStore.flush("")) {
        draftStore.edit("", text, [], true);
        throw new Error(t("draft.projectMigrationFailed"));
      }
      legacyOwner = "";
      onMigrated();
      return true;
    } catch (error) {
      onFailure(error);
      return false;
    } finally {
      migrating = false;
      onChange();
      navigation.revalidate();
    }
  }

  return Object.freeze({ key, owner, restoreLegacy,
    isMigrating: () => migrating });
}
