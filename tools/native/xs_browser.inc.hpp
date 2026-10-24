/* Native content views. Included after xs_directory_drop.inc.hpp. The bridge
 * accepts messages only from the original main document; content has no host
 * bindings and lives in an independent persistent browser environment. */
#include <functional>
#include <map>
#include <memory>
#include <algorithm>
#include <mutex>
namespace {
using xs_browser_json = std::string;
static std::string xb_get(const std::string& s,const char* key){return webview::detail::json_parse(s,key,0);}
static std::string xb_quote(const std::string& s){return webview::detail::json_escape(s);}
static bool xb_url(const std::string& url){return url=="about:blank"||((url.rfind("https://",0)==0||url.rfind("http://",0)==0)&&url.size()<=8192&&url.find_first_of("\r\n") == std::string::npos);}
template<class I,typename... Args> class xb_callback final:public I{
  std::atomic<ULONG> refs{1};IID iid;std::function<HRESULT(Args...)> fn;
public:
  xb_callback(REFIID id,std::function<HRESULT(Args...)> callback):iid(id),fn(std::move(callback)){}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(IsEqualIID(id,xs_wv_iid_unknown)||IsEqualIID(id,iid)){*out=static_cast<I*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
  ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
  ULONG STDMETHODCALLTYPE Release()override{auto n=--refs;if(!n)delete this;return n;}
  HRESULT STDMETHODCALLTYPE Invoke(Args... args)override{try{return fn(args...);}catch(...){return E_FAIL;}}
};
template<class I,typename... Args,typename F> static I* xb_cb(REFIID id,F fn){return new xb_callback<I,Args...>(id,std::function<HRESULT(Args...)>(std::move(fn)));}
struct xb_view{
  ICoreWebView2Controller* controller=nullptr;ICoreWebView2* core=nullptr;HWND window=nullptr;bool creating=true,manual=false,loading=true,failed=false;RECT bounds{};std::string error;int errorCode=0;UINT64 navigation=0;
  ~xb_view(){if(controller){controller->Close();controller->Release();}if(core)core->Release();if(window)DestroyWindow(window);}
};
struct xb_host{
  void* owner=nullptr;HWND window=nullptr;ICoreWebView2* main=nullptr;bool alive=true;std::wstring document,profile;
  webview::detail::mswebview2::loader loader;
  std::map<std::string,std::shared_ptr<xb_view>> views;
  bool trusted()const{xs_wv_text current;return alive&&main&&SUCCEEDED(main->get_Source(&current.p))&&xs_wv_document(current.p)==document;}
  void reply(const std::string& id,const std::string& result,const std::string& error=""){
    if(!trusted())return;
    std::string json="{\"channel\":\"mdo-browser\",\"id\":"+xb_quote(id)+(error.empty()?",\"result\":"+(result.empty()?"{}":result):",\"error\":"+xb_quote(error))+"}";
    if(json.size()>1048576)json="{\"channel\":\"mdo-browser\",\"id\":"+xb_quote(id)+",\"error\":\"Browser result exceeds 1 MiB\"}";
    main->PostWebMessageAsJson(webview::detail::widen_string(json).c_str());
  }
};
static std::map<void*,std::shared_ptr<xb_host>> xb_hosts;
static std::mutex xb_hosts_mutex;
static RECT xb_bounds(const std::string& json){
  const auto value=xb_get(json,"bounds");
  auto number=[&](const char* key,double fallback){try{return std::stod(xb_get(value,key));}catch(...){return fallback;}};
  double scale=std::max(0.5,std::min(8.0,number("scale",1)));
  auto x=static_cast<LONG>(std::max(0.0,std::min(32768.0,number("x",0)*scale)));
  auto y=static_cast<LONG>(std::max(0.0,std::min(32768.0,number("y",0)*scale)));
  auto w=static_cast<LONG>(std::max(1.0,std::min(16384.0,number("width",640)*scale)));
  auto h=static_cast<LONG>(std::max(1.0,std::min(16384.0,number("height",480)*scale)));
  return RECT{x,y,x+w,y+h};
}
/* The main WebView lives inside webview's full-client UI widget. A controller
 * parented to the outer frame can load and capture successfully while that
 * widget covers it. Give each content view its own child of the same widget,
 * then keep its HWND above the main controller without stealing focus. */
static bool xb_place(const std::shared_ptr<xb_view>& view,bool visible){
  if(!view->window||!view->controller)return false;
  const LONG width=view->bounds.right-view->bounds.left,height=view->bounds.bottom-view->bounds.top;
  const RECT local{0,0,width,height};
  HRESULT bounds=view->controller->put_Bounds(local);
  HRESULT shown=view->controller->put_IsVisible(visible?TRUE:FALSE);
  BOOL placed=SetWindowPos(view->window,HWND_TOP,view->bounds.left,view->bounds.top,width,height,
    SWP_NOACTIVATE|(visible?SWP_SHOWWINDOW:SWP_HIDEWINDOW));
  return SUCCEEDED(bounds)&&SUCCEEDED(shown)&&placed;
}
static std::string xb_navigation_error(COREWEBVIEW2_WEB_ERROR_STATUS error){
  switch(error){
    case COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_COMMON_NAME_IS_INCORRECT:
    case COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_EXPIRED:
    case COREWEBVIEW2_WEB_ERROR_STATUS_CLIENT_CERTIFICATE_CONTAINS_ERRORS:
    case COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_REVOKED:
    case COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_IS_INVALID:return "TLS certificate validation failed";
    case COREWEBVIEW2_WEB_ERROR_STATUS_HOST_NAME_NOT_RESOLVED:return "The website hostname could not be resolved";
    case COREWEBVIEW2_WEB_ERROR_STATUS_TIMEOUT:return "The website connection timed out";
    case COREWEBVIEW2_WEB_ERROR_STATUS_CANNOT_CONNECT:
    case COREWEBVIEW2_WEB_ERROR_STATUS_SERVER_UNREACHABLE:return "The website could not be reached";
    case COREWEBVIEW2_WEB_ERROR_STATUS_CONNECTION_RESET:
    case COREWEBVIEW2_WEB_ERROR_STATUS_CONNECTION_ABORTED:
    case COREWEBVIEW2_WEB_ERROR_STATUS_DISCONNECTED:return "The website connection was interrupted";
    case COREWEBVIEW2_WEB_ERROR_STATUS_REDIRECT_FAILED:return "The website redirect failed";
    default:return "Browser navigation failed";
  }
}
static void xb_receive(const std::shared_ptr<xb_host>& host,const std::string& json){
  const auto id=xb_get(json,"id"),command=xb_get(json,"command"),browser=xb_get(json,"browser");
  if(id.size()!=36||xb_get(json,"channel")!="mdo-browser")return;
  if(command=="capabilities"){host->reply(id,"{\"native\":true,\"snapshot\":true,\"screenshot\":true,\"input\":true,\"profile_isolation\":true}");return;}
  if(browser.empty()||browser.size()>100){host->reply(id,"","Invalid browser identity");return;}
  auto found=host->views.find(browser);
  if(command=="create"){
    if(found!=host->views.end()){
      auto view=found->second;
      if(view->controller){view->bounds=xb_bounds(json);if(!xb_place(view,xb_get(json,"visible")!="false")){host->reply(id,"","Cannot place content browser viewport");return;}}
      host->reply(id,"{\"attached\":true}");return;
    }
    if(host->views.size()>=8){host->reply(id,"","Native browser view limit reached (8)");return;}
    const auto url=xb_get(json,"url");const bool visible=xb_get(json,"visible")!="false";if(!xb_url(url)){host->reply(id,"","Only HTTP(S) navigation is supported");return;}
    auto view=std::make_shared<xb_view>();view->bounds=xb_bounds(json);
    view->window=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,
      0,0,1,1,host->window,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!view->window){host->reply(id,"","Cannot create content browser viewport");return;}host->views[browser]=view;
    auto* environment=xb_cb<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,HRESULT,ICoreWebView2Environment*>(IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,
      [host,view,id,browser,url,visible](HRESULT status,ICoreWebView2Environment* env){
        if(!host->alive)return S_OK;
        if(FAILED(status)||!env){host->views.erase(browser);host->reply(id,"","Cannot initialize content browser environment");return S_OK;}
        auto* ready=xb_cb<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,HRESULT,ICoreWebView2Controller*>(IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,
          [host,view,id,browser,url,visible](HRESULT code,ICoreWebView2Controller* controller){
            if(!host->alive||host->views.find(browser)==host->views.end()){if(controller)controller->Close();return S_OK;}
            if(FAILED(code)||!controller){host->views.erase(browser);host->reply(id,"","Cannot create content browser view");return S_OK;}
            controller->AddRef();view->controller=controller;controller->get_CoreWebView2(&view->core);view->creating=false;
            if(!view->core){host->views.erase(browser);host->reply(id,"","Content browser is unavailable");return S_OK;}
            xs_wv_com<ICoreWebView2Settings> settings;
            if(SUCCEEDED(view->core->get_Settings(settings.out()))){settings->put_IsWebMessageEnabled(FALSE);settings->put_AreHostObjectsAllowed(FALSE);settings->put_IsStatusBarEnabled(FALSE);}
            std::weak_ptr<xb_view> weak=view;
            std::weak_ptr<xb_host> weakHost=host;
            auto* accelerator=xb_cb<ICoreWebView2AcceleratorKeyPressedEventHandler,ICoreWebView2Controller*,ICoreWebView2AcceleratorKeyPressedEventArgs*>(IID_ICoreWebView2AcceleratorKeyPressedEventHandler,
              [weakHost,browser](ICoreWebView2Controller*,ICoreWebView2AcceleratorKeyPressedEventArgs* args){
                COREWEBVIEW2_KEY_EVENT_KIND kind;UINT key=0;args->get_KeyEventKind(&kind);args->get_VirtualKey(&key);
                if((kind!=COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN&&kind!=COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN)||!(GetKeyState(VK_CONTROL)&0x8000))return S_OK;
                const char* command=key=='L'?"address":key=='W'?"close-tab":key=='B'&&(GetKeyState(VK_SHIFT)&0x8000)?"new-tab":nullptr;
                if(command)if(auto parent=weakHost.lock())if(parent->trusted()){
                  args->put_Handled(TRUE);
                  auto* mainController=static_cast<ICoreWebView2Controller*>(webview_get_native_handle(static_cast<webview_t>(parent->owner),WEBVIEW_NATIVE_HANDLE_KIND_BROWSER_CONTROLLER));
                  if(mainController)mainController->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
                  auto message="{\"channel\":\"mdo-browser-shortcut\",\"key\":"+xb_quote(command)+",\"browser\":"+xb_quote(browser)+"}";
                  parent->main->PostWebMessageAsJson(webview::detail::widen_string(message).c_str());
                }return S_OK;
              });
            EventRegistrationToken acceleratorToken{};controller->add_AcceleratorKeyPressed(accelerator,&acceleratorToken);accelerator->Release();
            auto* navigation=xb_cb<ICoreWebView2NavigationStartingEventHandler,ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs*>(IID_ICoreWebView2NavigationStartingEventHandler,
              [weak](ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs* args){
                xs_wv_text uri;
                if(auto active=weak.lock()){
                  args->get_NavigationId(&active->navigation);active->loading=true;active->error.clear();active->errorCode=0;
                  if(SUCCEEDED(args->get_Uri(&uri.p))&&!xb_url(webview::detail::narrow_string(uri.p?uri.p:L""))){args->put_Cancel(TRUE);active->error="Navigation to a non-HTTP resource was blocked";active->loading=false;}
                }return S_OK;
              });
            EventRegistrationToken navigationToken{};view->core->add_NavigationStarting(navigation,&navigationToken);navigation->Release();
            auto* completed=xb_cb<ICoreWebView2NavigationCompletedEventHandler,ICoreWebView2*,ICoreWebView2NavigationCompletedEventArgs*>(IID_ICoreWebView2NavigationCompletedEventHandler,
              [weak](ICoreWebView2*,ICoreWebView2NavigationCompletedEventArgs* args){if(auto active=weak.lock()){
                UINT64 navigation=0;args->get_NavigationId(&navigation);if(navigation!=active->navigation)return S_OK;
                BOOL success=FALSE;args->get_IsSuccess(&success);active->loading=false;
                COREWEBVIEW2_WEB_ERROR_STATUS error=COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
                if(!success){args->get_WebErrorStatus(&error);active->errorCode=static_cast<int>(error);
                  if(error!=COREWEBVIEW2_WEB_ERROR_STATUS_OPERATION_CANCELED)active->error=xb_navigation_error(error);}
              }return S_OK;});
            EventRegistrationToken completedToken{};view->core->add_NavigationCompleted(completed,&completedToken);completed->Release();
            auto* failed=xb_cb<ICoreWebView2ProcessFailedEventHandler,ICoreWebView2*,ICoreWebView2ProcessFailedEventArgs*>(IID_ICoreWebView2ProcessFailedEventHandler,
              [weak](ICoreWebView2*,ICoreWebView2ProcessFailedEventArgs* args){if(auto active=weak.lock()){
                COREWEBVIEW2_PROCESS_FAILED_KIND kind;args->get_ProcessFailedKind(&kind);
                if(kind==COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED||kind==COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED){active->loading=false;active->failed=true;active->error="Browser process stopped; reload to restore the page";active->errorCode=static_cast<int>(kind);}
              }return S_OK;});
            EventRegistrationToken failedToken{};view->core->add_ProcessFailed(failed,&failedToken);failed->Release();
            auto* popup=xb_cb<ICoreWebView2NewWindowRequestedEventHandler,ICoreWebView2*,ICoreWebView2NewWindowRequestedEventArgs*>(IID_ICoreWebView2NewWindowRequestedEventHandler,
              [weak](ICoreWebView2*,ICoreWebView2NewWindowRequestedEventArgs* args){
                args->put_Handled(TRUE);xs_wv_text uri;
                if(auto active=weak.lock())if(SUCCEEDED(args->get_Uri(&uri.p))&&xb_url(webview::detail::narrow_string(uri.p?uri.p:L"")))active->core->Navigate(uri.p);return S_OK;
              });
            EventRegistrationToken popupToken{};view->core->add_NewWindowRequested(popup,&popupToken);popup->Release();
            if(!xb_place(view,visible)){host->views.erase(browser);host->reply(id,"","Cannot place content browser viewport");return S_OK;}
            HRESULT navigationResult=view->core->Navigate(webview::detail::widen_string(url).c_str());
            if(FAILED(navigationResult)){view->loading=false;view->error="Browser rejected the initial navigation";view->errorCode=static_cast<int>(navigationResult);}
            host->reply(id,"{\"created\":true}");return S_OK;
          });
        HRESULT result=env->CreateCoreWebView2Controller(view->window,ready);ready->Release();
        if(FAILED(result)){host->views.erase(browser);host->reply(id,"","Content controller request failed");}return S_OK;
      });
    HRESULT result=host->loader.create_environment_with_options(nullptr,host->profile.c_str(),nullptr,environment);environment->Release();
    if(FAILED(result)){host->views.erase(browser);host->reply(id,"","Content environment request failed");}return;
  }
  if(command=="close"){host->views.erase(browser);host->reply(id,"{}");return;}
  if(found==host->views.end()||found->second->creating){host->reply(id,"","Browser is not ready");return;}
  auto view=found->second;auto* core=view->core;
  if(command=="bounds"){
    view->bounds=xb_bounds(json);if(!xb_place(view,xb_get(json,"visible")!="false")){host->reply(id,"","Cannot place content browser viewport");return;}host->reply(id,"{}");return;
  }
  if(command=="state"){
    xs_wv_text url,title;core->get_Source(&url.p);core->get_DocumentTitle(&title.p);
    BOOL back=FALSE,forward=FALSE,visible=FALSE;core->get_CanGoBack(&back);core->get_CanGoForward(&forward);view->controller->get_IsVisible(&visible);
    HWND parent=nullptr;view->controller->get_ParentWindow(&parent);RECT actual{};view->controller->get_Bounds(&actual);
    const bool attached=parent==view->window&&GetParent(view->window)==host->window;
    POINT center{view->bounds.left+(view->bounds.right-view->bounds.left)/2,view->bounds.top+(view->bounds.bottom-view->bounds.top)/2};
    const bool topmost=ChildWindowFromPointEx(host->window,center,CWP_SKIPINVISIBLE|CWP_SKIPTRANSPARENT)==view->window;
    const auto viewport=",\"viewport_attached\":"+std::string(attached?"true":"false")+",\"viewport_topmost\":"+(topmost?"true":"false")+
      ",\"controller_bounds\":{\"x\":"+std::to_string(actual.left)+",\"y\":"+std::to_string(actual.top)+",\"width\":"+std::to_string(actual.right-actual.left)+",\"height\":"+std::to_string(actual.bottom-actual.top)+"}";
    const auto bounds="{\"x\":"+std::to_string(view->bounds.left)+",\"y\":"+std::to_string(view->bounds.top)+",\"width\":"+std::to_string(view->bounds.right-view->bounds.left)+",\"height\":"+std::to_string(view->bounds.bottom-view->bounds.top)+"}";
    host->reply(id,"{\"url\":"+xb_quote(webview::detail::narrow_string(url.p?url.p:L""))+",\"title\":"+xb_quote(webview::detail::narrow_string(title.p?title.p:L""))+",\"can_go_back\":"+(back?"true":"false")+",\"can_go_forward\":"+(forward?"true":"false")+",\"loading\":"+(view->loading?"true":"false")+",\"manual\":"+(view->manual?"true":"false")+",\"visible\":"+(visible?"true":"false")+viewport+",\"process_failed\":"+(view->failed?"true":"false")+",\"bounds\":"+bounds+",\"platform\":\"webview2\",\"error_code\":"+std::to_string(view->errorCode)+",\"error\":"+xb_quote(view->error)+"}");return;
  }
  if(command=="takeover"||command=="release"){
    if(command=="release"&&view->manual&&xb_get(json,"agent")=="true"){host->reply(id,"","Only the user can return manual control");return;}
    view->manual=command=="takeover";host->reply(id,view->manual?"{\"manual\":true}":"{\"manual\":false}");return;
  }
  const bool mutation=command!="snapshot"&&command!="screenshot";
  if(view->manual&&mutation&&xb_get(json,"agent")=="true"){host->reply(id,"","The user has taken control of this browser");return;}
  HRESULT code=S_OK;
  if(command=="navigate"){
    const auto url=xb_get(json,"url");if(!xb_url(url)){host->reply(id,"","Only HTTP(S) navigation is supported");return;}
    code=core->Navigate(webview::detail::widen_string(url).c_str());
  }else if(command=="back")code=core->GoBack();
  else if(command=="forward")code=core->GoForward();
  else if(command=="reload")code=core->Reload();
  else if(command=="stop")code=core->Stop();
  else if(command=="screenshot"){
    auto* callback=xb_cb<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler,HRESULT,LPCWSTR>(IID_ICoreWebView2CallDevToolsProtocolMethodCompletedHandler,
      [host,id](HRESULT status,LPCWSTR output){host->reply(id,SUCCEEDED(status)?webview::detail::narrow_string(output?output:L"{}"):"",FAILED(status)?"Screenshot failed":"");return S_OK;});
    code=core->CallDevToolsProtocolMethod(L"Page.captureScreenshot",L"{\"format\":\"jpeg\",\"quality\":65}",callback);callback->Release();if(SUCCEEDED(code))return;
  }else{
    std::string script;
    const auto selector=xb_quote(xb_get(json,"selector"));
    if(command=="snapshot")script="({url:location.href,title:document.title,ready:document.readyState,text:(document.body?.innerText||'').slice(0,200000),links:[...document.querySelectorAll('a[href]')].slice(0,100).map(a=>({text:a.innerText,url:a.href}))})";
    else if(command=="click")script="(()=>{let e="+(xb_get(json,"selector").empty()?"document.elementFromPoint("+xb_get(json,"x")+","+xb_get(json,"y")+")":"document.querySelector("+selector+")")+";if(!e)throw Error('Element not found');e.click();return true})()";
    else if(command=="type")script="(()=>{let e=document.querySelector("+selector+");if(!e)throw Error('Element not found');e.focus();e.value="+xb_quote(xb_get(json,"text"))+";e.dispatchEvent(new Event('input',{bubbles:true}));e.dispatchEvent(new Event('change',{bubbles:true}));return true})()";
    else if(command=="scroll"){
      auto number=[&](const char* key){try{return std::to_string(std::stoi(xb_get(json,key)));}catch(...){return std::string("0");}};
      script="(()=>{scrollBy("+number("x")+","+number("y")+");return true})()";
    }else if(command=="evaluate")script=xb_get(json,"script");
    else{host->reply(id,"","Unsupported browser command");return;}
    script="(()=>{try{return {ok:true,value:(()=>{"+(command=="evaluate"?"return ("+script+")":"return ("+script+")")+"})()}}catch(e){return {ok:false,error:String(e.message||e)}}})()";
    auto* callback=xb_cb<ICoreWebView2ExecuteScriptCompletedHandler,HRESULT,LPCWSTR>(IID_ICoreWebView2ExecuteScriptCompletedHandler,
      [host,id](HRESULT status,LPCWSTR output){
        const auto result=webview::detail::narrow_string(output?output:L"null");
        if(FAILED(status)||xb_get(result,"ok")!="true")host->reply(id,"",FAILED(status)?"Browser execution failed":xb_get(result,"error"));
        else host->reply(id,result);return S_OK;
      });
    code=core->ExecuteScript(webview::detail::widen_string(script).c_str(),callback);callback->Release();if(SUCCEEDED(code))return;
  }
  host->reply(id,SUCCEEDED(code)?"{\"accepted\":true}":"",FAILED(code)?"Browser action failed":"");
}
class xb_messages final:public ICoreWebView2WebMessageReceivedEventHandler{
  std::atomic<ULONG> refs{1};std::shared_ptr<xb_host> host;
public:
  explicit xb_messages(std::shared_ptr<xb_host> h):host(std::move(h)){}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(IsEqualIID(iid,xs_wv_iid_unknown)||IsEqualIID(iid,IID_ICoreWebView2WebMessageReceivedEventHandler)){*out=this;AddRef();return S_OK;}return E_NOINTERFACE;}
  ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
  ULONG STDMETHODCALLTYPE Release()override{auto n=--refs;if(!n)delete this;return n;}
  HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2*,ICoreWebView2WebMessageReceivedEventArgs* args)override{
    try{
      xs_wv_text source,message;
      if(!host->trusted()||FAILED(args->get_Source(&source.p))||xs_wv_document(source.p)!=host->document||FAILED(args->TryGetWebMessageAsString(&message.p)))return S_OK;
      auto json=webview::detail::narrow_string(message.p?message.p:L"");if(json.size()<=262144)xb_receive(host,json);
    }catch(...){return E_FAIL;}return S_OK;
  }
};
}
extern "C" int xsWvEnableBrowser(void* view,const char* url,const char* profile){
  try{
    if(!view)return 0;
    {std::lock_guard<std::mutex> lock(xb_hosts_mutex);if(xb_hosts.count(view))return 0;}
    auto host=std::make_shared<xb_host>();host->owner=view;host->document=xs_wv_document(webview::detail::widen_string(url?url:"").c_str());
    if(!xs_wv_local_document(host->document)||!profile||!*profile)return 0;
    host->profile=webview::detail::widen_string(std::string(profile)+"-content");
    auto* controller=static_cast<ICoreWebView2Controller*>(webview_get_native_handle(static_cast<webview_t>(view),WEBVIEW_NATIVE_HANDLE_KIND_BROWSER_CONTROLLER));
    if(!controller||FAILED(controller->get_ParentWindow(&host->window))||!host->window)return 0;
    xs_wv_com<ICoreWebView2> core;if(!controller||FAILED(controller->get_CoreWebView2(core.out())))return 0;host->main=core.p;
    auto* handler=new xb_messages(host);EventRegistrationToken token{};auto result=core->add_WebMessageReceived(handler,&token);handler->Release();
    if(FAILED(result))return 0;{std::lock_guard<std::mutex> lock(xb_hosts_mutex);xb_hosts[view]=host;}return 1;
  }catch(...){return 0;}
}
extern "C" void xsWvDestroyBrowsers(void* view){
  std::shared_ptr<xb_host> host;
  {std::lock_guard<std::mutex> lock(xb_hosts_mutex);auto found=xb_hosts.find(view);if(found==xb_hosts.end())return;host=found->second;xb_hosts.erase(found);}
  host->alive=false;host->views.clear();host->main=nullptr;
}
