// The UI and Agent dispatcher use the same admission descriptor. Concurrent
// mounts must attach to one native view, not navigate a second popup target.
export function createNativeBrowserExecutor(bridge, browser) {
  // Task-owned, with no closure over a panel, mutable broker row or AbortSignal.
  return (command, params = {}) => bridge.call(command, { ...params, browser });
}

export async function ensureNativeBrowser({ bridge, sdk, browser, clientId, bounds, visible = false, requireDownloads = false, requireAutomation = false, alive = () => true, beforeCreate, cancelOnDetach = true }) {
  requireAutomation ||= browser.automation_required === true;
  const pending = browser.runtime?.popup_request;
  const check = () => { if (!alive()) throw new DOMException("浏览器所属窗口或设备已改变", "AbortError"); };
  check();
  if (pending && (pending.opener !== browser.opener || pending.client !== clientId || browser.client !== clientId))
    throw new Error("此弹出页的原生请求属于之前的窗口，请关闭失效标签后重新操作。");
  const required=requireDownloads||browser.download_policy==='intercept';
  if(required||requireAutomation){
    const capability=await bridge.call('capabilities');check();
    if(requireAutomation&&capability.browser_use?.available===false){
      const reason=capability.browser_use.reason;
      const message=reason==='isolated_world_unsupported'?'当前系统 WebView 不支持安全的隔离执行，Browser Use 自动化不可用；可以手动浏览网页。':
        reason==='android_adapter_incomplete'?'当前 Android 客户端尚未完成 Browser Use 自动化适配，可以手动浏览网页。':'当前设备未提供安全的 Browser Use 自动化能力，可以手动浏览网页。';
      throw Object.assign(new Error(message),{code:'unsupported_capability',capability_reason:['isolated_world_unsupported','android_adapter_incomplete','provider_loader_unsupported','world_provider_unavailable'].includes(reason)?reason:'browser_use_unavailable'});
    }
    if(required&&capability.initial_download_interception!==true)throw new Error('当前客户端不能在打开网址前安全拦截下载，请更新客户端后重试。');
  }
  if (beforeCreate) { await beforeCreate(); check(); }
  const result = await bridge.call("create", { browser: browser.id, url: browser.url || "about:blank", bounds, visible, ...(required?{intercept_downloads:true}:{}),...(requireAutomation?{automation:true}:{}),
    ...(pending ? { popup: pending.id, opener: pending.opener } : {}) });
  const reference=result?.navigation_ref;
  const nativeReference=typeof reference==='string'&&/^nv-[a-zA-Z0-9-]{1,96}$/.test(reference)&&result.created===true&&
    result.downloads_intercepted===true&&result.initial_navigation_pending===true?reference:null;
  try{
    check();
    const failure=reference!==undefined&&!nativeReference||result?.initial_navigation_pending===true&&!nativeReference?
      {error:{code:'native_create_result_invalid'}}:result?.ok===false?result:null;
    if(failure){
      const raw=failure.native_create,names=['created_may_have_occurred','navigation_may_have_occurred','network_cleanup_confirmed','view_cleanup_confirmed'];
      const valid=raw&&typeof raw==='object'&&!Array.isArray(raw)&&names.every(name=>typeof raw[name]==='boolean');
      const receipt=Object.freeze(Object.fromEntries(names.map(name=>[name,valid?raw[name]:name.endsWith('may_have_occurred')])));
      const reason=typeof failure.error?.code==='string'&&/^[a-z][a-z0-9_]{1,79}$/.test(failure.error.code)?failure.error.code:'native_create_result_invalid';
      const message=receipt.navigation_may_have_occurred?'浏览器导航结果未确认，请先检查原标签，避免重复打开网址。':
        receipt.network_cleanup_confirmed&&receipt.view_cleanup_confirmed?'浏览器在打开网址前启动失败，原生资源已清理。':'浏览器启动失败，原生资源尚未确认释放，请检查原标签状态。';
      throw Object.assign(new Error(message),{code:'native_create_failed',creation_reason:reason,
        native_create:receipt,effect_may_have_occurred:receipt.navigation_may_have_occurred});
    }
    if(required&&result.created&&result.downloads_intercepted!==true&&!result.manual)throw new Error('浏览器未确认首次导航前的下载拦截；请检查已有标签，不要重新打开网址。');
    if (pending) await sdk.call("browser.popup.settle", { id: browser.id, client: clientId, popup: pending.id });
    check();return result;
  }catch(error){
    // The old target may already have effects. Revoke only its exact queued
    // operation; never close or stop a physical view now used by a human.
    if(nativeReference&&(cancelOnDetach||alive()))try{await bridge.call('navigation_cancel',{browser:browser.id,agent:true,navigation_ref:nativeReference},{timeout:1000});}catch{}
    throw error;
  }
}
