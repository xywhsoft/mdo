import { t } from "../../i18n.js";

const text = key => t(`share.${key}`);
const same = (a, b) => a.text === b.text && JSON.stringify(a.attachments) === JSON.stringify(b.attachments);
const append = (a, b) => [a, b].filter(Boolean).join("\n\n");
const reference = path => `@${JSON.stringify(path)}`;
const validId = value => typeof value === "string" && /^[0-9a-f]{32}$/.test(value);
const validImages = value => Array.isArray(value) && value.length <= 4 && value.every(validId) && new Set(value).size === value.length;
const validDraft = value => value && typeof value.text === "string" && validImages(value.attachments);
function validShare(value) {
  return value && validId(value.id) && !value.error && typeof value.text === "string" &&
    new TextEncoder().encode(value.text).length <= 32768 && Array.isArray(value.files) && value.files.length <= 32 &&
    new Set(value.files.map(file => file?.id)).size === value.files.length && value.files.every(file => file &&
      validId(file.id) && typeof file.name === "string" && file.name.length > 0 &&
      new TextEncoder().encode(file.name).length <= 180 && !/[\0\r\n/\\]/.test(file.name) &&
      typeof file.mime === "string" && file.mime.length <= 127 && /^[a-z0-9.+-]+\/[a-z0-9.+-]+$/.test(file.mime) &&
      typeof file.sha256 === "string" && /^[0-9a-f]{64}$/.test(file.sha256) &&
      Number.isSafeInteger(file.bytes) && file.bytes >= 0 && file.bytes <= 64*1024*1024);
}

export async function sharedImage(bridge, share, file, check) {
  const parts = []; let bytes = 0;
  while (bytes < file.bytes) {
    check();
    const encoded = bridge.read(share.id, file.id, bytes);
    if (typeof encoded !== "string") throw new Error(text("readFailed"));
    const decoded = Uint8Array.from(atob(encoded), char => char.charCodeAt(0));
    if (!decoded.length || decoded.length > 65536 || bytes + decoded.length > file.bytes)
      throw new Error(text("readFailed"));
    parts.push(decoded); bytes += decoded.length;
  }
  const blob = new Blob(parts, { type: file.mime });
  // The source can be removed/modified by local tools. Match the snapshot that
  // the native importer verified, rather than attaching different image bytes.
  const digest = await globalThis.crypto.subtle.digest("SHA-256", await blob.arrayBuffer());
  const hash = [...new Uint8Array(digest)].map(byte => byte.toString(16).padStart(2, "0")).join("");
  if (hash !== file.sha256) throw new Error(text("readFailed"));
  return new File([blob], file.name, { type: file.mime });
}

// Binding and the exact before/after draft plan are persisted by the Android
// shell. Files and attachments have stable IDs, so manual retries are safe even
// if an HTTP acknowledgement or the final native acknowledgement was lost.
export async function importShare({ share, destination, bridge, api, createSession,
  draftStore, check = () => {}, supportsImages = session => Boolean(Number(session.attachments) & 1),
  onProgress = () => {}, onConflict = async () => false }) {
  if (!validShare(share)) throw new Error(text("incomplete"));
  check();
  const identity = share.id;
  share = bridge.bind(identity, destination.project_id, destination.session_id, Boolean(destination.create));
  if (!validShare(share) || share.id !== identity || share.destination?.project_id !== destination.project_id ||
      share.destination?.session_id !== destination.session_id) throw new Error(text("bindFailed"));
  destination = share.destination;
  const path = `/projects/${encodeURIComponent(destination.project_id)}/sessions/${encodeURIComponent(destination.session_id)}`;
  const key = `${destination.project_id}/${destination.session_id}`;
  let session;
  try { check(); session = (await api.get(path)).data; }
  catch (error) {
    if (error.status !== 404 || !destination.create) throw error;
    check();
    session = await createSession({ project_id: destination.project_id,
      client_session_id: destination.session_id, title: share.files[0]?.name || share.text.slice(0, 60) || text("title") });
  }
  if (session.id !== destination.session_id || session.project_id !== destination.project_id || session.status !== "active")
    throw new Error(text("inactive"));
  check();
  if (!await draftStore.ensureLoaded(key)) throw new Error(text("draftFailed"));
  let plan = share.delivery;
  if (!plan) {
    const before = draftStore.snapshot(key), attachments = [...before.attachments], references = [];
    for (const [index, file] of share.files.entries()) {
      if (!validId(file.id) || !Number.isSafeInteger(file.bytes) || file.bytes < 0 || file.bytes > 64*1024*1024)
        throw new Error(text("readFailed"));
      check(); onProgress(index + 1, share.files.length);
      const result = (await api.post(path + "/shared-files", { share_id: share.id, file_id: file.id })).data;
      if (typeof result.path !== "string" || !result.path.startsWith(`shared-files/${share.id}/`) || result.path.includes("\n"))
        throw new Error(text("readFailed"));
      references.push(reference(result.path));
      if (supportsImages(session) && attachments.length < 4 && file.bytes > 0 &&
          file.bytes <= 8*1024*1024 && ["image/png", "image/jpeg", "image/webp"].includes(file.mime)) {
        const image = await sharedImage(bridge, share, file, check);
        check();
        try {
          const uploaded = await api.uploadImage(destination.project_id, destination.session_id, image, file.mime, { uploadId: file.id });
          if (uploaded.id !== file.id) throw new Error(text("readFailed"));
          if (!attachments.includes(file.id)) attachments.push(file.id);
        } catch (error) {
          // A mislabeled image still exists as an ordinary workspace file.
          if (error.code !== "image_type_invalid") throw error;
        }
      }
    }
    check();
    const addition = append(share.text, references.join("\n"));
    plan = { before, after: { text: append(before.text, addition), attachments },
      addition, addedImages: attachments.filter(id => !before.attachments.includes(id)) };
  }
  check();
  if (!validDraft(plan?.before) || !validDraft(plan?.after) || typeof plan.addition !== "string" || !validImages(plan.addedImages))
    throw new Error(text("draftFailed"));
  let current = draftStore.snapshot(key), replace = false;
  if (!same(current, plan.before) && !same(current, plan.after)) {
    if (!await onConflict()) throw new Error(text("draftChanged"));
    check(); current = draftStore.snapshot(key);
    plan = { ...plan, before: current, after: { text: append(current.text, plan.addition),
      attachments: [...new Set([...current.attachments, ...plan.addedImages])] } };
    replace = true;
  }
  if (new TextEncoder().encode(plan.after.text).length > 65536 || plan.after.attachments.length > 4)
    throw new Error(text("draftFull"));
  if (!bridge.plan(share.id, plan, replace)) throw new Error(text("draftFailed"));
  check();
  if (!same(current, plan.after)) draftStore.edit(key, plan.after.text, plan.after.attachments, true);
  if (!await draftStore.flush(key) || !draftStore.isDraftDurable(key, plan.after.text, plan.after.attachments))
    throw new Error(text("draftFailed"));
  check();
  if (!bridge.acknowledge(share.id)) throw new Error(text("ackFailed"));
  return { project_id: destination.project_id, session_id: destination.session_id, key };
}
