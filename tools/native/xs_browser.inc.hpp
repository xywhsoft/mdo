/* Native content views. Included after xs_directory_drop.inc.hpp. The bridge
 * accepts messages only from the original main document; content has no host
 * bindings and lives in an independent persistent browser environment. */
#include <functional>
#include <map>
#include <memory>
#include <algorithm>
#include <mutex>
#include <cmath>
#include <vector>
#include <cstdint>
#include <wincrypt.h>
#include <sddl.h>
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
struct xb_input_sequence;
static void xb_dialog_cleanup(HWND window);
struct xb_dialog {
  ICoreWebView2ScriptDialogOpeningEventArgs* args=nullptr;
  ICoreWebView2Deferral* deferral=nullptr;
  std::string id;ULONGLONG expires=0;
  ~xb_dialog(){if(deferral){deferral->Complete();deferral->Release();}if(args)args->Release();}
};

static void xb_popup_cleanup(HWND window);
struct xb_popup {
  ICoreWebView2NewWindowRequestedEventArgs* args=nullptr;
  ICoreWebView2Deferral* deferral=nullptr;
  std::string id,url;ULONGLONG expires=0;bool claimed=false,completed=false,userInitiated=false;
  HRESULT complete(ICoreWebView2* window=nullptr){
    if(completed||!args)return E_ABORT;completed=true;
    HRESULT status=window?args->put_NewWindow(window):S_OK;
    if(deferral){HRESULT end=deferral->Complete();if(SUCCEEDED(status))status=end;deferral->Release();deferral=nullptr;}
    args->Release();args=nullptr;return status;
  }
  ~xb_popup(){complete();}
};

struct xb_chooser_operation;
struct xb_world_context;
struct xb_chooser_pending{
  std::string id,frame,session,loader,document,ref,bindingKey;std::vector<std::pair<std::string,std::string>> frameGuard;
  std::shared_ptr<xb_world_context> world,rootWorld;
  int backend=0;bool multiple=false;UINT64 navigation=0;ULONGLONG expires=0;
};
struct xb_chooser_state{
  ICoreWebView2DevToolsProtocolEventReceiver* receiver=nullptr;EventRegistrationToken token{};
  bool subscribed=false,supported=false,enabled=false,wanted=false,changing=false,policyFailed=false,objectFailed=false,cleanupWanted=false;
  unsigned lease=120000,ignored=0;uint64_t epoch=0;std::string error;
  std::shared_ptr<xb_chooser_pending> pending,deferredCancel;std::weak_ptr<xb_chooser_operation> operation;
  void unsubscribe(){if(receiver){if(subscribed)receiver->remove_DevToolsProtocolEventReceived(token);receiver->Release();receiver=nullptr;}subscribed=false;}
  ~xb_chooser_state(){unsubscribe();}
};

struct xb_download_store;
struct xb_download_arm_job;
struct xb_download_protocol{std::string url,frame,name,state;bool bound=false,retired=false;unsigned begins=0;ULONGLONG updated=0;};
struct xb_download_state{
  std::weak_ptr<xb_download_arm_job> arm;
  ICoreWebView2_4* source=nullptr;EventRegistrationToken token{};
  ICoreWebView2DevToolsProtocolEventReceiver* beginReceiver=nullptr;ICoreWebView2DevToolsProtocolEventReceiver* progressReceiver=nullptr;EventRegistrationToken beginToken{},progressToken{};
  bool subscribed=false,supported=false,armed=false,protocolReady=false,arming=false,beginSubscribed=false,progressSubscribed=false;UINT64 navigation=0;std::string navigationUrl;bool navigationDownload=false;unsigned offerTimeout=120000,activeProtocol=0;uint64_t sequence=0,protocolBegins=0,identityRejected=0;std::string error;
  std::map<std::string,xb_download_protocol> protocol;
  void retire(xb_download_protocol& item){if(!item.url.empty()&&activeProtocol)activeProtocol--;item.bound=item.retired=true;item.url.clear();item.frame.clear();item.name.clear();}
  void unsubscribe(){if(beginReceiver){if(beginSubscribed)beginReceiver->remove_DevToolsProtocolEventReceived(beginToken);beginReceiver->Release();beginReceiver=nullptr;}if(progressReceiver){if(progressSubscribed)progressReceiver->remove_DevToolsProtocolEventReceived(progressToken);progressReceiver->Release();progressReceiver=nullptr;}if(source){if(subscribed)source->remove_DownloadStarting(token);source->Release();source=nullptr;}subscribed=beginSubscribed=progressSubscribed=false;}
  ~xb_download_state(){unsubscribe();}
};

