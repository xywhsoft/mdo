// Typed screenshots only; CDP parameters never come from model-provided script.
static constexpr UINT_PTR xb_capture_timer=0xBADD3;
struct xb_capture_job;
struct xb_capture_callback{
  std::shared_ptr<xb_capture_job> job;bool completed=false;
  explicit xb_capture_callback(const std::shared_ptr<xb_capture_job>& value):job(value){}
  xb_capture_callback(const xb_capture_callback&)=delete;xb_capture_callback& operator=(const xb_capture_callback&)=delete;
  ~xb_capture_callback();
};
static std::map<HWND,std::weak_ptr<xb_capture_job>> xb_capture_jobs;
struct xb_capture_job:std::enable_shared_from_this<xb_capture_job>{
  xb_capture_job()=default;xb_capture_job(const xb_capture_job&)=delete;xb_capture_job& operator=(const xb_capture_job&)=delete;
  std::shared_ptr<xb_host> host;std::shared_ptr<xb_view> view;
  std::string id,key,format;double x=0,y=0,width=0,height=0,scale=1;size_t maximum=131072;
  UINT64 navigation=0;ULONGLONG expires=0;unsigned attempts=0;bool done=false,agent=true,inFlight=false,released=false,reserved=false;
  ~xb_capture_job(){release();}
  bool live()const{return host->alive&&!view->closed&&!view->requestedClose&&!view->failed&&!view->dialog&&!xb_popup_blocked(view)&&
    (!agent||!view->manual)&&view->navigation==navigation;}
  void detach(){if(view&&view->capture.get()==this)view->capture.reset();}
  void release(){if(released)return;released=true;detach();if(reserved&&host&&host->captures)host->captures--;reserved=false;}
  void settle(){
    if(done)return;done=true;KillTimer(view->window,xb_capture_timer);xb_capture_jobs.erase(view->window);
    if(!inFlight)release();else if(view->closed||!host->alive)detach();
  }
  void finish(const std::string& result,const std::string& error=""){
    if(done)return;settle();
    host->reply(id,result,error);
  }
  void fail(const char* reason)noexcept{try{finish("",reason);}catch(...){settle();}}
  void run(){
    try{
    if(done)return;if(!live()){finish("","Screenshot context changed");return;}
    if(GetTickCount64()>=expires){finish("","Screenshot deadline expired");return;}attempts++;
    const auto clip="{\"x\":"+std::to_string(x)+",\"y\":"+std::to_string(y)+",\"width\":"+std::to_string(width)+
      ",\"height\":"+std::to_string(height)+",\"scale\":"+std::to_string(scale)+"}";
    const auto params="{\"format\":"+xb_quote(format)+(format=="jpeg"?",\"quality\":80":"")+
      ",\"fromSurface\":true,\"captureBeyondViewport\":true,\"clip\":"+clip+"}";
    auto ticket=std::make_shared<xb_capture_callback>(shared_from_this());
    inFlight=true;xb_cdp(view,L"Page.captureScreenshot",params,[ticket](bool ok,const std::string& result){
      if(ticket->completed)return;ticket->completed=true;auto self=ticket->job;
      try{
      self->inFlight=false;if(self->done){self->release();return;}if(!self->live()){self->finish("","Screenshot context changed");return;}
      const auto data=xb_get(result,"data");if(!ok||data.empty()){self->finish("","Native screenshot failed");return;}
      if(data.size()%4){self->finish("","Native screenshot has an invalid encoding");return;}
      const size_t bytes=data.size()/4*3-(data.back()=='=')-(data.size()>1&&data[data.size()-2]=='=');
      if(bytes>self->maximum){
        if(self->attempts<4){self->scale*=0.5;self->run();return;}
        self->finish("","Screenshot exceeds its byte budget; capture a smaller area");return;
      }
      self->finish("{\"ok\":true,\"data\":"+xb_quote(data)+",\"format\":"+xb_quote(self->format)+
        ",\"scale\":"+std::to_string(self->scale)+",\"attempts\":"+std::to_string(self->attempts)+"}");
      }catch(...){self->fail("Native screenshot result could not be retained");}
    });
    }catch(...){fail("Native screenshot dispatch was not confirmed");}
  }
};
inline xb_capture_callback::~xb_capture_callback(){
  // Provider destruction is terminal ownership evidence even when the callback
  // was never invoked. A completed/duplicate old callback cannot settle a retry.
  if(!completed){completed=true;job->inFlight=false;if(job->done)job->release();else job->fail("Native screenshot callback was released");}
}
static void xb_cancel_capture(const std::shared_ptr<xb_view>& view){
  auto current=view->capture;if(current){current->fail("Screenshot was cancelled");if(view->closed||!current->host->alive)current->detach();}
}
static void CALLBACK xb_capture_tick(HWND window,UINT,UINT_PTR,DWORD){
  auto entry=xb_capture_jobs.find(window);if(entry==xb_capture_jobs.end()){KillTimer(window,xb_capture_timer);return;}
  auto job=entry->second.lock();if(!job){xb_capture_jobs.erase(entry);KillTimer(window,xb_capture_timer);return;}
  if(!job->live())job->finish("","Screenshot context changed");
  else if(GetTickCount64()>=job->expires)job->finish("","Screenshot deadline expired");
}
static double xb_capture_number(const std::string& json,const char* key,double fallback){
  const auto text=xb_get(json,key);if(text.empty())return fallback;
  try{size_t used=0;double value=std::stod(text,&used);if(used==text.size()&&std::isfinite(value))return value;}catch(...){}
  return -1e100;
}
static void xb_capture(const std::shared_ptr<xb_host>& host,const std::shared_ptr<xb_view>& view,const std::string& id,const std::string& json){
  if(host->captures||view->capture||view->drag||view->uploadBusy){host->reply(id,"","Another screenshot, upload or drag is still completing");return;}
  const auto clip=xb_get(json,"clip"),format=xb_get(json,"format"),key=xb_get(json,"capture_key");
  double x=xb_capture_number(clip,"x",0),y=xb_capture_number(clip,"y",0),width=xb_capture_number(clip,"width",0),height=xb_capture_number(clip,"height",0),scale=xb_capture_number(clip,"scale",1);
  double maximum=xb_capture_number(json,"max_bytes",131072),timeout=xb_capture_number(json,"timeout_ms",10000);
  if((format!="png"&&format!="jpeg")||key.empty()||key.size()>100||x< -32768||y< -32768||x>10000000||y>10000000||width<=0||height<=0||width>32768||height>32768||width*height>16777216||scale<0.001||scale>1||maximum<8192||maximum>262144||timeout<1||timeout>60000){host->reply(id,"","Invalid screenshot area or resource budget");return;}
  if(xb_get(json,"navigation")!=std::to_string(view->navigation)){host->reply(id,"","Screenshot document changed");return;}
  auto job=std::make_shared<xb_capture_job>();job->host=host;job->view=view;job->id=id;job->key=key;job->format=format;
  job->x=x;job->y=y;job->width=width;job->height=height;job->scale=scale;job->maximum=static_cast<size_t>(maximum);
  job->navigation=view->navigation;job->expires=GetTickCount64()+static_cast<ULONGLONG>(timeout);job->agent=xb_get(json,"agent")=="true";
  try{
    view->capture=job;host->captures++;job->reserved=true;xb_capture_jobs[view->window]=job;
    if(!SetTimer(view->window,xb_capture_timer,50,xb_capture_tick)){job->fail("Cannot retain screenshot deadline");return;}
    job->run();
  }catch(...){job->fail("Cannot retain screenshot operation");}
}
