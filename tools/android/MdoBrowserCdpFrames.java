package org.xserver.android;

import java.io.IOException;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

/** Worker-owned frame graph of one proven WebView. A renderer's tree omits
 * remote frames: recursively auto-attach only related iframe targets and join
 * their native parent/frame IDs. URLs/titles/opener hints grant no authority.
 * Isolated execution is delegated to the private world manager. Native owner,
 * input and transfer semantics are layered above this routing core. */
final class MdoBrowserCdpFrames implements MdoBrowserCdpSession.Events {
    private static final int FRAME_LIMIT=32,SESSION_LIMIT=64,DEPTH_LIMIT=32;
    private static final class Source {
        final String session,target,parent;boolean armed,started,ready,chooserEvents;
        final Map<String,Boolean> loading=new LinkedHashMap<>();
        Source(String session,String target,String parent){this.session=session;this.target=target;this.parent=parent;}
    }
    private static final class Node {
        final String raw,alias;String parent,loader,session,document;boolean loading,swapped;
        java.util.concurrent.atomic.AtomicBoolean epoch=new java.util.concurrent.atomic.AtomicBoolean(true);
        Node(String raw,String parent,String loader,String session,boolean root){this.raw=raw;this.parent=parent;this.loader=loader;this.session=session;
            alias=root?"f0":"f-"+UUID.randomUUID();document="fd-"+UUID.randomUUID();}
    }
    static final class Route {
        final String raw,frame,document,session,loader;private final List<String> ancestors;
        Route(Node node,List<String> ancestors){raw=node.raw;frame=node.alias;document=node.document;session=node.session;loader=node.loader;this.ancestors=ancestors;}
    }
    private final MdoBrowserCdpBinding binding;private final Thread owner=Thread.currentThread();
    private final MdoBrowserCdpWorlds worlds;private final MdoBrowserCdpChoosers choosers;
    private final Map<String,Source> sources=new LinkedHashMap<>();private final Map<String,Node> nodes=new LinkedHashMap<>();
    private final Set<String> seenSessions=new LinkedHashSet<>();private long revision;private String fault;
    private final java.util.concurrent.atomic.AtomicBoolean live=new java.util.concurrent.atomic.AtomicBoolean(true);
    private volatile java.util.concurrent.atomic.AtomicBoolean publishedRoot;
    MdoBrowserCdpFrames(MdoBrowserCdpBinding binding)throws IOException {
        if(binding==null)throw problem("cdp_frame_arguments_invalid");this.binding=binding;
        sources.put(null,new Source(null,binding.rootFrame,null));
        nodes.put(binding.rootFrame,new Node(binding.rootFrame,null,binding.rootLoader,null,true));worlds=new MdoBrowserCdpWorlds(binding,this);choosers=new MdoBrowserCdpChoosers(this);binding.observe(this);
    }
    private static IOException problem(String code){return new IOException(code);}
    private void current()throws IOException {
        if(Thread.currentThread()!=owner)throw problem("cdp_wrong_worker");if(fault!=null)throw problem(fault);binding.current();if(!live.get())throw problem("cdp_frame_retired");
    }
    private IOException invalid(String code){fault=code;live.set(false);return problem(code);}
    private static String id(JSONObject value,String field)throws IOException {
        Object text=value.opt(field);if(!(text instanceof String)||!MdoBrowserCdpJson.token((String)text))throw problem("cdp_frame_metadata_invalid");return(String)text;
    }
    private static String loader(JSONObject frame)throws IOException {
        Object value=frame.opt("loaderId");if(!(value instanceof String)||((String)value).length()>128)throw problem("cdp_frame_metadata_invalid");
        String text=(String)value;for(int i=0;i<text.length();i++){char c=text.charAt(i);
            if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||"_.:-".indexOf(c)>=0))throw problem("cdp_frame_metadata_invalid");}
        return text;
    }
    private Source source(String session)throws IOException {Source source=sources.get(session);if(source==null)throw invalid("cdp_frame_source_invalid");return source;}
    private boolean within(Node node,Source source){
        for(int depth=0;node!=null&&depth<DEPTH_LIMIT;depth++){
            if(!Objects.equals(node.session,source.session))return false;
            if(node.raw.equals(source.target))return true;node=nodes.get(node.parent);
        }return false;
    }
    private Node upsert(String raw,String parent,String load,String session,boolean committed)throws IOException {
        Node node=nodes.get(raw);
        if(node==null){if(nodes.size()>=FRAME_LIMIT)throw invalid("cdp_frame_limit");node=new Node(raw,parent,load,session,raw.equals(binding.rootFrame));nodes.put(raw,node);revision++;}
        else if(committed||!Objects.equals(parent,node.parent)||!Objects.equals(session,node.session)||load.isEmpty()||!load.equals(node.loader)){
            node.epoch.set(false);node.epoch=new java.util.concurrent.atomic.AtomicBoolean(true);node.document="fd-"+UUID.randomUUID();node.parent=parent;node.loader=load;node.session=session;node.swapped=false;revision++;
        }
        return node;
    }
    private void remove(String raw,boolean includeRoot)throws IOException {
        if(raw.equals(binding.rootFrame)&&includeRoot)throw invalid("cdp_frame_root_changed");
        List<String> remove=new ArrayList<>();remove.add(raw);
        for(int i=0;i<remove.size();i++)for(Node node:nodes.values())if(Objects.equals(node.parent,remove.get(i))&&!remove.contains(node.raw))remove.add(node.raw);
        for(String key:remove)if(includeRoot||!key.equals(raw)){Node old=nodes.remove(key);if(old!=null)old.epoch.set(false);for(Source source:sources.values())source.loading.remove(key);}revision++;
    }
    @Override public void event(String method,String session,JSONObject params)throws IOException {
        current();Source source=source(session);
        try{
            if(method.startsWith("Runtime.")){worlds.event(method,session,params);return;}
            if("Page.fileChooserOpened".equals(method)){choosers.event(session,params);return;}
            if("Target.attachedToTarget".equals(method)){
                String child=id(params,"sessionId");JSONObject target=params.getJSONObject("targetInfo");String raw=id(target,"targetId");
                if(!source.armed||!"iframe".equals(target.opt("type"))||!Boolean.FALSE.equals(params.opt("waitingForDebugger"))||raw.equals(binding.rootFrame))
                    throw invalid("cdp_frame_attachment_invalid");
                if(seenSessions.contains(child)||sources.size()>=SESSION_LIMIT||seenSessions.size()>=256)throw invalid("cdp_frame_session_limit");
                for(Source row:sources.values())if(raw.equals(row.target))throw invalid("cdp_frame_session_conflict");
                // Related-target events arrive only on a source we armed. Admit
                // metadata transport now; commands wait for a native tree join.
                binding.admitChild(child);seenSessions.add(child);sources.put(child,new Source(child,raw,session));revision++;return;
            }
            if("Target.detachedFromTarget".equals(method)){
                String child=id(params,"sessionId");Source gone=sources.get(child);
                if(gone==null){if(seenSessions.contains(child))return;throw invalid("cdp_frame_session_invalid");}
                if(child.equals(session)||!Objects.equals(gone.parent,session))throw invalid("cdp_frame_session_invalid");
                List<String> retired=new ArrayList<>();retired.add(child);
                for(int i=0;i<retired.size();i++)for(Source row:sources.values())if(Objects.equals(row.parent,retired.get(i))&&!retired.contains(row.session))retired.add(row.session);
                for(String key:retired){Source row=sources.remove(key);binding.retireChild(key);worlds.retire(key);Node node=nodes.get(row.target);if(node!=null){remove(node.raw,true);}}
                revision++;return;
            }
            if(!method.startsWith("Page.frame")&&!"Page.documentOpened".equals(method))return;
            JSONObject frame=params.optJSONObject("frame");String raw=frame==null?id(params,"frameId"):id(frame,"id");Node node=nodes.get(raw);
            if("Page.frameAttached".equals(method)){
                String parent=id(params,"parentFrameId");Node parentNode=nodes.get(parent);
                if(raw.equals(binding.rootFrame)||raw.equals(parent))throw invalid("cdp_frame_tree_invalid");
                if(parentNode==null){source.ready=false;revision++;return;}
                if(!within(parentNode,source))throw invalid("cdp_frame_parent_unproven");
                for(Node at=parentNode;at!=null;at=nodes.get(at.parent))if(at.raw.equals(raw))throw invalid("cdp_frame_tree_invalid");
                if(node!=null&&!Objects.equals(node.session,session))throw invalid("cdp_frame_source_invalid");
                upsert(raw,parent,"",session,false).loading=true;return;
            }
            if("Page.frameStartedLoading".equals(method)||"Page.frameStoppedLoading".equals(method)){
                if(!source.loading.containsKey(raw)&&source.loading.size()>=FRAME_LIMIT)throw invalid("cdp_frame_limit");
                source.loading.put(raw,"Page.frameStartedLoading".equals(method));
            }
            if(node==null){source.ready=false;revision++;return;} // No alias from an unjoined/late event.
            if(!Objects.equals(node.session,session)){
                // A parent renderer may report its own remote child detaching
                // during a process swap; it cannot mutate the child's DOM.
                if(!("Page.frameDetached".equals(method)||"Page.frameSubtreeWillBeDetached".equals(method))||!Objects.equals(source(node.session).parent,session))throw invalid("cdp_frame_source_invalid");
            }
            if("Page.frameSubtreeWillBeDetached".equals(method)){remove(raw,true);return;}
            if("Page.frameDetached".equals(method)){
                String reason=params.getString("reason");if("remove".equals(reason)){remove(raw,true);return;}
                if(!"swap".equals(reason)||raw.equals(binding.rootFrame))throw invalid("cdp_frame_detach_invalid");
                remove(raw,false);node.epoch.set(false);node.epoch=new java.util.concurrent.atomic.AtomicBoolean(true);node.document="fd-"+UUID.randomUUID();node.loading=true;node.swapped=true;revision++;return;
            }
            if("Page.frameNavigated".equals(method)||"Page.documentOpened".equals(method)){
                if(raw.equals(binding.rootFrame))throw invalid("cdp_frame_root_changed");
                String parent=frame.has("parentId")?id(frame,"parentId"):node.parent;
                Node parentNode=nodes.get(parent);Source parentSource=raw.equals(source.target)?source(source.parent):source;
                if(parentNode==null||!within(parentNode,parentSource)||raw.equals(parent))throw invalid("cdp_frame_parent_unproven");
                remove(raw,false);node=upsert(raw,parent,loader(frame),session,true);node.loading=true;source.loading.put(raw,true);return;
            }
            if("Page.frameStartedLoading".equals(method)){if(!node.loading){node.epoch.set(false);node.loading=true;revision++;}}
            else if("Page.frameStoppedLoading".equals(method)){if(node.loading){node.epoch=new java.util.concurrent.atomic.AtomicBoolean(true);node.loading=false;revision++;}}
        }catch(JSONException error){throw invalid("cdp_frame_metadata_invalid");}
    }
    private JSONObject call(Source source,String method,JSONObject params,MdoBrowserCdpWorker.Deadline deadline)throws IOException {
        current();if(sources.get(source.session)!=source)throw problem("cdp_frame_changed");
        JSONObject reply=binding.call(method,params,source.session,Math.min(5000,deadline.remaining()),false);
        current();if(sources.get(source.session)!=source)throw problem("cdp_frame_changed");return reply;
    }
    private void collect(Source source,JSONObject tree,Set<String> found,int depth,String expectedParent)throws IOException,JSONException {
        if(depth>=DEPTH_LIMIT)throw invalid("cdp_frame_depth_limit");JSONObject frame=tree.getJSONObject("frame");String raw=id(frame,"id"),load=loader(frame);
        if(!found.add(raw))throw invalid("cdp_frame_tree_invalid");
        String parent=frame.has("parentId")?id(frame,"parentId"):expectedParent;
        if(depth==0){
            if(!raw.equals(source.target))throw invalid("cdp_frame_target_mismatch");
            if(source.session==null){if(parent!=null||!load.equals(binding.rootLoader))throw invalid("cdp_frame_root_changed");}
            else {if(parent==null){Node old=nodes.get(raw);parent=old==null?null:old.parent;}
                Node ancestor=nodes.get(parent);if(ancestor==null||!within(ancestor,source(source.parent)))throw invalid("cdp_frame_parent_unproven");}
        }else if(!Objects.equals(parent,expectedParent))throw invalid("cdp_frame_tree_invalid");
        Node current=nodes.get(raw);
        if(current!=null&&!Objects.equals(current.session,source.session)&&!(depth==0&&source.session!=null&&Objects.equals(current.session,source.parent)))
            throw invalid("cdp_frame_source_conflict");
        Node node=upsert(raw,parent,load,source.session,false);node.swapped=false;
        if(source.loading.containsKey(raw))node.loading=source.loading.get(raw);
        JSONArray children=tree.optJSONArray("childFrames");if(tree.has("childFrames")&&children==null)throw invalid("cdp_frame_tree_invalid");
        if(children!=null){if(children.length()>FRAME_LIMIT)throw invalid("cdp_frame_limit");for(int i=0;i<children.length();i++)collect(source,children.getJSONObject(i),found,depth+1,raw);}
    }
    /** Read-only bootstrap/refresh. Every OOPIF tree must join a native parent
     * inside the exact auto-attachment source. No guessed/missing ancestry. */
    JSONObject refresh(MdoBrowserCdpWorker.Deadline deadline)throws IOException {
        current();try{
            for(int round=0;round<=SESSION_LIMIT;round++){
                for(Source source:new ArrayList<>(sources.values()))if(!source.started){
                    source.chooserEvents=choosers.requested();
                    if(source.chooserEvents)chooserEvents(source,true,deadline);else call(source,"Page.enable",new JSONObject(),deadline);
                    if(source.session!=null)call(source,"Runtime.enable",new JSONObject(),deadline);
                    source.armed=true;
                    call(source,"Target.setAutoAttach",new JSONObject().put("autoAttach",true).put("waitForDebuggerOnStart",false).put("flatten",true)
                        .put("filter",new JSONArray().put(new JSONObject().put("type","iframe"))),deadline);source.started=true;
                }
                for(Source source:new ArrayList<>(sources.values()))if(source.started){
                    long before=revision;source.ready=false;
                    JSONObject tree=call(source,"Page.getFrameTree",new JSONObject(),deadline).getJSONObject("frameTree");Set<String> found=new LinkedHashSet<>();
                    if(revision!=before)continue; // Discard a raced read; no effects are replayed.
                    collect(source,tree,found,0,null);
                    for(Node node:new ArrayList<>(nodes.values()))if(Objects.equals(node.session,source.session)&&!found.contains(node.raw))remove(node.raw,true);
                    source.loading.keySet().retainAll(found);
                    source.ready=true;
                }
                if(sources.values().stream().allMatch(source->source.started&&source.ready))return snapshot();
            }
            throw problem("cdp_frame_tree_busy");
        }catch(JSONException error){throw invalid("cdp_frame_metadata_invalid");}
    }
    private boolean attached(Node node,boolean requireReady){
        Set<String> seen=new LinkedHashSet<>();
        for(int depth=0;node!=null&&depth<DEPTH_LIMIT;depth++){
            Source source=sources.get(node.session);
            if(requireReady&&(source==null||!source.ready||node.loading||node.swapped||node.loader.isEmpty()))return false;
            if(!seen.add(node.raw))return false;if(node.raw.equals(binding.rootFrame))return node.parent==null&&node.session==null;
            node=nodes.get(node.parent);
        }return false;
    }
    private JSONObject snapshot()throws IOException,JSONException {
        current();JSONArray frames=new JSONArray();boolean complete=true;
        java.util.concurrent.atomic.AtomicBoolean rootReady=null;
        for(Node node:nodes.values()){
            Source source=sources.get(node.session);Node parent=nodes.get(node.parent);
            boolean linked=attached(node,false)&&source!=null&&source.ready&&!node.swapped;if(!linked)complete=false;
            boolean settled=linked&&attached(node,true),isolated=linked&&worlds.ready(node.alias,node.document);
            if(node.raw.equals(binding.rootFrame)&&settled&&isolated)rootReady=node.epoch;
            frames.put(new JSONObject().put("id",node.alias).put("document",node.document).put("parent",parent==null?JSONObject.NULL:parent.alias)
                .put("loading",!settled).put("isolated",isolated));
        }
        JSONObject result=new JSONObject().put("ok",true).put("complete",complete).put("frames",frames).put("generation",revision);
        publishedRoot=rootReady;return result;
    }
    /** Data-only status for the last published root. A native event revokes its
     * original atomic epoch; only a successful worker snapshot can publish a
     * replacement. UI callers never inspect the worker-owned graph/world maps. */
    boolean documentUnavailable(){java.util.concurrent.atomic.AtomicBoolean original=publishedRoot;return !live.get()||original==null||!original.get();}
    Route select(String frame,String document)throws IOException {
        current();Node selected=null;for(Node node:nodes.values())if(node.alias.equals(frame)&&node.document.equals(document))selected=node;
        if(selected==null)throw problem("cdp_frame_stale");return route(selected,true);
    }
    Route eventRoute(String raw,String session)throws IOException {
        current();Node selected=nodes.get(raw);
        if(selected==null||!Objects.equals(selected.session,session))throw problem("cdp_chooser_frame_unproven");
        // A Page event may precede a getFrameTree reply while source.ready is
        // transiently false. Preserve the last native route; every consumer
        // refreshes/revalidates it before using the event's metadata.
        return route(selected,false);
    }
    private Route route(Node selected,boolean ready)throws IOException {
        List<String> chain=new ArrayList<>();Node node=selected;
        for(int depth=0;node!=null&&depth<DEPTH_LIMIT;depth++){
            Source source=sources.get(node.session);if(source==null||ready&&!source.ready||node.loading||node.swapped||node.loader.isEmpty())throw problem("cdp_frame_not_ready");
            chain.add(node.alias);chain.add(node.document);if(node.raw.equals(binding.rootFrame))return new Route(selected,chain);node=nodes.get(node.parent);
        }throw problem("cdp_frame_parent_unproven");
    }
    void validate(Route route)throws IOException {
        current();if(route==null)throw problem("cdp_frame_arguments_invalid");Route now=select(route.frame,route.document);
        if(!now.raw.equals(route.raw)||!now.loader.equals(route.loader)||!Objects.equals(now.session,route.session)||!now.ancestors.equals(route.ancestors))throw problem("cdp_frame_stale");
    }
    /** Own isolated context loss is a reference fence even when a provider has
     * not yet delivered its corresponding Page document/loader event. */
    void worldLost(String raw,String session,String loader,String document)throws IOException {
        current();Node root=nodes.get(raw);if(root==null||!Objects.equals(root.session,session)||!root.loader.equals(loader)||!root.document.equals(document))return;
        List<String> changed=new ArrayList<>();changed.add(raw);
        for(int i=0;i<changed.size();i++)for(Node node:nodes.values())if(Objects.equals(node.parent,changed.get(i))&&!changed.contains(node.raw))changed.add(node.raw);
        for(String key:changed){Node node=nodes.get(key);node.epoch.set(false);node.epoch=new java.util.concurrent.atomic.AtomicBoolean(true);node.document="fd-"+UUID.randomUUID();}revision++;
    }
    JSONObject inventory(MdoBrowserCdpWorker.Deadline deadline)throws IOException {
        refresh(deadline);for(Node node:new ArrayList<>(nodes.values()))if(attached(node,true))worlds.prepare(select(node.alias,node.document),deadline);
        return refresh(deadline);
    }
    boolean responseReady(String raw)throws IOException {
        current();Node node=nodes.get(raw);return node!=null&&attached(node,true)&&worlds.ready(node.alias,node.document);
    }
    MdoBrowserCdpNetworkFrames.Origin responseOrigin(String raw)throws IOException {
        if(!responseReady(raw))throw problem("download_frame_unproven");Node node=nodes.get(raw);List<java.util.concurrent.atomic.AtomicBoolean> epochs=new ArrayList<>();epochs.add(live);
        for(Node at=node;at!=null;at=nodes.get(at.parent)){if(epochs.size()>DEPTH_LIMIT)throw problem("download_frame_unproven");epochs.add(at.epoch);}
        return binding.issueResponseOrigin(node.alias,node.document,raw.equals(binding.rootFrame),epochs.toArray(new java.util.concurrent.atomic.AtomicBoolean[0]));
    }
    void retire(){live.set(false);}
    JSONObject evaluate(String frame,String document,String expression,boolean effect,MdoBrowserCdpWorker.Deadline deadline)throws IOException {
        Route route=select(frame,document);refresh(deadline);validate(route);return worlds.evaluate(route,expression,effect,deadline);
    }
    MdoBrowserCdpWorlds.Pin pinWorld(String frame,String document,MdoBrowserCdpWorker.Deadline deadline)throws IOException {
        Route route=select(frame,document);refresh(deadline);validate(route);return worlds.pin(route);
    }
    void validateWorld(MdoBrowserCdpWorlds.Pin pin,MdoBrowserCdpWorker.Deadline deadline)throws IOException {refresh(deadline);worlds.validate(pin);}
    JSONObject evaluatePinned(MdoBrowserCdpWorlds.Pin pin,String expression,boolean effect,MdoBrowserCdpWorker.Deadline deadline)throws IOException {
        refresh(deadline);return worlds.evaluatePinned(pin,expression,effect,deadline);
    }
    JSONObject openPrivatePicker(MdoBrowserCdpWorlds.Pin pin,String key,MdoBrowserCdpWorker.Deadline deadline)throws IOException {
        refresh(deadline);return worlds.openPicker(pin,key,deadline);
    }
    JSONObject owner(String action,String frame,String document,JSONObject point,MdoBrowserCdpWorker.Deadline deadline)throws IOException {
        if(!("owner".equals(action)||"upload_policy".equals(action)||"capture_owner".equals(action)))throw problem("cdp_owner_action_invalid");
        JSONObject position=null;
        if("owner".equals(action)){
            if(point==null||point.length()!=2||!(point.opt("x") instanceof Number)||!(point.opt("y") instanceof Number))throw problem("cdp_owner_point_invalid");
            double x=((Number)point.opt("x")).doubleValue(),y=((Number)point.opt("y")).doubleValue();
            if(!Double.isFinite(x)||!Double.isFinite(y)||x<0||y<0||x>32768||y>32768)throw problem("cdp_owner_point_invalid");
            try{position=new JSONObject().put("x",x).put("y",y);}catch(JSONException error){throw problem("cdp_owner_point_invalid");}
        }else if(point!=null)throw problem("cdp_owner_point_invalid");
        Route child=select(frame,document);refresh(deadline);validate(child);Node node=nodes.get(child.raw),parent=nodes.get(node.parent);
        if(parent==null)throw problem("cdp_owner_root_unavailable");Route ancestor=select(parent.alias,parent.document);
        return worlds.owner(child,ancestor,action,position,deadline);
    }
    JSONObject fileInput(String frame,String document,String localDocument,String ref,MdoBrowserCdpWorker.Deadline deadline)throws IOException {
        Route route=select(frame,document);refresh(deadline);validate(route);return worlds.fileInput(route,localDocument,ref,deadline);
    }
    JSONObject clearFileInput(String frame,String document,String localDocument,String ref,MdoBrowserCdpFileInput.BeforeClear original,MdoBrowserCdpWorker.Deadline deadline)throws IOException {
        Route route=select(frame,document);refresh(deadline);validate(route);return worlds.clearFileInput(route,localDocument,ref,original,deadline);
    }
    JSONObject armChoosers(MdoBrowserCdpWorker.Deadline deadline)throws IOException {
        current();boolean start=choosers.begin();refresh(deadline);
        for(Source source:new ArrayList<>(sources.values()))if(!source.chooserEvents){source.chooserEvents=true;chooserEvents(source,true,deadline);}
        refresh(deadline);if(start)choosers.armed();return choosers.snapshot();
    }
    private void chooserEvents(Source source,boolean enabled,MdoBrowserCdpWorker.Deadline deadline)throws IOException {
        try{call(source,"Page.enable",new JSONObject().put("enableFileChooserOpenedEvent",enabled),deadline);}
        catch(MdoBrowserCdpSession.Failure failure){if(failure.protocolCode==-32601||failure.protocolCode==-32602)throw problem("cdp_chooser_events_unsupported");throw failure;}
        catch(JSONException error){throw problem("cdp_chooser_request_invalid");}
    }
    JSONObject disarmChoosers(MdoBrowserCdpWorker.Deadline deadline)throws IOException {
        current();choosers.disable();
        for(Source source:new ArrayList<>(sources.values()))if(source.chooserEvents){source.chooserEvents=false;chooserEvents(source,false,deadline);}
        return choosers.snapshot();
    }
    JSONObject listChoosers(MdoBrowserCdpWorker.Deadline deadline)throws IOException {refresh(deadline);return choosers.snapshot();}
    void validateChooser(String id)throws IOException {choosers.require(id);}
    JSONObject chooserTarget(String id,MdoBrowserCdpFrameChain.Pin pin,String document,MdoBrowserCdpWorker.Deadline deadline)throws IOException {
        refresh(deadline);MdoBrowserCdpChoosers.Candidate candidate=choosers.require(id);
        if(!pin.frame.equals(candidate.route.frame)||!pin.document.equals(candidate.route.document))throw problem("cdp_chooser_changed");
        JSONObject result=worlds.chooserInput(candidate.route,candidate.backend,document,deadline);choosers.validate(candidate);
        try{
            JSONObject value=result.getJSONObject("value"),element=value.getJSONObject("element");
            if(!element.getBoolean("directory")&&element.getBoolean("multiple")!=candidate.mode.equals("selectMultiple"))throw problem("cdp_chooser_target_changed");
            value.put("assignment_supported",false);return result.put("chooser",candidate.id);
        }catch(JSONException error){throw problem("cdp_chooser_result_invalid");}
    }
}