struct xb_capture_job;
struct xb_drag_job;
struct xb_upload_store;
struct xb_upload_assignment;
struct xb_frame_job;
struct xb_frame_receiver{
  ICoreWebView2DevToolsProtocolEventReceiver* source=nullptr;EventRegistrationToken token{};bool subscribed=false;
  ~xb_frame_receiver(){if(source){if(subscribed)source->remove_DevToolsProtocolEventReceived(token);source->Release();}}
};
struct xb_frame_node{
  std::string raw,parent,id,document,loader,url,name,session;bool loading=false;
};
struct xb_frame_source_identity{bool alive=true;};
struct xb_frame_session{
  std::string target,parent,error;bool ready=false,chooserEnabled=false;
  std::shared_ptr<xb_frame_source_identity> identity=std::make_shared<xb_frame_source_identity>();
};
struct xb_frame_state{
  ICoreWebView2_11* source=nullptr;bool ready=false,starting=false,closed=false;std::string root,error;
  uint64_t generation=0;std::vector<std::shared_ptr<xb_frame_receiver>> receivers;
  std::map<std::string,xb_frame_node> nodes;std::map<std::string,xb_frame_session> sessions;
  std::map<std::string,xb_frame_node> previousNodes;
  std::map<std::string,bool> loading;
  std::map<std::string,std::shared_ptr<xb_frame_job>> jobs;
  ~xb_frame_state(){receivers.clear();if(source)source->Release();}
};

/* Private native execution-world identities. A numeric context ID is metadata,
 * never an execution selector. Retired pins cannot be rebound to a new realm. */
struct xb_world_context{
  std::string name,frame,loader,unique,session;int numeric=0;bool alive=true,registered=false,admitted=false,retired=false,inventoryLost=false,functions=false;
  std::shared_ptr<xb_frame_source_identity> sourceOwner;
};
struct xb_world_receiver{
  ICoreWebView2DevToolsProtocolEventReceiver* source=nullptr;EventRegistrationToken token{};bool subscribed=false;
  ~xb_world_receiver(){if(source){if(subscribed)source->remove_DevToolsProtocolEventReceived(token);source->Release();}}
};
struct xb_world_request;
struct xb_world_document{std::string loader,frame,session;unsigned created=0;};
struct xb_world_state{
  bool enabled=false,enabling=false,closed=false;std::string error;
  std::vector<std::shared_ptr<xb_world_receiver>> receivers;
  std::map<std::string,std::weak_ptr<xb_world_context>> names;
  std::map<std::string,bool> seen;unsigned created=0,documentWorlds=0;std::string documentProof;
  unsigned otherFrames=0,refreshes=0;
  std::shared_ptr<xb_world_context> original;
  std::shared_ptr<xb_world_request> pending;
  std::map<std::string,std::shared_ptr<xb_world_request>> framePending;
  std::map<std::string,std::shared_ptr<xb_world_context>> frameWorlds;
  std::map<std::string,xb_world_document> frameDocuments;
  std::map<std::string,std::weak_ptr<xb_frame_source_identity>> enabledSources;
};

// UI-thread creation reservations outlive tab removal until the provider's
// terminal callback or destruction. They never borrow a dead host's counter.
class xb_creation_budget {
  unsigned count_=0;friend class xb_creation_state;
public:
  static constexpr unsigned limit=16;
  unsigned pending()const{return count_;}
};
class xb_creation_state {
public:
  enum class phase {environment,controller,interception,ready,failed,cancelled};
private:
  std::shared_ptr<xb_creation_budget> reservation_;
  phase phase_=phase::environment;unsigned long long started_=0,ended_=0;bool begun_=false,finished_=false;
  void release(){auto held=std::move(reservation_);if(held)--held->count_;}
public:
  xb_creation_state()=default;
  xb_creation_state(const xb_creation_state&)=delete;xb_creation_state& operator=(const xb_creation_state&)=delete;
  ~xb_creation_state(){release();}
  bool acquire(const std::shared_ptr<xb_creation_budget>& budget,unsigned long long now){
    if(begun_||!budget||budget->count_>=xb_creation_budget::limit)return false;
    reservation_=budget;++budget->count_;begun_=true;started_=now;return true;
  }
  bool advance(phase next){
    if(!reservation_||finished_||phase_==phase::cancelled||
       (next!=phase::controller&&next!=phase::interception)||static_cast<int>(next)!=static_cast<int>(phase_)+1)return false;
    phase_=next;return true;
  }
  void cancel(){if(begun_&&!finished_)phase_=phase::cancelled;}
  void finish(bool closed,bool ok,unsigned long long now){
    if(!begun_||finished_)return;
    finished_=true;ended_=now;
    phase_=closed||phase_==phase::cancelled?phase::cancelled:ok?phase::ready:phase::failed;release();
  }
  bool reserved()const{return bool(reservation_);}
  const char* name()const{
    if(!begun_)return "none";
    switch(phase_){case phase::environment:return "environment";case phase::controller:return "controller";
      case phase::interception:return "interception";case phase::ready:return "ready";case phase::failed:return "failed";case phase::cancelled:return "cancelled";}
    return "failed";
  }
  unsigned long long elapsed(unsigned long long now)const{
    if(!begun_)return 0;
    const auto end=finished_?ended_:now;return end>=started_?end-started_:0;
  }
};

