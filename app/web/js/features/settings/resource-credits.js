import { browserText, detailFacts } from "../../components/extension-browser.js";

export const storeResource = (item, receipt) => item?.store_managed === true || Boolean(receipt && receipt.source !== "local");
export const customizableResource = (item, receipt) => item?.management_known !== false && !storeResource(item, receipt);
export const storeReadOnlyMessage = () => browserText("storeReadOnly", "商店安装的资源仅支持启停、更新和卸载；不能编辑、复制或再次分享。");
export function resourceCredits(item = {}, receipt = null) {
  const manifest = receipt?.manifest || {}, managed = storeResource(item, receipt);
  const submitter = item.submitter || receipt?.submitter || (managed ? browserText("unknownSubmitter", "提交者信息未提供") : item.external===false ? browserText("builtin", "内置") : browserText("localAdded", "本机添加"));
  const author = manifest.author || receipt?.author || item.author || "";
  const copyright = manifest.copyright || receipt?.copyright || item.copyright || "";
  const license = manifest.license || receipt?.license || item.license || "";
  const version = receipt?.version || item.version || "";
  const short = text => { const chars = Array.from(text); return chars.length > 120 ? chars.slice(0, 120).join("") + "…" : text; };
  return { submitter, author, copyright, license, version,
    meta: [author ? `${browserText("author", "作者")}：${author}` : "", version, license, copyright ? short(copyright) : ""],
    facts: () => detailFacts([[browserText("submitter", "提交者"), submitter], [browserText("author", "作者"), author || browserText("notProvided", "未提供")],
      [browserText("copyright", "版权声明"), copyright || browserText("copyrightUnknown", "未提供独立版权声明，请参阅许可及源码中的归属信息。")],
      [browserText("license", "许可"), license || browserText("notProvided", "未提供")], [browserText("version", "版本"), version]]) };
}
