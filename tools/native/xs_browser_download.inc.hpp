/* Browser responses (including POST/blob) stay in the browser's own download
 * operation. Only opaque scoped identities, bounded metadata/ranges leave it. */
/* A policy acknowledgement must precede any new URL/adoption. The timer and
 * generation fence also cover missing, duplicate and late protocol callbacks. */
static constexpr UINT_PTR xb_download_policy_timer=0xBADD8;
static const std::string xb_download_policy_empty,xb_download_policy_changed="Browser control changed before download interception was ready",xb_download_policy_expired="Native download interception was not acknowledged before its deadline or control changed",xb_download_policy_stopped="The browser host stopped before download interception was ready";
using xb_download_arm_done=std::function<void(bool,const std::string&)>;
struct xb_download_arm_job;
static std::map<HWND,std::map<xb_view*,std::shared_ptr<xb_download_arm_job>>> xb_download_policies;
struct xb_download_arm_job:std::enable_shared_from_this<xb_download_arm_job>{
  std::weak_ptr<xb_host> owner;std::weak_ptr<xb_view> target;HWND window{};
  xb_view* targetKey=nullptr;std::string browser;ULONGLONG expires=0;bool finished=false;xb_download_arm_done done;
  void finish(bool ok,const std::string& cause){
    if(finished)return;finished=true;auto host=owner.lock();auto view=target.lock();
    auto state=view?view->downloads:nullptr;auto callback=std::move(done);
    const bool live=host&&host->alive&&view&&!view->closed&&!view->requestedClose&&!view->failed&&!view->manual&&state&&
      host->views.count(browser)&&host->views.at(browser)==view;
    ok=ok&&live;
    const std::string& error=ok?xb_download_policy_empty:cause.empty()?xb_download_policy_changed:cause;
    if(state){state->arming=false;state->arm.reset();if(ok)state->armed=state->protocolReady=true;else state->armed=false;}
    auto group=xb_download_policies.find(window);if(group!=xb_download_policies.end()){
      auto at=group->second.find(targetKey);if(at!=group->second.end()&&at->second.get()==this)group->second.erase(at);
      if(group->second.empty()){KillTimer(window,xb_download_policy_timer);xb_download_policies.erase(group);}
    }
    if(state&&!ok)try{state->error=error;}catch(...){state->error.clear();}
    // Cleanup is complete before any allocation or publication can fail.
    if(callback)try{callback(ok,error);}catch(...){}
  }
};
static void CALLBACK xb_download_policy_tick(HWND window,UINT,UINT_PTR,DWORD){
  auto group=xb_download_policies.find(window);if(group==xb_download_policies.end()){KillTimer(window,xb_download_policy_timer);return;}
  std::shared_ptr<xb_download_arm_job> expired[8];size_t count=0;
  for(const auto& item:group->second){auto host=item.second->owner.lock();auto view=item.second->target.lock();
    if((!host||!host->alive||!view||view->closed||view->requestedClose||view->failed||view->manual||GetTickCount64()>=item.second->expires)&&count<8)expired[count++]=item.second;
  }
  for(size_t i=0;i<count;i++)expired[i]->finish(false,xb_download_policy_expired);
}
static void xb_download_abort_arm(const std::shared_ptr<xb_view>& view,const char* reason){if(view->downloads)if(auto job=view->downloads->arm.lock()){try{job->finish(false,reason);}catch(...){job->finish(false,xb_download_policy_changed);}}}
template<class Done> static void xb_download_arm(const std::shared_ptr<xb_host>& host,const std::shared_ptr<xb_view>& view,Done&& done){
  auto state=view->downloads;
  if(!host->alive||view->closed||view->requestedClose||view->failed||view->manual||!state||!state->supported||!state->beginSubscribed||!state->progressSubscribed){done(false,"Native download interception is unavailable or controlled by the user");return;}
  const std::string* browser=nullptr;for(const auto& item:host->views)if(item.second==view){browser=&item.first;break;}
  if(!browser){done(false,"The native browser identity changed before policy admission");return;}
  if(state->arming){done(false,"Native download interception is already being acknowledged");return;}
  if(state->protocolReady){state->armed=true;done(true,{});return;}
  auto group=xb_download_policies.find(host->window);if(group!=xb_download_policies.end()&&group->second.size()>=8){done(false,"Native download policy limit reached");return;}
  const bool needsTimer=group==xb_download_policies.end();std::shared_ptr<xb_download_arm_job> job;
  try{
    job=std::make_shared<xb_download_arm_job>();job->owner=host;job->target=view;job->targetKey=view.get();job->window=host->window;job->expires=GetTickCount64()+5000;job->done=std::forward<Done>(done);job->browser=*browser;
    xb_download_policies[host->window][view.get()]=job;state->arming=true;state->arm=job;
    if(needsTimer&&!SetTimer(host->window,xb_download_policy_timer,50,xb_download_policy_tick)){job->finish(false,"Cannot retain the native download policy deadline");return;}
    xb_cdp(view,L"Page.enable","{}",[job](bool ok,const std::string& result){
      if(job->finished)return;const auto error=xb_get(result,"error");
      job->finish(ok&&error.empty(),ok&&error.empty()?std::string{}:"Cannot enable scoped page download identity events: "+xb_get(error,"message").substr(0,256));
    });
  }catch(...){
    if(job&&job->finished)return;
    const bool stored=job&&static_cast<bool>(job->done);
    if(job)job->finish(false,"Cannot allocate a bounded native download policy request");
    if(!stored)done(false,"Cannot allocate a bounded native download policy request");
  }
}
static void xb_download_policy_shutdown(const std::shared_ptr<xb_host>& host){
  auto group=xb_download_policies.find(host->window);if(group==xb_download_policies.end())return;
  std::shared_ptr<xb_download_arm_job> pending[8];size_t count=0;for(const auto& item:group->second)if(count<8)pending[count++]=item.second;
  for(size_t i=0;i<count;i++)pending[i]->finish(false,xb_download_policy_stopped);
  KillTimer(host->window,xb_download_policy_timer);xb_download_policies.erase(host->window);
}

