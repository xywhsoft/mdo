#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cstring>
#include <cstdint>
#include <cassert>
#include <iostream>
namespace webview{namespace detail{
// PARSER
}}
static std::string xb_get(const std::string& value,const char* key){return webview::detail::json_parse(value,key,0);}
static std::string xb_quote(const std::string& value){return webview::detail::json_escape(value);}
struct xb_frame_source_identity{bool alive=true;};
// CONTEXT
struct xb_frame_session{std::shared_ptr<xb_frame_source_identity> identity=std::make_shared<xb_frame_source_identity>();};
struct xb_frame_state{bool closed=false;std::map<std::string,xb_frame_session> sessions;std::map<std::string,std::shared_ptr<xb_script_context>> scriptContexts;};
struct xb_view{std::shared_ptr<xb_frame_state> frames=std::make_shared<xb_frame_state>();};
// REGISTRY
static std::string created(const std::string& frame,const std::string& unique,int number=7,bool normal=true){
  return "{\"context\":{\"id\":"+std::to_string(number)+",\"uniqueId\":"+xb_quote(unique)+",\"auxData\":{\"isDefault\":"+(normal?"true":"false")+",\"frameId\":"+xb_quote(frame)+"}}}";
}
int main(){
  auto view=std::make_shared<xb_view>();auto state=view->frames;
  const auto create=[&](std::string frame,std::string unique,int number=7,std::string source="",bool normal=true){xb_script_metadata(view,"Runtime.executionContextCreated",source,created(frame,unique,number,normal));};
  create("root","original");assert(state->scriptContexts.size()==1);auto first=state->scriptContexts.begin()->second;
  create("root","original");assert(state->scriptContexts.begin()->second==first);
  create("root","replacement");assert(!first->alive&&state->scriptContexts.size()==1);auto current=state->scriptContexts.begin()->second;
  xb_script_metadata(view,"Runtime.executionContextDestroyed","","{\"executionContextUniqueId\":\"original\",\"executionContextId\":7}");assert(current->alive);
  state->sessions.emplace("child",xb_frame_session{});create("child-frame","child-world",7,"child");assert(state->scriptContexts.size()==2);
  xb_script_metadata(view,"Runtime.executionContextsCleared","child","{}");assert(state->scriptContexts.size()==1&&current->alive);
  create("ignored","isolated",8,"",false);assert(state->scriptContexts.size()==1);
  create("ignored","",8);create("ignored","invalid",0);create("ignored","invalid",8,"unknown");assert(state->scriptContexts.size()==1);
  xb_script_metadata(view,"Runtime.executionContextDestroyed","","{\"executionContextUniqueId\":\"replacement\"}");assert(!current->alive&&state->scriptContexts.empty());
  for(int index=0;index<140;index++)create("frame-"+std::to_string(index),"world-"+std::to_string(index),index+1);
  assert(state->scriptContexts.size()==128);
  xb_script_metadata(view,"Runtime.executionContextsCleared","","{}");assert(state->scriptContexts.empty());
  state->closed=true;create("root","closed");assert(state->scriptContexts.empty());
  auto budget=std::make_shared<xb_script_budget>();std::vector<std::shared_ptr<xb_script_reservation>> callbacks;
  for(int index=0;index<16;index++)callbacks.push_back(std::make_shared<xb_script_reservation>(budget));
  assert(budget->pending==16);
  // A client timeout does not destroy its still-pending provider callback.
  auto original=callbacks[0];assert(budget->pending==16);callbacks.erase(callbacks.begin());assert(budget->pending==16);
  original->release();assert(budget->pending==15);original->release();assert(budget->pending==15);
  callbacks.push_back(std::make_shared<xb_script_reservation>(budget));assert(budget->pending==16);
  callbacks.clear();assert(budget->pending==0);original.reset();assert(budget->pending==0);
  std::cout<<"Native script context and pending-reservation lifetime checks PASS\n";
}
