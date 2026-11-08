/* Bounded event metadata only: no URL, title, page content or credentials. */
static void xb_navigation_record(const std::shared_ptr<xb_view>& view,const char* event,UINT64 id,bool blank=false){
  if(view->navigationEvents.size()>=12)view->navigationEvents.erase(view->navigationEvents.begin());
  view->navigationEvents.push_back("{\"event\":"+xb_quote(event)+",\"id\":"+xb_quote(std::to_string(id))+
    ",\"initial\":"+(view->initialNavigationPending?"true":"false")+(std::string(event)=="start"?std::string(",\"blank\":")+(blank?"true":"false"):"")+"}");
}
static std::string xb_navigation_events(const std::shared_ptr<xb_view>& view){
  std::string out="[";for(const auto& event:view->navigationEvents){if(out.size()>1)out+=",";out+=event;}return out+"]";
}
static bool xb_navigation_start(const std::shared_ptr<xb_view>& view,UINT64 id,bool blank){
  xb_navigation_record(view,"start",id,blank);
  if(view->initialNavigationPending&&blank)return false;
  view->navigation=id;view->processFailure.started(id);view->initialNavigationStarted=true;return true;
}
static bool xb_navigation_complete(const std::shared_ptr<xb_view>& view,UINT64 id){
  xb_navigation_record(view,"complete",id);
  if(id!=view->navigation||(view->initialNavigationPending&&!view->initialNavigationStarted))return false;
  view->initialNavigationPending=false;return true;
}
