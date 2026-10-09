export const noticeIcons = { message: "ⓘ", warning: "⚠", error: "❗", success: "✓", coupon: "◇", update: "↑", tools: "ⓘ", account: "ⓘ" };
export function matchesNotice(n, device, locale, signedIn, now = Date.now()) {
  return n && typeof n.id === "string" && typeof n.title === "string" && typeof n.body === "string"
    && (!n.min_build || device.build_id >= n.min_build)
    && (!n.max_build || device.build_id < n.max_build)
    && (!n.platforms?.length || n.platforms.includes(device.platform))
    && (!n.editions?.length || n.editions.includes(device.edition))
    && (!n.languages?.length || n.languages.includes(locale))
    && (!n.starts_at || now >= n.starts_at * 1000) && (!n.ends_at || now < n.ends_at * 1000)
    && (n.audience !== "signed_in" || signedIn) && (n.audience !== "signed_out" || !signedIn);
}
export function buildNotices({ distribution = {}, update, account, locale, preferences = {}, now = Date.now(), copy }) {
  const notices = [], tools = distribution.tools ?? [], installed = distribution.installed ?? {};
  const updates=[];
  for (const pack of distribution.toolpacks ?? []) {
    if (pack.status!=="withdrawn" && installed[pack.id] && pack.revision > (installed[pack.id + "_revision"] ?? 0))updates.push(pack);
  }
  const apkTools=distribution.edition === "full" && distribution.toolpack_revision > (distribution.bundled_toolpack_revision ?? 0);
  const toolsBody=updates.map(p=>p.notes||copy.toolsBody).join("\n")+(apkTools?"\n"+copy.android:"");
  if(update?.available)notices.push({id:"app-update",revision:update.sha256+":"+updates.map(p=>p.revision).join(",")+":"+(apkTools?distribution.toolpack_revision:""),title:copy.update,body:(update.notes||copy.updateBody)+(toolsBody?"\n\n"+toolsBody:""),icon:"update",rank:0,action:"update",persistent:true});
  else if(updates.length||apkTools)notices.push({id:"tools-update",revision:updates.map(p=>p.id+":"+p.revision).join(",")+(apkTools?distribution.toolpack_revision:""),title:copy.toolsUpdate,body:toolsBody,icon:"update",rank:1,action:"update",persistent:true});
  if (tools.filter(t => ["busybox", "curl", "jq", "ssh"].includes(t.id)).length < 4)
    notices.push({ id: "tools-guide", revision: 1, title: copy.install, body: copy.toolsBody, icon: "tools", rank: 2, action: "tools" });
  if (account?.state === "signed_out") notices.push({ id: "account-guide", revision: 1, title: copy.login, body: copy.accountBody, icon: "account", rank: 3, action: "account" });
  for (const n of distribution.notices ?? []) if (matchesNotice(n, distribution, locale, account?.state === "signed_in", now) && !(n.icon === "coupon" && preferences.marketing === false))
    notices.push({ ...n, rank: 4, online: true });
  return notices.filter(n => n.persistent || (preferences[n.id]?.dismissed !== n.revision && !(preferences[n.id]?.snooze > now)))
    .sort((a, b) => a.rank - b.rank || (b.priority ?? 0) - (a.priority ?? 0) || (b.starts_at ?? 0) - (a.starts_at ?? 0) || a.id.localeCompare(b.id));
}