static constexpr UINT_PTR xb_download_timer=0xBADD7;
static constexpr uint64_t xb_download_file_limit=64ull*1024*1024,xb_download_total_limit=128ull*1024*1024;
static bool xb_download_navigation_abort(const std::shared_ptr<xb_view>& view,UINT64 navigation,COREWEBVIEW2_WEB_ERROR_STATUS error){
  // Only this navigation's own native response can explain an aborted load.
  // Retained older downloads never mask DNS/TLS/reset or another navigation.
  return error==COREWEBVIEW2_WEB_ERROR_STATUS_CONNECTION_ABORTED&&view->downloads&&view->downloads->navigation==navigation&&view->downloads->navigationDownload;
}
static std::wstring xb_download_text(LPCWSTR value,size_t maximum){if(!value)return {};size_t length=0;while(length<=maximum&&value[length])length++;return length<=maximum?std::wstring(value,length):std::wstring{};}
static bool xb_download_guid(const std::wstring& name){
  if(name.size()!=38||name.rfind(L"d-",0)!=0)return false;
  for(size_t i=2;i<38;i++){auto c=name[i];bool dash=i==10||i==15||i==20||i==25;if(dash?c!=L'-':!((c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f')||(c>=L'A'&&c<=L'F')))return false;}return true;
}
static bool xb_download_layout(const std::wstring& directory,std::vector<std::wstring>& files){
  if(!xb_upload_plain_directory(directory))return false;
  WIN32_FIND_DATAW entry{};xb_upload_finder find;find.value=FindFirstFileW((directory+L"\\*").c_str(),&entry);unsigned count=0;
  if(find.value==INVALID_HANDLE_VALUE)return false;
  do{std::wstring name=entry.cFileName;if(name==L"."||name==L".."||name==L".lease")continue;
    if(++count>2||(name!=L"payload"&&name!=L"payload.crdownload")||(entry.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))return false;
    files.push_back(directory+L"\\"+name);
  }while(FindNextFileW(find.value,&entry));return GetLastError()==ERROR_NO_MORE_FILES;
}
static const char xb_download_inventory[]="mdo.browser-download.v1\n";
static bool xb_download_collect_stale(const std::wstring& root){
  WIN32_FIND_DATAW entry{};xb_upload_finder find;find.value=FindFirstFileW((root+L"\\*").c_str(),&entry);unsigned count=0;bool ok=true;
  if(find.value==INVALID_HANDLE_VALUE)return false;
  do{std::wstring name=entry.cFileName;if(name==L"."||name==L".."||name==L".guard")continue;
    if(++count>64||!xb_download_guid(name)||!(entry.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||(entry.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)){ok=false;continue;}
    const auto directory=root+L"\\"+name;xb_upload_handle lease;lease.value=CreateFileW((directory+L"\\.lease").c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(lease.value==INVALID_HANDLE_VALUE){auto error=GetLastError();if(error==ERROR_SHARING_VIOLATION)continue;if(error==ERROR_FILE_NOT_FOUND&&xb_upload_remove_directory(directory))continue;ok=false;continue;}
    BY_HANDLE_FILE_INFORMATION info{};char data[sizeof(xb_download_inventory)]{};DWORD bytes=0;
    const bool valid=GetFileType(lease.value)==FILE_TYPE_DISK&&GetFileInformationByHandle(lease.value,&info)&&info.nNumberOfLinks==1&&
      !(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))&&!info.nFileSizeHigh&&info.nFileSizeLow<=sizeof(xb_download_inventory)-1&&
      ReadFile(lease.value,data,sizeof(data),&bytes,nullptr)&&bytes==info.nFileSizeLow&&!memcmp(data,xb_download_inventory,bytes);
    std::vector<std::wstring> paths;if(!valid||!xb_download_layout(directory,paths)||(bytes!=sizeof(xb_download_inventory)-1&&!paths.empty())){ok=false;continue;}
    bool removed=true;for(const auto& path:paths)if(!xb_upload_remove_file(path))removed=false;
    CloseHandle(lease.value);lease.value=INVALID_HANDLE_VALUE;
    if(!removed||!xb_upload_remove_file(directory+L"\\.lease")||!xb_upload_remove_directory(directory))ok=false;
  }while(FindNextFileW(find.value,&entry));return GetLastError()==ERROR_NO_MORE_FILES&&ok;
}
struct xb_download_job{
  std::weak_ptr<xb_view> view;ICoreWebView2DownloadStartingEventArgs* args=nullptr;ICoreWebView2Deferral* deferral=nullptr;ICoreWebView2DownloadOperation* operation=nullptr;
  EventRegistrationToken bytesToken{},stateToken{};bool bytesSubscribed=false,stateSubscribed=false;
  std::string id,name,mime,state="identifying",error,sha,uri,guid,failureSource;std::wstring directory,path;UINT64 navigation=0;
  uint64_t received=0,size=0,limit=32ull*1024*1024;int64_t expected=-1;int reason=0;ULONGLONG expires=0,verifyDeadline=0,identifyDeadline=0,protocolCancelAt=0;
  HANDLE lease=INVALID_HANDLE_VALUE,file=INVALID_HANDLE_VALUE;bool directoryOwned=false,leaseOwned=false,closing=false,updating=false,cancelSent=false,cancelConfirmed=false,published=false,cleanupBlocked=false;
  bool finishDeferral(){bool ok=true;if(deferral){auto* held=deferral;deferral=nullptr;ok=SUCCEEDED(held->Complete());held->Release();}if(args){args->Release();args=nullptr;}return ok;}
  void detach(){if(operation){if(bytesSubscribed)operation->remove_BytesReceivedChanged(bytesToken);if(stateSubscribed)operation->remove_StateChanged(stateToken);bytesSubscribed=stateSubscribed=false;operation->Release();operation=nullptr;}}
  bool terminal()const{return state=="completed"||state=="cancelled"||state=="failed";}
  void cancel(const std::string& code){
    if(terminal())return;if(error.empty())error=code;
    if(state=="identifying"||state=="pending"||state=="verifying"){state=code=="cancelled"?"cancelled":"failed";if(args)args->put_Cancel(TRUE);finishDeferral();detach();return;}
    state="cancelling";if(operation&&!cancelSent){cancelSent=true;cancelConfirmed=SUCCEEDED(operation->Cancel());if(!cancelConfirmed)error="download_cancel_unconfirmed";}
  }
  void update(){
    if(updating||terminal()||state=="identifying"||state=="pending"||state=="verifying"||!operation)return;updating=true;struct reset{bool& flag;~reset(){flag=false;}} resetter{updating};
    INT64 count=0,total=-1;COREWEBVIEW2_DOWNLOAD_STATE nativeState{};COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON nativeReason{};
    if(protocolCancelAt){
      if(SUCCEEDED(operation->get_BytesReceived(&count))&&count>=0)received=static_cast<uint64_t>(count);
      if(SUCCEEDED(operation->get_InterruptReason(&nativeReason))&&nativeReason!=COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_NONE)reason=static_cast<int>(nativeReason);
      failureSource="page_download_progress";cancel("download_interrupted");
      // Page.downloadProgress observes the actual DownloadItem terminal state.
      // WebView2 can keep an obsolete COM wrapper 'in progress' during resume.
      if(GetTickCount64()>=protocolCancelAt+200){state=error=="cancelled"?"cancelled":"failed";detach();}return;
    }
    if(FAILED(operation->get_BytesReceived(&count))||FAILED(operation->get_TotalBytesToReceive(&total))||FAILED(operation->get_State(&nativeState))||count<0||total< -1){cancel("download_state_unavailable");return;}
    // This WebView2 also reports zero for unknown HTTP response lengths.
    // Zero and unknown are indistinguishable here; actual size/hash remain exact.
    received=static_cast<uint64_t>(count);expected=total>0?total:-1;
    if(nativeState==COREWEBVIEW2_DOWNLOAD_STATE_INTERRUPTED&&!reason&&SUCCEEDED(operation->get_InterruptReason(&nativeReason)))reason=static_cast<int>(nativeReason);
    if(state!="cancelling"&&(received>limit||expected>=0&&static_cast<uint64_t>(expected)>limit))cancel("download_limit");
    if(state!="cancelling"&&GetTickCount64()>=expires)cancel("download_timed_out");
    if(nativeState==COREWEBVIEW2_DOWNLOAD_STATE_INTERRUPTED&&state!="cancelling"){
      if(SUCCEEDED(operation->get_InterruptReason(&nativeReason)))reason=static_cast<int>(nativeReason);failureSource="native_state";cancel("download_interrupted");
    }
    if(state=="cancelling"){
      // Some native interruptions may auto-resume; only a confirmed Cancel
      // prevents that. Never delete an operation still owning a writable file.
      if(cancelConfirmed&&nativeState!=COREWEBVIEW2_DOWNLOAD_STATE_IN_PROGRESS){state=error=="cancelled"?"cancelled":"failed";detach();}return;
    }
    if(nativeState==COREWEBVIEW2_DOWNLOAD_STATE_COMPLETED){state="verifying";verifyDeadline=GetTickCount64()+5000;detach();}
  }
  void verify(){
    if(state!="verifying")return;
    xb_upload_handle opened;opened.value=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
    if(opened.value==INVALID_HANDLE_VALUE){if(GetTickCount64()>=verifyDeadline){state="failed";error="download_file_unavailable";}return;}
    BY_HANDLE_FILE_INFORMATION before{},after{};bool ok=xb_upload_plain_directory(directory)&&GetFileType(opened.value)==FILE_TYPE_DISK&&GetFileInformationByHandle(opened.value,&before)&&
      !(before.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))&&before.nNumberOfLinks==1;
    size=static_cast<uint64_t>(before.nFileSizeHigh)<<32|before.nFileSizeLow;ok=ok&&size<=limit&&size==received&&(expected<0||size==static_cast<uint64_t>(expected));
    xb_upload_provider provider;xb_upload_hasher hasher;ok=ok&&CryptAcquireContextW(&provider.value,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT)&&CryptCreateHash(provider.value,CALG_SHA_256,0,0,&hasher.value);
    xb_upload_bytes buffer(65536);DWORD bytes=0;uint64_t read=0;
    while(ok){if(GetTickCount64()>=verifyDeadline||!ReadFile(opened.value,buffer.data(),static_cast<DWORD>(buffer.size()),&bytes,nullptr)){ok=false;break;}if(!bytes)break;
      read+=bytes;if(read>size||!CryptHashData(hasher.value,buffer.data(),bytes,0)){ok=false;break;}}
    unsigned char digest[32]{};DWORD length=32;
    ok=ok&&read==size&&GetFileInformationByHandle(opened.value,&after)&&!memcmp(&before.ftLastWriteTime,&after.ftLastWriteTime,sizeof(FILETIME))&&before.nFileSizeHigh==after.nFileSizeHigh&&before.nFileSizeLow==after.nFileSizeLow&&CryptGetHashParam(hasher.value,HP_HASHVAL,digest,&length,0)&&length==32;
    if(!ok){state="failed";error="download_verification_failed";return;}
    static const char digits[]="0123456789abcdef";for(auto c:digest){sha+=digits[c>>4];sha+=digits[c&15];}
    file=opened.value;opened.value=INVALID_HANDLE_VALUE;state="completed";
  }
  bool cleanup(){
    if(!directoryOwned)return true;if(operation||deferral||!terminal()||cleanupBlocked)return false;
    xb_upload_guard guard(directory.substr(0,directory.find_last_of(L'\\')));if(guard.handle==INVALID_HANDLE_VALUE)return false;
    if(GetFileAttributesW(directory.c_str())==INVALID_FILE_ATTRIBUTES&&(GetLastError()==ERROR_FILE_NOT_FOUND||GetLastError()==ERROR_PATH_NOT_FOUND))return true;
    std::vector<std::wstring> paths;if(!xb_download_layout(directory,paths))return false;
    if(file!=INVALID_HANDLE_VALUE){CloseHandle(file);file=INVALID_HANDLE_VALUE;}
    bool ok=true;for(const auto& item:paths)if(!xb_upload_remove_file(item))ok=false;if(!ok)return false;
    if(lease!=INVALID_HANDLE_VALUE){CloseHandle(lease);lease=INVALID_HANDLE_VALUE;}
    return (!leaseOwned||xb_upload_remove_file(directory+L"\\.lease"))&&xb_upload_remove_directory(directory);
  }
  ~xb_download_job(){try{cancel("download_closed");finishDeferral();detach();if(!terminal())state="failed";cleanup();}catch(...){}if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);if(lease!=INVALID_HANDLE_VALUE)CloseHandle(lease);}
};
struct xb_download_store{
  std::weak_ptr<xb_host> host;std::wstring root;bool initialized=false;std::map<std::string,std::shared_ptr<xb_download_job>> jobs;
  ULONGLONG reapUntil=0,reapNext=0;unsigned reapDelay=100;
  void retired(){const auto now=GetTickCount64();reapUntil=now+5000;reapNext=now+100;reapDelay=100;}
  uint64_t reserved()const{uint64_t total=0;for(const auto& item:jobs)if(item.second->directoryOwned)total+=item.second->limit;return total;}
  bool initialize(const std::shared_ptr<xb_host>& owner){if(initialized)return true;host=owner;root=owner->profile+L"\\mdo-browser-use-downloads";
    if(!xb_upload_private_root(root))return false;xb_upload_guard guard(root);if(guard.handle==INVALID_HANDLE_VALUE||!xb_download_collect_stale(root))return false;initialized=true;return true;}
  void retire(const std::shared_ptr<xb_download_job>& job){auto view=job->view.lock();if(!view||!view->downloads||job->guid.empty())return;auto at=view->downloads->protocol.find(job->guid);
    if(at!=view->downloads->protocol.end())view->downloads->retire(at->second);}
  void collect(){
    auto owner=host.lock();bool watching=false;for(auto at=jobs.begin();at!=jobs.end();){auto job=at->second;auto view=job->view.lock();
      if(!owner||!owner->alive||!view||view->closed){job->closing=true;job->cancel("download_closed");}
      if(view&&!view->closed&&view->downloads){auto state=view->downloads;
        if(job->state=="identifying"){
          std::string candidate;bool ambiguous=false;for(const auto& item:state->protocol)if(!item.second.bound&&item.second.begins&&item.second.url==job->uri){if(!candidate.empty())ambiguous=true;else candidate=item.first;}
          if(!candidate.empty()&&!ambiguous){state->protocol[candidate].bound=true;job->guid=candidate;job->state="pending";job->published=true;job->expires=GetTickCount64()+state->offerTimeout;}
          else if(ambiguous||GetTickCount64()>=job->identifyDeadline){job->cancel(ambiguous?"download_identity_ambiguous":"download_identity_unavailable");job->closing=true;state->identityRejected++;state->error="A native download without a unique page identity was rejected";}
        }
        auto protocol=state->protocol.find(job->guid);if(protocol!=state->protocol.end()&&protocol->second.state=="canceled"){
          if(!job->protocolCancelAt)job->protocolCancelAt=GetTickCount64();
          if(job->state=="pending"){COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON reason{};if(job->operation&&SUCCEEDED(job->operation->get_InterruptReason(&reason)))job->reason=static_cast<int>(reason);job->failureSource="page_download_progress";job->cancel("download_interrupted");}
        }
      }
      if(job->state=="pending"&&GetTickCount64()>=job->expires)job->cancel("download_offer_expired");
      job->cleanupBlocked=false;for(const auto& other:jobs)if(other.second!=job&&other.second->view.lock()==view&&other.second->state=="identifying")job->cleanupBlocked=true;
      job->update();job->verify();if(job->closing&&job->cleanup()){if(job->directoryOwned)retired();retire(job);at=jobs.erase(at);}else{watching|=!job->terminal()||job->closing;++at;}
    }
    // Cancellation/controller closure can outlive our first filesystem cleanup.
    // Recheck the attested cache briefly after its last owner retires. Active
    // leases and unknown/reparse layouts remain protected by the same collector.
    const auto now=GetTickCount64();
    if(initialized&&owner&&owner->alive&&now<reapUntil){watching=true;if(now>=reapNext){
      xb_upload_guard guard(root);if(guard.handle!=INVALID_HANDLE_VALUE)xb_download_collect_stale(root);
      reapNext=now+reapDelay;reapDelay=std::min(1000u,reapDelay*2);
    }}
    if(!watching&&owner)KillTimer(owner->window,xb_download_timer);
  }
};
static std::map<HWND,std::weak_ptr<xb_download_store>> xb_download_stores;
static void CALLBACK xb_download_tick(HWND window,UINT,UINT_PTR,DWORD){auto at=xb_download_stores.find(window);if(at==xb_download_stores.end()){KillTimer(window,xb_download_timer);return;}if(auto store=at->second.lock())store->collect();else{KillTimer(window,xb_download_timer);xb_download_stores.erase(at);}}
static bool xb_download_watch(const std::shared_ptr<xb_host>& host){xb_download_stores[host->window]=host->downloads;return SetTimer(host->window,xb_download_timer,100,xb_download_tick)!=0;}
static bool xb_download_holds_page_close(const std::shared_ptr<xb_host>& host,const std::shared_ptr<xb_view>& view){
  // Chromium can close a download-only popup while its original response still
  // owns bytes/identity. Keep that broker alive until explicit release/close.
  // Include unpublished candidates; callback ordering must not lose a response.
  if(host->downloads)for(const auto& item:host->downloads->jobs)if(item.second->view.lock()==view)return true;
  return view->downloads&&view->downloads->armed&&view->requestedClose&&GetTickCount64()-view->closeRequestedAt<1100;
}
static std::string xb_download_status(const std::shared_ptr<xb_download_job>& job){
  return "{\"id\":"+xb_quote(job->id)+",\"name\":"+xb_quote(job->name)+",\"mime\":"+xb_quote(job->mime)+",\"state\":"+xb_quote(job->state)+",\"received\":"+std::to_string(job->received)+",\"expected_size\":"+(job->expected<0?"null":std::to_string(job->expected))+",\"max_bytes\":"+std::to_string(job->limit)+",\"interrupt_reason\":"+std::to_string(job->reason)+",\"failure_source\":"+xb_quote(job->failureSource)+",\"error\":"+xb_quote(job->error)+(job->state=="completed"?",\"size\":"+std::to_string(job->size)+",\"sha256\":"+xb_quote(job->sha):"")+"}";
}
static void xb_download_close_view(const std::shared_ptr<xb_host>& host,const std::shared_ptr<xb_view>& view){xb_download_abort_arm(view,"The browser closed before policy acknowledgement");if(view->downloads)view->downloads->unsubscribe();if(!host->downloads)return;
  for(auto& item:host->downloads->jobs)if(item.second->view.lock()==view){item.second->closing=true;item.second->cancel("download_closed");}if(!host->downloads->jobs.empty())xb_download_watch(host);host->downloads->collect();}
static void xb_download_drain_closed_view(const std::shared_ptr<xb_host>& host,const std::shared_ptr<xb_view>& view){
  if(!view->closed||!host->downloads)return;
  // After Controller.Close no new download admission from this view is allowed.
  // A stale COM wrapper may never send StateChanged, so drain confirmed native
  // cancellations here instead of retaining that wrapper/lease indefinitely.
  for(auto& item:host->downloads->jobs)if(item.second->view.lock()==view){auto job=item.second;job->cleanupBlocked=false;
    if(!job->operation||job->cancelConfirmed){job->finishDeferral();job->detach();if(!job->terminal())job->state="failed";}
  }host->downloads->collect();
}
static void xb_download_takeover(const std::shared_ptr<xb_host>& host,const std::shared_ptr<xb_view>& view){xb_download_abort_arm(view,"The user took over before policy acknowledgement");if(view->downloads)view->downloads->armed=false;if(!host->downloads)return;
  for(auto& item:host->downloads->jobs)if(item.second->view.lock()==view&&!item.second->terminal())item.second->cancel("download_taken_over");host->downloads->collect();}
static void xb_download_shutdown(const std::shared_ptr<xb_host>& host){xb_download_policy_shutdown(host);KillTimer(host->window,xb_download_timer);xb_download_stores.erase(host->window);if(host->downloads){host->downloads->collect();host->downloads.reset();}}
static void xb_install_downloads(const std::shared_ptr<xb_host>& host,const std::shared_ptr<xb_view>& view){auto state=std::make_shared<xb_download_state>();view->downloads=state;
  if(FAILED(view->core->QueryInterface(IID_ICoreWebView2_4,reinterpret_cast<void**>(&state->source)))||!state->source)return;
  std::weak_ptr<xb_view> weak=view;std::weak_ptr<xb_host> owner=host;
  auto subscribe=[&](const wchar_t* event,bool begin,ICoreWebView2DevToolsProtocolEventReceiver** receiver,EventRegistrationToken* token){
    if(FAILED(view->core->GetDevToolsProtocolEventReceiver(event,receiver))||!*receiver)return false;
    auto* callback=xb_cb<ICoreWebView2DevToolsProtocolEventReceivedEventHandler,ICoreWebView2*,ICoreWebView2DevToolsProtocolEventReceivedEventArgs*>(IID_ICoreWebView2DevToolsProtocolEventReceivedEventHandler,
      [weak,owner,begin](ICoreWebView2*,ICoreWebView2DevToolsProtocolEventReceivedEventArgs* args){auto view=weak.lock();auto host=owner.lock();if(!view||!host||!host->alive||view->closed||!view->downloads)return S_OK;auto state=view->downloads;
        if(!state->armed){if(begin)return S_OK;bool owned=false;if(host->downloads)for(const auto& item:host->downloads->jobs)owned|=item.second->view.lock()==view;if(!owned)return S_OK;}
        xs_wv_text raw;if(FAILED(args->get_ParameterObjectAsJson(&raw.p)))return S_OK;auto bounded=xb_download_text(raw.p,16384);if(bounded.empty())return S_OK;const auto json=webview::detail::narrow_string(bounded),guid=xb_get(json,"guid");
        if(guid.empty()||guid.size()>100)return S_OK;auto at=state->protocol.find(guid);if(!state->armed&&(at==state->protocol.end()||!at->second.bound||at->second.retired))return S_OK;if(at==state->protocol.end()&&state->protocol.size()>=4096){state->error="Download protocol event budget exceeded; end this browser before more downloads";return S_OK;}
        auto& item=state->protocol[guid];item.updated=GetTickCount64();if(begin){item.begins++;state->protocolBegins++;if(!item.retired){const auto url=xb_get(json,"url"),frame=xb_get(json,"frameId"),name=xb_get(json,"suggestedFilename");if(url.size()>8192||frame.size()>128||name.size()>1024)return S_OK;
          if(item.url.empty()){
            if(state->activeProtocol>=64)for(auto& old:state->protocol)if(!old.second.bound&&old.second.state=="canceled"&&GetTickCount64()-old.second.updated>1000)state->retire(old.second);
            if(state->activeProtocol>=64){state->retire(item);state->error="Download live identity budget exceeded";return S_OK;}state->activeProtocol++;
          }item.url=url;item.frame=frame;item.name=name;}}else item.state=xb_get(json,"state");return S_OK;
      });
    const bool added=SUCCEEDED((*receiver)->add_DevToolsProtocolEventReceived(callback,token));callback->Release();return added;
  };
  state->beginSubscribed=subscribe(L"Page.downloadWillBegin",true,&state->beginReceiver,&state->beginToken);
  state->progressSubscribed=subscribe(L"Page.downloadProgress",false,&state->progressReceiver,&state->progressToken);
  auto* handler=xb_cb<ICoreWebView2DownloadStartingEventHandler,ICoreWebView2*,ICoreWebView2DownloadStartingEventArgs*>(IID_ICoreWebView2DownloadStartingEventHandler,
    [weak,owner](ICoreWebView2*,ICoreWebView2DownloadStartingEventArgs* args){auto view=weak.lock();auto host=owner.lock();if(!view||!host||!host->alive||view->closed||!host->trusted()){args->put_Cancel(TRUE);args->put_Handled(TRUE);return S_OK;}
      auto state=view->downloads;if(!state||!state->armed||view->manual)return S_OK;
      state->sequence++;const auto cancelled=args->put_Cancel(TRUE),handled=args->put_Handled(TRUE);if(FAILED(cancelled)||FAILED(handled)){state->error="Cannot suppress the native download";return E_FAIL;}
      if(!host->downloads){host->downloads=std::make_shared<xb_download_store>();host->downloads->host=host;}
      auto store=host->downloads;store->collect();auto job=std::make_shared<xb_download_job>();job->view=view;
      if(FAILED(args->get_DownloadOperation(&job->operation))||!job->operation){state->error="Cannot inspect the native download identity";return S_OK;}
      xs_wv_text path,mime,uri;args->get_ResultFilePath(&path.p);job->operation->get_Uri(&uri.p);job->uri=webview::detail::narrow_string(xb_download_text(uri.p,8192));
      if(job->uri.empty()){state->error="Cannot identify this native response";return S_OK;}
      const auto proposed=xb_download_text(path.p,32768);unsigned owned=0;for(const auto& item:store->jobs)owned+=item.second->view.lock()==view;
      if(owned>=16||store->jobs.size()>=32){state->error="Download receipt limit reached; release completed downloads";return S_OK;}
      job->navigation=view->navigation;
      if(state->navigation==view->navigation&&state->navigationUrl==job->uri){
        state->navigationDownload=true;if(view->errorCode==COREWEBVIEW2_WEB_ERROR_STATUS_CONNECTION_ABORTED)view->error.clear();
      }
      job->identifyDeadline=GetTickCount64()+1000;
      GUID guid{};wchar_t identifier[40]{};
      if(FAILED(CoCreateGuid(&guid))||!StringFromGUID2(guid,identifier,40)||FAILED(args->GetDeferral(&job->deferral))){state->error="Cannot retain a native download";return S_OK;}
      args->AddRef();job->args=args;job->id="d-"+webview::detail::narrow_string(std::wstring(identifier+1,36));
      job->operation->get_MimeType(&mime.p);auto suggested=proposed;auto split=suggested.find_last_of(L"\\/");if(split!=std::wstring::npos)suggested=suggested.substr(split+1);job->name=webview::detail::narrow_string(suggested);
      if(!xb_upload_name(job->name))job->name="download.bin";job->mime=webview::detail::narrow_string(xb_download_text(mime.p,128));if(job->mime.size()>128)job->mime.clear();
      INT64 total=-1;if(SUCCEEDED(job->operation->get_TotalBytesToReceive(&total))&&total>0)job->expected=total;
      store->jobs[job->id]=job;if(!xb_download_watch(host)){job->cancel("download_timer_unavailable");state->error="Cannot retain a native download deadline";return S_OK;}
      state->error.clear();if(auto input=view->input.lock())input->boundary("download");if(view->drag)view->drag->stop("A download interrupted the drag; inspect its receipt");return S_OK;
    });
  state->subscribed=SUCCEEDED(state->source->add_DownloadStarting(handler,&state->token));handler->Release();state->supported=state->subscribed;
}
static void xb_download_request(const std::shared_ptr<xb_host>& host,const std::shared_ptr<xb_view>& view,const std::string& id,const std::string& json){
  auto fail=[&](const char* text){host->reply(id,"",text);};auto state=view->downloads;const auto action=xb_get(json,"action");
  if(!state||!state->supported){fail("This browser cannot intercept native downloads");return;}
  if(host->downloads)host->downloads->collect();
  if(action=="list"){
    std::string items;if(host->downloads)for(const auto& item:host->downloads->jobs)if(item.second->view.lock()==view&&item.second->published){if(!items.empty())items+=",";items+=xb_download_status(item.second);}
    unsigned repeated=0;for(const auto& item:state->protocol)if(item.second.begins>1)repeated++;
    host->reply(id,"{\"ok\":true,\"armed\":"+std::string(state->armed?"true":"false")+",\"sequence\":"+std::to_string(state->sequence)+",\"protocol_ready\":"+(state->protocolReady?"true":"false")+",\"protocol_begin_count\":"+std::to_string(state->protocolBegins)+",\"protocol_ids\":"+std::to_string(state->protocol.size())+",\"protocol_repeated_ids\":"+std::to_string(repeated)+",\"identity_rejected\":"+std::to_string(state->identityRejected)+",\"downloads\":["+items+"],\"error\":"+xb_quote(state->error)+"}");return;
  }
    if(view->closed||view->failed||(view->manual&&xb_get(json,"agent")=="true"&&action!="get")){fail("The browser is unavailable or controlled by the user");return;}
  if(action=="arm"){uint64_t timeout=120000;if(view->manual||(!xb_get(json,"timeout_ms").empty()&&!xb_upload_uint(xb_get(json,"timeout_ms"),timeout))||timeout<1000||timeout>120000){fail("Only Agent control may arm downloads with a 1000..120000 ms offer lease");return;}state->offerTimeout=static_cast<unsigned>(timeout);
    xb_download_arm(host,view,[host,id](bool ok,const std::string& error){host->reply(id,ok?"{\"ok\":true,\"armed\":true}":"",error);});return;}
  auto store=host->downloads;auto at=store?store->jobs.find(xb_get(json,"download")):std::map<std::string,std::shared_ptr<xb_download_job>>::iterator{};
  if(!store||at==store->jobs.end()||at->second->view.lock()!=view){fail("The download expired or belongs to another browser");return;}auto job=at->second;
  if(action=="get"){host->reply(id,xb_download_status(job));return;}
  if(action=="cancel"){job->cancel("cancelled");job->update();host->reply(id,xb_download_status(job));return;}
  if(action=="release"){
    if(!job->terminal()){fail("Cancel and wait for the native operation before releasing a download");return;}
    job->closing=true;if(!job->cleanup()){fail("Download cache cleanup is pending or its layout changed; files were preserved");return;}if(job->directoryOwned){store->retired();xb_download_watch(host);}store->retire(job);store->jobs.erase(at);host->reply(id,"{\"ok\":true,\"released\":true}");return;
  }
  if(action=="accept"){
    uint64_t limit=32ull*1024*1024,timeout=60000;
    if((!xb_get(json,"max_bytes").empty()&&!xb_upload_uint(xb_get(json,"max_bytes"),limit))||!limit||limit>xb_download_file_limit||(!xb_get(json,"timeout_ms").empty()&&!xb_upload_uint(xb_get(json,"timeout_ms"),timeout))||timeout<1000||timeout>600000){fail("Choose a download byte budget of 1..64 MiB and a 1000..600000 ms deadline");return;}
    if(job->state!="pending"){host->reply(id,xb_download_status(job));return;}
    if(job->expected>=0&&static_cast<uint64_t>(job->expected)>limit){job->cancel("download_limit");host->reply(id,xb_download_status(job));return;}
    if(!store->initialize(host)||limit>xb_download_total_limit-store->reserved()){fail("Private download cache is unavailable or exceeds its reserved byte budget");return;}
    ULARGE_INTEGER free{};if(!GetDiskFreeSpaceExW(store->root.c_str(),&free,nullptr,nullptr)||free.QuadPart<limit+256ull*1024*1024){fail("Insufficient free space for this download reservation");return;}
    xb_upload_guard guard(store->root);if(guard.handle==INVALID_HANDLE_VALUE||!xb_download_collect_stale(store->root)){fail("Download cache collection is busy or has unexpected entries");return;}
    job->directory=store->root+L"\\"+webview::detail::widen_string(job->id);job->path=job->directory+L"\\payload";
    if(!CreateDirectoryW(job->directory.c_str(),nullptr)){fail("Cannot reserve a private download directory");return;}job->directoryOwned=true;
    job->lease=CreateFileW((job->directory+L"\\.lease").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_HIDDEN,nullptr);DWORD written=0;
    job->leaseOwned=job->lease!=INVALID_HANDLE_VALUE;
    if(job->lease==INVALID_HANDLE_VALUE||!WriteFile(job->lease,xb_download_inventory,sizeof(xb_download_inventory)-1,&written,nullptr)||written!=sizeof(xb_download_inventory)-1){job->cancel("download_cache_failed");fail("Cannot retain the private download cleanup inventory");return;}
    if(FAILED(job->args->put_ResultFilePath(job->path.c_str()))){job->cancel("download_start_failed");fail("Cannot direct this download to its reserved cache");return;}
    job->limit=limit;job->expires=GetTickCount64()+timeout;std::weak_ptr<xb_download_job> weak=job;
    auto* bytes=xb_cb<ICoreWebView2BytesReceivedChangedEventHandler,ICoreWebView2DownloadOperation*,IUnknown*>(IID_ICoreWebView2BytesReceivedChangedEventHandler,[weak](ICoreWebView2DownloadOperation*,IUnknown*){if(auto job=weak.lock())job->update();return S_OK;});
    auto* changed=xb_cb<ICoreWebView2StateChangedEventHandler,ICoreWebView2DownloadOperation*,IUnknown*>(IID_ICoreWebView2StateChangedEventHandler,[weak](ICoreWebView2DownloadOperation*,IUnknown*){if(auto job=weak.lock())job->update();return S_OK;});
    job->bytesSubscribed=SUCCEEDED(job->operation->add_BytesReceivedChanged(bytes,&job->bytesToken));job->stateSubscribed=SUCCEEDED(job->operation->add_StateChanged(changed,&job->stateToken));bytes->Release();changed->Release();
    if(!job->bytesSubscribed||!job->stateSubscribed){job->cancel("download_events_unavailable");fail("Cannot observe download progress and limits");return;}
    if(FAILED(job->args->put_Cancel(FALSE))){job->cancel("download_start_failed");fail("Cannot start this download in its reserved cache");return;}
    job->state="receiving";if(!job->finishDeferral())job->cancel("download_start_unconfirmed");job->update();host->reply(id,xb_download_status(job));return;
  }
  if(action=="read"){
    uint64_t offset=0,limit=65536;if(job->closing||job->state!="completed"||job->file==INVALID_HANDLE_VALUE||!xb_upload_uint(xb_get(json,"offset"),offset)||(!xb_get(json,"limit").empty()&&!xb_upload_uint(xb_get(json,"limit"),limit))||!limit||limit>65536||offset>job->size){fail("Read a verified completed download with a valid offset and 1..65536-byte range");return;}
    const auto count=static_cast<DWORD>(std::min<uint64_t>(limit,job->size-offset));xb_upload_bytes data(count);LARGE_INTEGER point{};point.QuadPart=offset;DWORD bytes=0;
    if(count&&(!SetFilePointerEx(job->file,point,nullptr,FILE_BEGIN)||!ReadFile(job->file,data.data(),count,&bytes,nullptr)||bytes!=count)){fail("Cannot read the exact verified download range");return;}
    std::string encoded;encoded.reserve((count+2)/3*4);static const char table[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for(size_t i=0;i<count;i+=3){uint32_t value=static_cast<uint32_t>(data[i])<<16;const bool second=i+1<count,third=i+2<count;
      if(second)value|=static_cast<uint32_t>(data[i+1])<<8;if(third)value|=data[i+2];
      encoded+=table[value>>18];encoded+=table[value>>12&63];encoded+=second?table[value>>6&63]:'=';encoded+=third?table[value&63]:'=';}
    host->reply(id,"{\"ok\":true,\"id\":"+xb_quote(job->id)+",\"offset\":"+std::to_string(offset)+",\"bytes\":"+std::to_string(bytes)+",\"next\":"+std::to_string(offset+bytes)+",\"eof\":"+(offset+bytes==job->size?"true":"false")+",\"sha256\":"+xb_quote(job->sha)+",\"base64\":"+xb_quote(encoded)+"}");return;
  }
  fail("Unsupported native download operation");
}
