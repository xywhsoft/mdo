import { t as uiText } from "../i18n.js";
import { api } from "./client.js";
import { targetFetch, targetState } from "./target.js";
import { createSha256 } from "../utils/sha256.js";
import { saveBlobFile, ANDROID_EXPORT_MAX_BYTES } from "../utils/file-download.js";

export async function exportDirectoryZip(kind,id,{signal}={}) {
  const target=targetState().selected?.id||"local";
  const check=()=>{if(signal?.aborted||(targetState().selected?.id||"local")!==target)throw new DOMException("Cancelled","AbortError");};
  check();const task=(await api.post(`/extensions/${kind}/${encodeURIComponent(id)}/pack`,{},{signal})).data;
  if(!Number.isSafeInteger(task.bytes)||!task.bytes||task.bytes>512*1048576||!/^[A-Za-z0-9_-]{32}$/.test(task.id))throw Error(uiText("ui.extensionExport.invalidZipTask", {}, "无效的 ZIP 任务"));
  if(typeof globalThis.XsExport?.save==="function"&&task.bytes>ANDROID_EXPORT_MAX_BYTES)throw Error(uiText("ui.extensionExport.thisZipExceedsTheAndroidExportLimitExportIn", {}, "此 ZIP 超过 Android 当前导出上限，请使用浏览器或桌面端导出；归档任务已保留。"));
  const parts=[],hash=createSha256();
  for(let at=0;at<task.bytes;at+=1048576){check();const response=await targetFetch(`/api/v1/extension-tasks/${task.id}/file/${at}`,{signal,credentials:"same-origin",cache:"no-store"});if(!response.ok)throw Error(uiText("ui.extensionExport.zipDownloadFailedHttpValue1", { value1: response.status }, "ZIP 下载失败（HTTP {value1}）"));const bytes=new Uint8Array(await response.arrayBuffer());if(bytes.length!==Math.min(1048576,task.bytes-at))throw Error(uiText("ui.extensionExport.zipByteCountDoesNotMatchTheTask", {}, "ZIP 字节长度与任务不一致"));hash.update(bytes);parts.push(bytes);}
  check();if(hash.hex()!==task.sha256)throw Error(uiText("ui.extensionExport.zipIntegrityCheckFailedCreateTheArchiveAgain", {}, "ZIP 完整性检查失败，请重新打包"));saveBlobFile({blob:new Blob(parts,{type:"application/zip"}),filename:`${id}.zip`});
}
