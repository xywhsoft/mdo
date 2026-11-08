/* One bounded native gesture. Drag data comes only from the page's intercepted
 * browser event; callers cannot supply data, files, scripts or protocol names. */
static constexpr UINT_PTR xb_drag_timer=0xBADD4;
struct xb_drag_job;
static std::map<HWND,std::weak_ptr<xb_drag_job>> xb_drag_jobs;
static bool xb_drag_url(const std::string& url){
  if(url=="about:blank")return true;
  size_t start=url.rfind("https://",0)==0?8:url.rfind("http://",0)==0?7:0;
  if(!start||url.size()>8192)return false;auto end=url.find_first_of("/?#",start);if(end==std::string::npos)end=url.size();
  if(end==start||url.substr(start,end-start).find('@')!=std::string::npos)return false;
  for(unsigned char c:url)if(c<=32||c=='\\')return false;return true;
}
static bool xb_drag_uri_list(const std::string& data){
  size_t start=0;unsigned lines=0;
  while(start<data.size()){
    if(++lines>32)return false;auto end=data.find('\n',start);if(end==std::string::npos)end=data.size();
    auto line=data.substr(start,end-start);if(!line.empty()&&line.back()=='\r')line.pop_back();
    if(!line.empty()&&line.front()!='#'&&!xb_drag_url(line))return false;start=end+1;
  }return true;
}
struct xb_drag_job:std::enable_shared_from_this<xb_drag_job>{
  std::shared_ptr<xb_host> host;std::shared_ptr<xb_view> view;
  ICoreWebView2DevToolsProtocolEventReceiver* receiver=nullptr;EventRegistrationToken token{};
  std::string id,key,error,dragData,sourceSession,eventSession,sourceRaw,sourceLoader;xb_input_frame_guard sourceGuard,destinationGuard;xb_input_frame_snapshot frameSnapshot;UINT64 navigation=0;ULONGLONG expires=0,nextAt=0;
  double x=0,y=0,toX=0,toY=0,currentX=0,currentY=0;unsigned phase=0,moved=0,cleanup=0;
  LONG width=0,height=0;
  bool done=false,inFlight=false,replied=false,dispatched=false,intercepting=false,sourceIntercepting=false,intercepted=false,pressed=false,dropped=false,subscribed=false,cleanupFailed=false,watchCleanupFailed=false,nativeCleanupFailed=false;
  std::shared_ptr<xb_world_context> watchWorld;bool watchStarted=false,watchInstalled=false,watchChecked=false,watchActive=false,watchCleared=false;
  ~xb_drag_job(){if(receiver){if(subscribed)receiver->remove_DevToolsProtocolEventReceived(token);receiver->Release();}}
  bool live()const{return xb_automation_live(host,view,navigation)&&xb_input_frames_live(view,sourceGuard)&&xb_input_frames_live(view,destinationGuard)&&xb_input_snapshot_live(view,frameSnapshot)&&view->bounds.right-view->bounds.left==width&&view->bounds.bottom-view->bounds.top==height;}
  void reply(){
    if(replied)return;replied=true;
    host->reply(id,"{\"accepted\":"+std::string(error.empty()?"true":"false")+",\"trusted_input\":true,\"dispatched\":"+
      (dispatched?"true":"false")+",\"drag_kind\":"+xb_quote(dragData.empty()?"pointer":"html5")+
      (error.empty()?"":",\"error\":"+xb_quote(error))+"}");
  }
  void detach(bool retain=false){
    done=true;KillTimer(view->window,xb_drag_timer);xb_drag_jobs.erase(view->window);
    if(receiver){if(subscribed)receiver->remove_DevToolsProtocolEventReceived(token);receiver->Release();receiver=nullptr;}
    if(!retain&&view->drag.get()==this)view->drag.reset();
  }
  void close(){if(error.empty())error="Browser was closed during drag; inspect any delivered effects";reply();detach();}
  void stop(const std::string& reason){if(error.empty())error=reason;reply();nextAt=0;}
  std::string point()const{return ",\"x\":"+std::to_string(currentX)+",\"y\":"+std::to_string(currentY);}
  void call(const wchar_t* method,const std::string& params,const std::string& session=""){
    // A destroyed source session has no drag controller/interception to release.
    // The root mouse-release step still drains independently of frame liveness.
    if(!session.empty()&&(!watchWorld||watchWorld->session!=session||!xb_world_source_live(view,session,watchWorld->sourceOwner))){
      if(!cleanup)stop("The original native drag source is no longer attached");nextAt=0;return;
    }
    inFlight=true;auto self=shared_from_this();
    xb_frame_cdp(view,session,method,params,[self](bool ok,const std::string& result){
      self->inFlight=false;if(self->done)return;
      if(!ok||!xb_get(result,"error").empty()){
        if(self->cleanup){self->nativeCleanupFailed=true;self->cleanupFailed=true;}
        self->stop("Native drag delivery was not confirmed; inspect before repeating");
      }
      self->nextAt=GetTickCount64()+20;
    });
  }
  bool sourceLive()const{return host->alive&&!view->closed&&view->core&&view->navigation==navigation&&xb_input_frames_live(view,sourceGuard);}
  bool watchHostAlive()const{return host->alive&&!view->closed&&view->core;}
  bool sourceReplaced()const{
    if(!watchHostAlive())return true;
    if(watchWorld&&(watchWorld->retired||watchWorld->sourceOwner&&!watchWorld->sourceOwner->alive))return true;
    if(sourceGuard.empty()||!view->frames||sourceRaw.empty())return false;
    const auto node=view->frames->nodes.find(sourceRaw);
    // An absent inventory or rotated alias can accompany a surviving DOM.
    // A changed nonempty native loader positively identifies a replacement.
    return node!=view->frames->nodes.end()&&!sourceLoader.empty()&&!node->second.loader.empty()&&node->second.loader!=sourceLoader;
  }
  // This private observer is created in the exact source document/world, never
  // the page world. Missing/blocked event evidence is unconfirmed, not inactive.
  void watch(const std::string& expression,std::function<void(bool,const std::string&)> complete,bool retainedContext=false){
    if(retainedContext){if(!watchHostAlive()||!xb_world_cleanup_live(view,watchWorld)){complete(false,"");return;}}
    else if(!sourceLive()||sourceGuard.empty()){complete(false,"");return;}
    inFlight=true;auto self=shared_from_this();
    auto evaluate=[self,expression,complete,retainedContext](std::shared_ptr<xb_world_context> world){
      if(self->done)return;if(!(retainedContext?self->watchHostAlive()&&xb_world_cleanup_live(self->view,world):self->sourceLive()&&xb_world_live(self->view,world))){self->inFlight=false;complete(false,"");return;}
      self->watchWorld=world;
      xb_world_protocol(self->view,world->session,world->sourceOwner,L"Runtime.evaluate","{\"expression\":"+xb_quote(expression)+","+xb_world_selector(world)+",\"returnByValue\":true,\"timeout\":1000}",
        [self,world,complete,retainedContext](bool ok,const std::string& result){
          self->inFlight=false;if(self->done)return;self->nextAt=GetTickCount64()+20;
          const auto value=xb_get(result,"result");complete(ok&&(retainedContext?self->watchHostAlive()&&xb_world_cleanup_live(self->view,world):self->sourceLive()&&xb_world_live(self->view,world))&&xb_get(value,"type")=="number"&&xb_get(result,"exceptionDetails").empty(),xb_get(value,"value"));
        });
    };
    if(watchWorld){evaluate(watchWorld);return;}
    auto node=xb_frame_alias(view->frames,sourceGuard.front().first);if(!node){inFlight=false;complete(false,"");return;}
    std::weak_ptr<xb_drag_job> weak=self;
    xb_world_frame(host,view,navigation,sourceSession,node->raw,node->loader,[weak](){auto self=weak.lock();return self&&!self->done&&self->sourceLive();},evaluate);
  }
  std::string watchLookup()const{return"globalThis.__mdo_native_drag_watch_v1";}
  void armWatch(){
    watchStarted=true;watchInstalled=true;auto self=shared_from_this();
    watch("(()=>{const old="+watchLookup()+";if(old)throw Error('Previous drag observer was not released');const s={key:"+xb_quote(key)+",active:false,seen:false,target:null};"
      "const start=e=>{if(!e.isTrusted||s.seen)return;s.seen=true;s.active=true;s.target=e.target};"
      "const end=e=>{if(e.isTrusted&&s.seen&&e.target===s.target){s.active=false;s.target=null}};"
      "const hide=e=>{if(e.isTrusted)s.dispose()};s.dispose=()=>{document.removeEventListener('dragstart',start,true);document.removeEventListener('dragend',end,true);removeEventListener('pagehide',hide,true);if("+watchLookup()+"===s)delete globalThis.__mdo_native_drag_watch_v1};"
      "Object.defineProperty(globalThis,'__mdo_native_drag_watch_v1',{value:s,configurable:true});document.addEventListener('dragstart',start,true);document.addEventListener('dragend',end,true);addEventListener('pagehide',hide,true);return 1})()",
      [self](bool ok,const std::string& value){if(!ok||value!="1")self->stop("Cannot arm the original drag source observer");});
  }
  void checkWatch(){
    watchChecked=true;auto self=shared_from_this();
    watch("(()=>{const s="+watchLookup()+";return!s||s.key!=="+xb_quote(key)+"||!s.seen?0:s.active?2:1})()",
      [self](bool ok,const std::string& value){
        if(!self->sourceLive()){self->watchCleared=true;return;}
        if(!ok||(value!="1"&&value!="2")){self->watchCleanupFailed=true;self->cleanupFailed=true;self->stop("The original drag source state is unconfirmed");return;}
        self->watchActive=value=="2";
      });
  }
  void clearWatch(){
    watchCleared=true;if(!watchHostAlive()||!watchWorld)return;auto self=shared_from_this();
    if(sourceReplaced()){watchCleanupFailed=false;cleanupFailed=nativeCleanupFailed;return;}
    // Pure removal may run in the retained original context after its navigation
    // token changes (including original-response downloads). It must never create
    // a new world or send input, and only removes this job's exact keyed record.
    watch("(()=>{const s="+watchLookup()+";if(!s||s.key!=="+xb_quote(key)+")return 0;const active=s.active;s.dispose();return active?2:1})()",
      [self](bool ok,const std::string& value){
        if(!self->sourceReplaced()&&(!ok||(value!="0"&&value!="1"&&value!="2")||(self->intercepted&&value=="2"))){
          self->watchCleanupFailed=true;self->cleanupFailed=true;self->stop("Native drag source cleanup was not confirmed");
        }else{self->watchCleanupFailed=false;self->cleanupFailed=self->nativeCleanupFailed;}
      },true);
  }
  bool reapRetiredSource(){
    // Only a fully drained, inert job can leave quarantine. A source-document
    // change says nothing about an unacknowledged native mouse/interception
    // cleanup, an active provider callback, or an unfinished cleanup step.
    if(!done||inFlight||cleanup!=5||!watchCleanupFailed||nativeCleanupFailed||!sourceReplaced())return false;
    watchCleanupFailed=false;cleanupFailed=false;return true;
  }
  void receive(const std::string& json){
    if(done||intercepted)return;
    intercepted=true;
    // cancelDragging only owns Chromium's automatic drag controller. An
    // intercepted drag needs dragCancel even when its payload was rejected.
    // A late intercepted event can arrive while earlier cleanup drains.
    if(cleanup>1&&!dropped)cleanup=1;
    if(!error.empty())return;
    if(phase>=8){stop("Native drag data arrived after gesture cleanup; no drop was issued");return;}
    const auto data=xb_get(json,"data"),files=xb_get(data,"files"),items=xb_get(data,"items"),mask=xb_get(data,"dragOperationsMask");
    if(data.empty()||data.size()>65536||(!files.empty()&&files!="[]")||items.empty()||items.front()!='['){stop("File drag or oversized native drag data is not allowed through this gesture");return;}
    unsigned operations=0;try{size_t used=0;auto parsed=std::stoul(mask,&used);if(used!=mask.size()||parsed>19||(parsed&~19ul))throw 0;operations=static_cast<unsigned>(parsed);}catch(...){stop("Invalid native drag operation mask");return;}
    std::string safe;
    for(int n=0;n<17;n++){
      const auto item=webview::detail::json_parse(items,"",n);if(item.empty())break;
      const auto mime=xb_get(item,"mimeType"),value=xb_get(item,"data"),title=xb_get(item,"title"),base=xb_get(item,"baseURL");
      if(n==16||mime.empty()||mime.size()>128||value.size()>16384||title.size()>2048||base.size()>8192){stop("Native drag item exceeds its resource bound");return;}
      auto type=mime;for(char& c:type)if(c>='A'&&c<='Z')c+=32;
      if(type=="downloadurl"||(type=="text/uri-list"&&!xb_drag_uri_list(value))||(!base.empty()&&!xb_drag_url(base))){stop("Local/non-HTTP file or download data requires the transfer interface");return;}
      if(!safe.empty())safe+=",";safe+="{\"mimeType\":"+xb_quote(mime)+",\"data\":"+xb_quote(value);
      if(!title.empty())safe+=",\"title\":"+xb_quote(title);if(!base.empty())safe+=",\"baseURL\":"+xb_quote(base);safe+="}";
    }
    dragData="{\"items\":["+safe+"],\"dragOperationsMask\":"+std::to_string(operations)+"}";
  }
  void run(){
    if(done||inFlight||GetTickCount64()<nextAt)return;
    if(!live())stop("Browser control or document changed during drag; inspect its effects");
    if(GetTickCount64()>=expires)stop("Native drag deadline expired; inspect its effects");
    if(!error.empty()||phase>=8){
      // Cleanup is attempted once per step; it cannot issue another drop.
      if(cleanup==0){cleanup++;if(!error.empty()||!dropped){call(L"Input.cancelDragging","{}");return;}}
      if(cleanup==1){
        // DispatchDragEvent(drop) ends the widget under the destination. An
        // intercepted cross-widget source can still own a drag afterwards.
        // Drain at its original source point, never send another drop, and do
        // not deliver this cleanup into a replacement source document.
        if(watchInstalled&&intercepted&&!watchChecked&&sourceLive()){checkWatch();return;}
        cleanup++;
        if(intercepted&&watchInstalled&&watchActive&&sourceLive()){
          call(L"Input.dispatchDragEvent","{\"type\":\"dragCancel\",\"x\":"+std::to_string(x)+",\"y\":"+std::to_string(y)+",\"data\":{\"items\":[],\"dragOperationsMask\":0}}");return;
        }
        if(intercepted&&!watchInstalled&&!dropped){call(L"Input.dispatchDragEvent","{\"type\":\"dragCancel\",\"x\":0,\"y\":0,\"data\":{\"items\":[],\"dragOperationsMask\":0}}",eventSession);return;}
      }
      if(cleanup==2){cleanup++;if(pressed){pressed=false;call(L"Input.dispatchMouseEvent","{\"type\":\"mouseReleased\",\"button\":\"left\",\"buttons\":0,\"clickCount\":1"+point()+"}");return;}}
      if(cleanup==3){if(watchInstalled&&!watchCleared){clearWatch();return;}cleanup++;if(sourceIntercepting){sourceIntercepting=false;call(L"Input.setInterceptDrags","{\"enabled\":false}",sourceSession);return;}}
      if(cleanup==4){cleanup++;if(intercepting){intercepting=false;call(L"Input.setInterceptDrags","{\"enabled\":false}");return;}}
      reply();detach(cleanupFailed);return;
    }
    if(phase==0){
      if(!sourceGuard.empty()&&!watchStarted){armWatch();return;}
      if(!intercepting){intercepting=true;call(L"Input.setInterceptDrags","{\"enabled\":true}");return;}
      if(!sourceSession.empty()&&!sourceIntercepting){sourceIntercepting=true;call(L"Input.setInterceptDrags","{\"enabled\":true}",sourceSession);return;}
      phase=1;
    }
    if(phase==1){phase=2;dispatched=true;currentX=x;currentY=y;call(L"Input.dispatchMouseEvent","{\"type\":\"mouseMoved\""+point()+"}");return;}
    if(phase==2){phase=3;pressed=true;call(L"Input.dispatchMouseEvent","{\"type\":\"mousePressed\",\"button\":\"left\",\"buttons\":1,\"clickCount\":1"+point()+"}");return;}
    if(phase==3){
      if(!dragData.empty()){phase=5;currentX=toX;currentY=toY;}
      else if(moved<16){moved++;currentX=x+(toX-x)*moved/16;currentY=y+(toY-y)*moved/16;
        call(L"Input.dispatchMouseEvent","{\"type\":\"mouseMoved\",\"button\":\"left\",\"buttons\":1"+point()+"}");return;}
      else{phase=4;nextAt=GetTickCount64()+200;return;}
    }
    if(phase==4){if(dragData.empty()){phase=8;run();return;}phase=5;currentX=toX;currentY=toY;}
    if(phase>=5&&phase<=7){
      const char* kind=phase==5?"dragEnter":phase==6?"dragOver":"drop";
      if(phase==7)dropped=true;phase++;
      call(L"Input.dispatchDragEvent","{\"type\":"+xb_quote(kind)+point()+",\"data\":"+dragData+"}");return;
    }
  }
};
static void xb_reap_retired_drag(const std::shared_ptr<xb_view>& view){
  auto job=view->drag;if(job&&job->reapRetiredSource()&&view->drag==job)view->drag.reset();
}
static void CALLBACK xb_drag_tick(HWND window,UINT,UINT_PTR,DWORD){
  auto found=xb_drag_jobs.find(window);if(found==xb_drag_jobs.end()){KillTimer(window,xb_drag_timer);return;}
  auto job=found->second.lock();if(!job){xb_drag_jobs.erase(found);KillTimer(window,xb_drag_timer);return;}
  // Deadlines/cancellation are reported even while a renderer callback drains.
  if(!job->live())job->stop("Browser control or document changed during drag; inspect its effects");
  if(GetTickCount64()>=job->expires)job->stop("Native drag deadline expired; inspect its effects");
  job->run();
}
static void xb_close_drag(const std::shared_ptr<xb_view>& view){auto job=view->drag;if(job)job->close();}
static void xb_drag(const std::shared_ptr<xb_host>& host,const std::shared_ptr<xb_view>& view,const std::string& id,const std::string& json){
  if(view->drag||!view->input.expired()||view->capture||view->uploadBusy||xb_chooser_blocked(view)){host->reply(id,"","Another native input, upload, picker or capture is still completing");return;}
  const auto key=xb_get(json,"drag_key");
  double x=xb_capture_number(json,"x",-1),y=xb_capture_number(json,"y",-1),toX=xb_capture_number(json,"to_x",-1),toY=xb_capture_number(json,"to_y",-1),timeout=xb_capture_number(json,"timeout_ms",10000);
  if(key.empty()||key.size()>100||x<0||y<0||toX<0||toY<0||x>16384||y>16384||toX>16384||toY>16384||timeout<1||timeout>60000||xb_get(json,"navigation")!=std::to_string(view->navigation)||!xb_automation_live(host,view,view->navigation)){
    host->reply(id,"","Invalid drag target, document, control or resource budget");return;
  }
  auto job=std::make_shared<xb_drag_job>();job->host=host;job->view=view;job->id=id;job->key=key;job->navigation=view->navigation;
  if(!xb_input_read_snapshot(json,job->frameSnapshot)||!xb_input_snapshot_live(view,job->frameSnapshot)){
    host->reply(id,"{\"accepted\":false,\"dispatched\":false,\"error\":\"The original visual drag frame snapshot changed\"}");return;
  }
  if(!xb_input_read_frames(json,job->sourceGuard,"source_frame_chain")||!xb_input_read_frames(json,job->destinationGuard,"destination_frame_chain")||
     job->sourceGuard.empty()!=job->destinationGuard.empty()||!xb_input_frames_live(view,job->sourceGuard)||!xb_input_frames_live(view,job->destinationGuard)){
    host->reply(id,"{\"accepted\":false,\"dispatched\":false,\"error\":\"The original drag source or destination frame document changed\"}");return;
  }
  if(!job->sourceGuard.empty()){
    auto source=xb_frame_alias(view->frames,job->sourceGuard.front().first);if(!source){host->reply(id,"{\"accepted\":false,\"dispatched\":false,\"error\":\"The source frame is no longer attached\"}");return;}job->sourceSession=source->session;job->sourceRaw=source->raw;job->sourceLoader=source->loader;
  }
  job->width=view->bounds.right-view->bounds.left;job->height=view->bounds.bottom-view->bounds.top;
  job->x=x;job->y=y;job->toX=toX;job->toY=toY;job->expires=GetTickCount64()+static_cast<ULONGLONG>(timeout);
  HRESULT received=view->core->GetDevToolsProtocolEventReceiver(L"Input.dragIntercepted",&job->receiver);
  if(FAILED(received)||!job->receiver){host->reply(id,"","This browser does not support native drag interception");return;}
  std::weak_ptr<xb_drag_job> weak=job;
  auto* handler=xb_cb<ICoreWebView2DevToolsProtocolEventReceivedEventHandler,ICoreWebView2*,ICoreWebView2DevToolsProtocolEventReceivedEventArgs*>(
    IID_ICoreWebView2DevToolsProtocolEventReceivedEventHandler,[weak](ICoreWebView2*,ICoreWebView2DevToolsProtocolEventReceivedEventArgs* args){
      if(auto active=weak.lock()){
        xs_wv_text session;xs_wv_com<ICoreWebView2DevToolsProtocolEventReceivedEventArgs2> routed;
        if(SUCCEEDED(args->QueryInterface(IID_ICoreWebView2DevToolsProtocolEventReceivedEventArgs2,reinterpret_cast<void**>(routed.out())))){
          if(FAILED(routed->get_SessionId(&session.p))){active->stop("Cannot identify the native drag source");return S_OK;}
        }else if(!active->sourceSession.empty()){active->stop("Native drag source session is unavailable");return S_OK;}
        const auto source=webview::detail::narrow_string(session.p?session.p:L"");
        // Chromium may report a child-origin drag through the main view's
        // browser-level input handler. Only that root or the selected frame
        // session can supply this gesture's payload, never another OOPIF.
        if(!source.empty()&&source!=active->sourceSession)return S_OK;
        if(!active->intercepted)active->eventSession=source;
        xs_wv_text text;if(FAILED(args->get_ParameterObjectAsJson(&text.p))||!text.p){active->stop("Cannot inspect native drag data");return S_OK;}
        size_t size=0;while(size<=65536&&text.p[size])size++;if(size>65536){active->stop("Native drag data exceeds 64 KiB");return S_OK;}
        active->receive(webview::detail::narrow_string(text.p));
      }return S_OK;
    });
  job->subscribed=SUCCEEDED(job->receiver->add_DevToolsProtocolEventReceived(handler,&job->token));handler->Release();
  if(!job->subscribed){job->receiver->Release();job->receiver=nullptr;host->reply(id,"","Cannot subscribe to native drag data");return;}
  view->drag=job;xb_drag_jobs[view->window]=job;
  if(!SetTimer(view->window,xb_drag_timer,20,xb_drag_tick)){job->error="Cannot retain native drag deadline";job->reply();job->detach();return;}
  job->run();
}
