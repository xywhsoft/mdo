// The pinned XS Android bridge transfers at most 8 MiB through FileReader.
// Check before creating a URL or reporting browser handoff. Desktop/browser
// downloads keep their existing limits and do not depend on this bridge.
export const ANDROID_EXPORT_MAX_BYTES = 8 * 1024 * 1024;

export function saveBlobFile({ blob, filename }) {
  if (typeof globalThis.XsExport?.save === "function" && blob.size > ANDROID_EXPORT_MAX_BYTES) {
    const error = new Error("This file exceeds Android's 8 MiB export limit. Use a desktop or browser to export it.");
    error.code = "export_platform_limit";
    throw error;
  }
  const url = URL.createObjectURL(blob);
  let link;
  try {
    link = document.createElement("a");
    link.href = url; link.download = filename;
    // The Android shell observes document clicks. A detached anchor cannot
    // reach that listener, even though desktop browsers may download it.
    document.body.append(link); link.click();
  } finally {
    link?.remove();
    // The browser/native listener needs time to take its own Blob reference.
    window.setTimeout(() => URL.revokeObjectURL(url), 60000);
  }
}