/* A failed renderer can recover only through a newly started, successfully
 * completed native navigation. A browser-process failure requires recreation.
 * This state never initiates navigation or replays a page operation. */
struct xb_process_failure_state {
  bool renderer=false;
  std::uint64_t failedNavigation=0,recoveryNavigation=0;
  void record(bool rendererExited,std::uint64_t navigation)noexcept{
    renderer=rendererExited;failedNavigation=navigation;recoveryNavigation=0;
  }
  void started(std::uint64_t navigation)noexcept{
    recoveryNavigation=renderer&&navigation&&navigation!=failedNavigation?navigation:0;
  }
  bool completed(std::uint64_t navigation,bool succeeded)noexcept{
    if(!renderer||!recoveryNavigation||navigation!=recoveryNavigation)return false;
    recoveryNavigation=0;
    if(!succeeded)return false;
    renderer=false;failedNavigation=0;return true;
  }
};

struct xb_view{
  xb_process_failure_state processFailure;
  std::string creationRequest;xb_creation_state creation;
  ICoreWebView2Controller* controller=nullptr;ICoreWebView2* core=nullptr;HWND window=nullptr;bool creating=true,manual=false,loading=true,failed=false;RECT bounds{};std::string error;int errorCode=0;UINT64 navigation=0;int automationContext=0;std::shared_ptr<xb_world_context> automationWorld;std::shared_ptr<xb_world_state> worlds;bool frameAutomation=false;std::shared_ptr<xb_frame_state> frames;bool initialNavigationPending=false,initialNavigationStarted=false;std::vector<std::string> navigationEvents;bool closed=false,dialogsEnabled=false,popupsEnabled=false,requestedClose=false;ULONGLONG closeRequestedAt=0;std::shared_ptr<xb_dialog> dialog;std::weak_ptr<xb_input_sequence> input;std::map<std::string,bool> automationRequests;ICoreWebView2Environment* environment=nullptr;std::shared_ptr<xb_popup> opening;std::map<std::string,std::shared_ptr<xb_popup>> popups;std::string opener,popupError,popupTicket;std::shared_ptr<xb_capture_job> capture;std::shared_ptr<xb_drag_job> drag;bool uploadBusy=false,uploadCleanupFailed=false;std::weak_ptr<xb_upload_assignment> uploadAssignment;std::string uploadAssignmentKey;std::shared_ptr<xb_chooser_state> chooser;std::shared_ptr<xb_download_state> downloads;
  ~xb_view(){for(auto& item:popups)item.second->complete();if(opening)opening->complete();xb_popup_cleanup(window);if(environment)environment->Release();dialog.reset();xb_dialog_cleanup(window);if(controller){controller->Close();controller->Release();}if(core)core->Release();if(window)DestroyWindow(window);}
};
struct xb_host{
  std::shared_ptr<xb_creation_budget> creations=std::make_shared<xb_creation_budget>();
  void* owner=nullptr;HWND window=nullptr;ICoreWebView2* main=nullptr;bool alive=true;unsigned captures=0;std::shared_ptr<xb_upload_store> uploads;std::shared_ptr<xb_download_store> downloads;std::wstring document,profile;
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
static void xb_upload_boundary(const std::shared_ptr<xb_view>&,bool);
static void xb_upload_abort_view(const std::shared_ptr<xb_view>&);
static bool xb_chooser_blocked(const std::shared_ptr<xb_view>&);
static bool xb_chooser_upload_ready(const std::shared_ptr<xb_view>&,const std::string&,const std::string&,const std::string&,const std::string& = "");
static bool xb_chooser_upload_consume(const std::shared_ptr<xb_view>&,const std::string&,const std::string&,const std::string&,const std::string& = "");
static void xb_chooser_boundary(const std::shared_ptr<xb_view>&,bool);
#include "xs_browser_automation.inc.hpp"
#include "xs_browser_frames.inc.hpp"
#include "xs_browser_popups.inc.hpp"
#include "xs_browser_capture.inc.hpp"
#include "xs_browser_drag.inc.hpp"
#include "xs_browser_navigation.inc.hpp"
#include "xs_browser_upload.inc.hpp"
#include "xs_browser_upload_assign.inc.hpp"
#include "xs_browser_chooser.inc.hpp"
#include "xs_browser_download.inc.hpp"
// Creation owns its original reply identity until one terminal receipt. Close
// settles it before waiting for a provider callback, and callbacks may not replay
// that receipt after cancellation or touch a replacement's request identity.
template<class Host,class View>
static bool xb_creation_settle(const Host& host,const View& view,const std::string& result,const std::string& error=""){
  if(view->creationRequest.empty())return false;
  const auto request=std::move(view->creationRequest);view->creationRequest.clear();
  host->reply(request,result,error);return true;
}
template<class Host,class View>
static bool xb_creation_finish(const Host& host,const View& view,unsigned long long now,const std::string& result,const std::string& error=""){
  view->creation.finish(view->closed,error.empty(),now);return xb_creation_settle(host,view,result,error);
}
template<class Creation>
static std::string xb_creation_json(const Creation& creation,unsigned long long now){
  return ",\"creation\":{\"phase\":\""+std::string(creation.name())+"\",\"elapsed_ms\":"+std::to_string(creation.elapsed(now))+"}";
}
template<class Host,class View>
static void xb_creation_cancel(const Host& host,const View& view){
  auto opening=std::move(view->opening);
  try{xb_creation_settle(host,view,"","Browser creation was cancelled");}
  catch(...){if(opening)opening->complete();throw;}
  if(opening)opening->complete();
}

static void xb_receive(const std::shared_ptr<xb_host>& host,const std::string& json){
  const auto id=xb_get(json,"id"),command=xb_get(json,"command"),browser=xb_get(json,"browser");
  if(id.size()!=36||xb_get(json,"channel")!="mdo-browser")return;
  if(command=="capabilities"){host->reply(id,"{\"native\":true,\"snapshot\":true,\"screenshot\":true,\"input\":true,\"profile_isolation\":true,\"initial_download_interception\":true,\"creation_capacity\":{\"limit\":16,\"pending\":"+std::to_string(host->creations->pending())+"}}");return;}
  if(browser.empty()||browser.size()>100){host->reply(id,"","Invalid browser identity");return;}
  auto found=host->views.find(browser);
  if(command=="create"){
    if(found!=host->views.end()){
      auto view=found->second;
      if(!xb_get(json,"popup").empty()&&(view->popupTicket!=xb_get(json,"popup")||view->opener!=xb_get(json,"opener"))){host->reply(id,"","Popup target browser already exists");return;}
      if(view->creating){host->reply(id,"","Browser creation is still in progress");return;}
      if(view->controller){view->bounds=xb_bounds(json);if(!xb_place(view,xb_get(json,"visible")!="false")){host->reply(id,"","Cannot place content browser viewport");return;}}
      host->reply(id,"{\"attached\":true}");return;
    }
    if(host->views.size()>=8){host->reply(id,"","Native browser view limit reached (8)");return;}
    std::shared_ptr<xb_popup> popup;std::shared_ptr<xb_view> opener;
    const auto popupId=xb_get(json,"popup");
    if(!popupId.empty()){
      auto source=host->views.find(xb_get(json,"opener"));
      if(source==host->views.end()||source->second->closed||!source->second->environment){host->reply(id,"","Popup opener is no longer available");return;}
      opener=source->second;auto item=opener->popups.find(popupId);
      if(item==opener->popups.end()||item->second->completed||item->second->claimed||GetTickCount64()>=item->second->expires){host->reply(id,"","Popup request is no longer available");return;}
      popup=item->second;
    }
    const auto url=popup?popup->url:xb_get(json,"url");const bool visible=xb_get(json,"visible")!="false";
    if(!xb_url(url)){host->reply(id,"","Only HTTP(S) navigation is supported");return;}
    const bool interceptDownloads=xb_get(json,"intercept_downloads")=="true"||(opener&&opener->downloads&&opener->downloads->armed);
    auto view=std::make_shared<xb_view>();if(!view->creation.acquire(host->creations,GetTickCount64())){host->reply(id,"","Native browser creation limit reached (16); wait for pending provider callbacks");return;}view->initialNavigationPending=url!="about:blank";view->bounds=xb_bounds(json);if(popup){popup->claimed=true;view->opening=popup;view->opener=xb_get(json,"opener");view->popupTicket=popup->id;view->manual=opener->manual;}
    view->window=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,
      0,0,1,1,host->window,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!view->window){host->reply(id,"","Cannot create content browser viewport");return;}host->views[browser]=view;view->creationRequest=id;
    auto* environment=xb_cb<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,HRESULT,ICoreWebView2Environment*>(IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,
      [host,view,id,browser,url,visible,popup,interceptDownloads](HRESULT status,ICoreWebView2Environment* env){
        if(!host->alive||view->closed||host->views.find(browser)==host->views.end()||host->views.at(browser)!=view||(popup&&popup->completed)){if(host->views.count(browser)&&host->views.at(browser)==view)host->views.erase(browser);view->closed=true;xb_creation_finish(host,view,GetTickCount64(),"","Browser creation was cancelled");return S_OK;}
        if(FAILED(status)||!env){host->views.erase(browser);xb_creation_finish(host,view,GetTickCount64(),"","Cannot initialize content browser environment");return S_OK;}
        view->environment=env;env->AddRef();view->creation.advance(xb_creation_state::phase::controller);
        auto* ready=xb_cb<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,HRESULT,ICoreWebView2Controller*>(IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,
          [host,view,id,browser,url,visible,popup,interceptDownloads](HRESULT code,ICoreWebView2Controller* controller){
            if(!host->alive||view->closed||host->views.find(browser)==host->views.end()||host->views.at(browser)!=view||(popup&&popup->completed)){if(controller)controller->Close();if(host->views.count(browser)&&host->views.at(browser)==view)host->views.erase(browser);view->closed=true;xb_creation_finish(host,view,GetTickCount64(),"","Browser creation was cancelled");return S_OK;}
            if(FAILED(code)||!controller){host->views.erase(browser);xb_creation_finish(host,view,GetTickCount64(),"","Cannot create content browser view");return S_OK;}
            controller->AddRef();view->controller=controller;controller->get_CoreWebView2(&view->core);if(view->core){xs_wv_com<ICoreWebView2_11> frameSupport;view->frameAutomation=SUCCEEDED(view->core->QueryInterface(IID_ICoreWebView2_11,reinterpret_cast<void**>(frameSupport.out())));}
            if(!view->core){host->views.erase(browser);xb_creation_finish(host,view,GetTickCount64(),"","Content browser is unavailable");return S_OK;}
            xs_wv_com<ICoreWebView2Settings> settings;
            if(SUCCEEDED(view->core->get_Settings(settings.out()))){settings->put_IsWebMessageEnabled(FALSE);settings->put_AreHostObjectsAllowed(FALSE);settings->put_IsStatusBarEnabled(FALSE);}
            xb_install_dialogs(host,view);xb_install_chooser(host,view);xb_install_downloads(host,view);
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
                  xs_wv_text first;UINT64 incoming=0;if(FAILED(args->get_NavigationId(&incoming))||FAILED(args->get_Uri(&first.p))){args->put_Cancel(TRUE);active->initialNavigationPending=false;active->loading=false;active->error="Cannot identify browser navigation";return S_OK;}if(!xb_navigation_start(active,incoming,webview::detail::narrow_string(first.p?first.p:L"")=="about:blank"))return S_OK;if(active->downloads){active->downloads->navigation=incoming;active->downloads->navigationUrl=webview::detail::narrow_string(xb_download_text(first.p,8192));active->downloads->navigationDownload=false;}active->loading=true;active->automationContext=0;active->automationWorld.reset();xb_frames_navigation(active);active->error.clear();active->errorCode=0;
                  if(SUCCEEDED(args->get_Uri(&uri.p))&&!xb_url(webview::detail::narrow_string(uri.p?uri.p:L""))){args->put_Cancel(TRUE);active->error="Navigation to a non-HTTP resource was blocked";active->loading=false;}
                }return S_OK;
              });
            EventRegistrationToken navigationToken{};view->core->add_NavigationStarting(navigation,&navigationToken);navigation->Release();
            auto* completed=xb_cb<ICoreWebView2NavigationCompletedEventHandler,ICoreWebView2*,ICoreWebView2NavigationCompletedEventArgs*>(IID_ICoreWebView2NavigationCompletedEventHandler,
              [weak](ICoreWebView2*,ICoreWebView2NavigationCompletedEventArgs* args){if(auto active=weak.lock()){
                UINT64 navigation=0;if(FAILED(args->get_NavigationId(&navigation))){active->initialNavigationPending=false;active->loading=false;active->error="Cannot identify browser navigation completion";return S_OK;}if(!xb_navigation_complete(active,navigation))return S_OK;
                BOOL success=FALSE;args->get_IsSuccess(&success);active->loading=false;if(active->processFailure.completed(navigation,success!=FALSE)){active->failed=false;active->error.clear();active->errorCode=0;}
                COREWEBVIEW2_WEB_ERROR_STATUS error=COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
                if(!success){args->get_WebErrorStatus(&error);active->errorCode=static_cast<int>(error);
                  if(error!=COREWEBVIEW2_WEB_ERROR_STATUS_OPERATION_CANCELED&&!xb_download_navigation_abort(active,navigation,error))active->error=xb_navigation_error(error);}
              }return S_OK;});
            EventRegistrationToken completedToken{};view->core->add_NavigationCompleted(completed,&completedToken);completed->Release();
            auto* failed=xb_cb<ICoreWebView2ProcessFailedEventHandler,ICoreWebView2*,ICoreWebView2ProcessFailedEventArgs*>(IID_ICoreWebView2ProcessFailedEventHandler,
              [weak](ICoreWebView2*,ICoreWebView2ProcessFailedEventArgs* args){if(auto active=weak.lock()){
                COREWEBVIEW2_PROCESS_FAILED_KIND kind=COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED;if(FAILED(args->get_ProcessFailedKind(&kind)))kind=COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED;
                if(kind==COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED||kind==COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED){active->loading=false;active->failed=true;active->automationContext=0;active->automationWorld.reset();active->processFailure.record(kind==COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED,active->navigation);active->error=kind==COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED?"The page renderer stopped; reload or reopen this browser to recover":"The browser process stopped; close and reopen this browser to recover";active->errorCode=static_cast<int>(kind);}
              }return S_OK;});
            EventRegistrationToken failedToken{};view->core->add_ProcessFailed(failed,&failedToken);failed->Release();
            xb_install_popups(host,view,browser);
            if(!xb_place(view,visible)){host->views.erase(browser);xb_creation_finish(host,view,GetTickCount64(),"","Cannot place content browser viewport");return S_OK;}
            view->creation.advance(xb_creation_state::phase::interception);
            const auto activate=[host,view,id,browser,url,popup](bool safe,const std::string& cause){
              auto reject=[&](const std::string& message){
                const auto current=host->views.find(browser);
                if(host->alive&&!view->closed&&current!=host->views.end()&&current->second==view){
                  view->closed=true;xb_download_close_view(host,view);xb_chooser_close(view);xb_cancel_popups(view);
                  if(view->opening)view->opening->complete();view->opening.reset();
                  if(view->controller)view->controller->Close();if(view->window)ShowWindow(view->window,SW_HIDE);host->views.erase(browser);
                }
                xb_creation_finish(host,view,GetTickCount64(),"",message);
              };
              if(!host->alive||view->closed||!host->views.count(browser)||host->views.at(browser)!=view||(popup&&popup->completed)){reject("Browser creation was cancelled before navigation");return;}
              if(!safe){reject(cause.empty()?"Cannot start this browser without download interception":cause);return;}
              view->creating=false;
              const auto flags=std::string(",\"downloads_intercepted\":")+(view->downloads&&view->downloads->armed?"true":"false")+",\"manual\":"+(view->manual?"true":"false");
              if(popup){
                view->loading=url!="about:blank";const auto adopted=popup->complete(view->core);view->opening.reset();
                if(FAILED(adopted)){reject("Cannot adopt the requested popup");return;}
                xb_creation_finish(host,view,GetTickCount64(),"{\"created\":true,\"popup\":true"+flags+"}");return;
              }
              const auto navigationResult=view->core->Navigate(webview::detail::widen_string(url).c_str());
              if(FAILED(navigationResult)){view->initialNavigationPending=false;view->loading=false;view->error="Browser rejected the initial navigation";view->errorCode=static_cast<int>(navigationResult);}
              xb_creation_finish(host,view,GetTickCount64(),"{\"created\":true"+flags+"}");
            };
            if(interceptDownloads&&!view->manual)xb_download_arm(host,view,activate);else activate(true,{});
            return S_OK;
          });
        HRESULT result=env->CreateCoreWebView2Controller(view->window,ready);ready->Release();
        if(FAILED(result)){host->views.erase(browser);xb_creation_finish(host,view,GetTickCount64(),"","Content controller request failed");}return S_OK;
      });
    HRESULT result=popup?environment->Invoke(S_OK,opener->environment):host->loader.create_environment_with_options(nullptr,host->profile.c_str(),nullptr,environment);environment->Release();
    if(FAILED(result)){host->views.erase(browser);xb_creation_finish(host,view,GetTickCount64(),"","Content environment request failed");}return;
  }
  if(command=="close"){if(found!=host->views.end()){found->second->closed=true;found->second->creation.cancel();xb_creation_cancel(host,found->second);xb_frames_close(found->second);xb_download_close_view(host,found->second);xb_chooser_close(found->second);xb_close_drag(found->second);xb_cancel_capture(found->second);xb_cancel_popups(found->second);xb_abort_automation(host,found->second,false);found->second->dialog.reset();xb_dialog_cleanup(found->second->window);if(found->second->controller)found->second->controller->Close();xb_download_drain_closed_view(host,found->second);xb_upload_close_view(host,found->second);if(found->second->window)ShowWindow(found->second->window,SW_HIDE);}host->views.erase(browser);host->reply(id,"{}");return;}
  if(command=="state"&&found!=host->views.end()&&found->second->creating){host->reply(id,"{\"creating\":true,\"loading\":true,\"automation\":false,\"visible\":false,\"platform\":\"webview2\""+xb_creation_json(found->second->creation,GetTickCount64())+"}");return;}
  if(found==host->views.end()||found->second->creating){host->reply(id,"","Browser is not ready");return;}
  auto view=found->second;xb_reap_retired_drag(view);auto* core=view->core;
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
    const auto viewport=std::string(",\"creating\":false")+xb_creation_json(view->creation,GetTickCount64())+",\"navigation\":"+xb_quote(std::to_string(view->navigation))+",\"automation\":true,\"automation_unique_world\":"+std::string(xb_world_live(view,view->automationWorld)?"true":"false")+",\"automation_world_error\":"+xb_quote(view->worlds?view->worlds->error:"")+",\"automation_world_metadata\":{\"other_frame_contexts\":"+std::to_string(view->worlds?view->worlds->otherFrames:0)+",\"original_context_refreshes\":"+std::to_string(view->worlds?view->worlds->refreshes:0)+"},\"frame_automation\":"+std::string(view->frameAutomation?"true":"false")+",\"frame_snapshot_input\":"+std::string(view->frameAutomation?"true":"false")+",\"frame_snapshot_drag\":"+std::string(view->frameAutomation?"true":"false")+",\"frame_snapshot_keyboard\":"+std::string(view->frameAutomation?"true":"false")+",\"frame_target_keyboard\":"+std::string(view->frameAutomation?"true":"false")+",\"viewport_attached\":"+std::string(attached?"true":"false")+",\"viewport_topmost\":"+(topmost?"true":"false")+
      ",\"controller_bounds\":{\"x\":"+std::to_string(actual.left)+",\"y\":"+std::to_string(actual.top)+",\"width\":"+std::to_string(actual.right-actual.left)+",\"height\":"+std::to_string(actual.bottom-actual.top)+"}";
    const auto bounds="{\"x\":"+std::to_string(view->bounds.left)+",\"y\":"+std::to_string(view->bounds.top)+",\"width\":"+std::to_string(view->bounds.right-view->bounds.left)+",\"height\":"+std::to_string(view->bounds.bottom-view->bounds.top)+"}";
    host->reply(id,"{\"url\":"+xb_quote(webview::detail::narrow_string(url.p?url.p:L""))+",\"title\":"+xb_quote(webview::detail::narrow_string(title.p?title.p:L""))+",\"can_go_back\":"+(back?"true":"false")+",\"can_go_forward\":"+(forward?"true":"false")+",\"loading\":"+((view->loading||view->initialNavigationPending)?"true":"false")+",\"manual\":"+(view->manual?"true":"false")+",\"visible\":"+(visible?"true":"false")+viewport+",\"navigation_events\":"+xb_navigation_events(view)+",\"initial_navigation_pending\":"+(view->initialNavigationPending?"true":"false")+",\"capture_capability\":true,\"advanced_mouse\":true,\"drag_capability\":true,\"upload_capability\":true,\"file_input_capability\":true,\"directory_upload_capability\":true,\"file_clear_capability\":true,\"chooser_upload_capability\":true,\"download_capability\":"+std::string(view->downloads&&view->downloads->supported?"true":"false")+",\"downloads_armed\":"+(view->downloads&&view->downloads->armed?"true":"false")+",\"download_sequence\":"+std::to_string(view->downloads?view->downloads->sequence:0)+",\"download_error\":"+xb_quote(view->downloads?view->downloads->error:"")+",\"chooser_capability\":"+std::string(view->chooser&&view->chooser->supported?"true":"false")+",\"chooser_armed\":"+(view->chooser&&view->chooser->enabled?"true":"false")+",\"chooser_open\":"+(view->chooser&&view->chooser->pending?"true":"false")+",\"chooser_cleanup_failed\":"+(view->chooser&&(view->chooser->policyFailed||view->chooser->objectFailed)?"true":"false")+",\"upload_cleanup_failed\":"+std::string(view->uploadCleanupFailed?"true":"false")+",\"drag_cleanup_failed\":"+std::string(view->drag&&view->drag->cleanupFailed?"true":"false")+",\"capture_busy\":"+std::string(host->captures?"true":"false")+",\"dialog_capability\":"+(view->dialogsEnabled?"true":"false")+",\"popup_capability\":"+(view->popupsEnabled?"true":"false")+",\"popup_open\":"+(xb_popup_blocked(view)?"true":"false")+",\"closed_by_page\":"+(view->requestedClose&&!xb_download_holds_page_close(host,view)?"true":"false")+",\"close_requested\":"+(view->requestedClose?"true":"false")+",\"opener\":"+xb_quote(view->opener)+",\"input_busy\":"+(view->input.expired()&&!view->drag&&!view->uploadBusy&&!(view->chooser&&(view->chooser->changing||view->chooser->deferredCancel||view->chooser->cleanupWanted))?"false":"true")+",\"dialog_open\":"+(view->dialog?"true":"false")+",\"process_failed\":"+(view->failed?"true":"false")+",\"bounds\":"+bounds+",\"platform\":\"webview2\",\"error_code\":"+std::to_string(view->errorCode)+",\"error\":"+xb_quote(view->error)+"}");return;
  }
  if(command=="takeover"||command=="release"){
    if(command=="release"&&view->manual&&xb_get(json,"agent")=="true"){host->reply(id,"","Only the user can return manual control");return;}
    view->manual=command=="takeover";if(view->manual){xb_chooser_takeover(host,view);xb_download_takeover(host,view);}host->reply(id,view->manual?"{\"manual\":true}":"{\"manual\":false}");return;
  }
  if(view->requestedClose&&xb_get(json,"agent")=="true"&&(command=="navigate"||command=="back"||command=="forward"||command=="reload"||command=="input"||command=="automation"||command=="evaluate")){host->reply(id,"","The page requested closure; only its retained downloads remain available");return;}
  const bool mutation=command!="downloads"&&command!="chooser"&&command!="upload"&&command!="drag_cancel"&&command!="capture"&&command!="capture_cancel"&&command!="snapshot"&&command!="screenshot"&&!((command=="dialog"||command=="popups")&&xb_get(json,"action")=="list");
  if(view->manual&&mutation&&xb_get(json,"agent")=="true"){host->reply(id,"","The user has taken control of this browser");return;}
  if(command=="downloads"){xb_download_request(host,view,id,json);return;}
  if(command=="chooser"){xb_chooser_request(host,view,id,json);return;}
  if(command=="upload"){xb_upload_request(host,view,id,json);return;}
  if(command=="drag_cancel"){auto job=view->drag;if(job&&job->key==xb_get(json,"drag_key"))job->stop("Native drag was cancelled; inspect delivered effects");host->reply(id,"{}");return;}
  if(command=="drag"){xb_drag(host,view,id,json);return;}
  if(command=="capture_cancel"){auto job=view->capture;if(job&&job->key==xb_get(json,"capture_key"))job->finish("","Screenshot was cancelled");host->reply(id,"{}");return;}
  if(command=="capture"){xb_capture(host,view,id,json);return;}
  if(command=="popups"){xb_popup_request(host,view,id,json);return;}
  if(command=="dialog"){xb_dialog_request(host,view,id,json);return;}
  if(command=="frames"){xb_frames_request(host,view,id,json);return;}
  if(command=="automation"){if(view->dialog){host->reply(id,"","A JavaScript dialog blocks page execution");return;}xb_automation(host,view,id,xb_get(json,"script"));return;}
  if(command=="input"){xb_input(host,view,id,json);return;}
  HRESULT code=S_OK;
  if(command=="navigate"){
    const auto url=xb_get(json,"url");if(!xb_url(url)){host->reply(id,"","Only HTTP(S) navigation is supported");return;}
    code=core->Navigate(webview::detail::widen_string(url).c_str());
  }else if(command=="back")code=core->GoBack();
  else if(command=="forward")code=core->GoForward();
  else if(command=="reload")code=core->Reload();
  else if(command=="stop"){view->initialNavigationPending=false;view->loading=false;code=core->Stop();}
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
  host->alive=false;for(auto& entry:host->views){entry.second->closed=true;xb_frames_close(entry.second);xb_download_close_view(host,entry.second);xb_chooser_close(entry.second);xb_close_drag(entry.second);xb_cancel_capture(entry.second);xb_cancel_popups(entry.second);entry.second->dialog.reset();xb_dialog_cleanup(entry.second->window);if(entry.second->controller)entry.second->controller->Close();xb_download_drain_closed_view(host,entry.second);xb_upload_close_view(host,entry.second);}xb_download_shutdown(host);xb_upload_shutdown(host);host->views.clear();host->main=nullptr;
}
