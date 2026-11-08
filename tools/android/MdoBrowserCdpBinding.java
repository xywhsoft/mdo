package org.xserver.android;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

/** Private worker binding to an exact native UI marker. No URL/title/geometry,
 * supplied debugger address or caller-picked target is an ownership proof.
 * Unbound candidates receive only context metadata/lexical-name enumeration;
 * the random marker name is never sent to a nonmatching candidate. */
final class MdoBrowserCdpBinding implements AutoCloseable,MdoBrowserCdpDownloads.AttachedSource {
    interface Targets {List<String> discover(int timeoutMs) throws IOException;}
    interface Peer extends AutoCloseable {
        /** 0 unknown/open, 1 pending, 2 confirmed, 3 failed; absence of a
         * receipt must never turn a once-only close flag into success. */
        default int closeState(){return 0;}
        JSONObject call(String method,JSONObject params,int timeoutMs) throws IOException;
        default JSONObject call(String method,JSONObject params,String child,int timeoutMs,boolean effect)throws IOException {
            if(child!=null||effect)throw new IOException("cdp_session_routing_unsupported");return call(method,params,timeoutMs);
        }
        default void admitChild(String child)throws IOException {throw new IOException("cdp_session_routing_unsupported");}
        default void retireChild(String child)throws IOException {throw new IOException("cdp_session_routing_unsupported");}
        default boolean poll(int timeoutMs)throws IOException {throw new IOException("cdp_session_routing_unsupported");}
        default boolean poll(int receiveMs,int timeoutMs)throws IOException {throw new IOException("cdp_session_routing_unsupported");}
        default boolean idle()throws IOException {throw new IOException("cdp_session_routing_unsupported");}
        default MdoBrowserCdpSession.ControlReceipt continuePausedRequest()throws IOException {throw new IOException("cdp_session_routing_unsupported");}
        default MdoBrowserCdpSession.ControlReceipt abortPausedRequest()throws IOException {throw new IOException("cdp_session_routing_unsupported");}
        default MdoBrowserCdpResponse pausedResponse()throws IOException {throw new IOException("cdp_session_routing_unsupported");}
        default MdoBrowserCdpSession.Offer retainPausedResponse()throws IOException {throw new IOException("cdp_session_routing_unsupported");}
        default int pendingOffers()throws IOException {throw new IOException("cdp_session_routing_unsupported");}
        default void cancelTransfers()throws IOException {throw new IOException("cdp_session_routing_unsupported");}
        default MdoBrowserCdpSession.Body takePausedBody(long limit,int timeout)throws IOException {throw new IOException("cdp_session_routing_unsupported");}
        default MdoBrowserCdpSession.BodyChunk readBody(MdoBrowserCdpSession.Body body,int timeout)throws IOException {throw new IOException("cdp_session_routing_unsupported");}
        default void cancelBody(MdoBrowserCdpSession.Body body,int timeout)throws IOException {throw new IOException("cdp_session_routing_unsupported");}
        default int pendingBodies()throws IOException {throw new IOException("cdp_session_routing_unsupported");}
        default void requestBodyRead(MdoBrowserCdpSession.Body body)throws IOException {throw new IOException("cdp_session_routing_unsupported");}
        default MdoBrowserCdpSession.BodyChunk takeBodyChunk(MdoBrowserCdpSession.Body body)throws IOException {throw new IOException("cdp_session_routing_unsupported");}
        default boolean serviceBodies(int timeout)throws IOException {throw new IOException("cdp_session_routing_unsupported");}
        default int pendingControls()throws IOException {throw new IOException("cdp_session_routing_unsupported");}
        default MdoBrowserCdpSession.Navigation navigateRoot(String url,String frame,int timeout)throws IOException {throw new IOException("cdp_session_routing_unsupported");}
        default MdoBrowserCdpSession.Navigation historyRoot(String frame,int entry,int timeout)throws IOException {throw new IOException("cdp_session_routing_unsupported");}
        default MdoBrowserCdpSession.Navigation reloadRoot(String frame,String loader,int timeout)throws IOException {throw new IOException("cdp_session_routing_unsupported");}
        @Override void close();
    }
    interface Factory {Peer open(String target,int timeoutMs,java.util.function.BooleanSupplier current,MdoBrowserCdpSession.Events events) throws IOException;}
    private static final Factory NATIVE=(target,timeout,current,events)->{
        MdoBrowserCdpSession session=MdoBrowserCdpSession.open(target,timeout,current,events);
        try {return new Peer(){
                public JSONObject call(String method,JSONObject params,int wait) throws IOException{return session.call(method,params,null,wait,false);}
                public JSONObject call(String method,JSONObject params,String child,int wait,boolean effect)throws IOException{return session.call(method,params,child,wait,effect);}
                public void admitChild(String child)throws IOException{session.admitChild(child);}
                public void retireChild(String child)throws IOException{session.retireChild(child);}
                public boolean poll(int wait)throws IOException{return session.poll(wait);}
                public boolean poll(int receive,int wait)throws IOException{return session.poll(receive,wait);}
                public boolean idle()throws IOException{return session.idle();}
                public MdoBrowserCdpSession.ControlReceipt continuePausedRequest()throws IOException{return session.continuePausedRequest();}
                public MdoBrowserCdpSession.ControlReceipt abortPausedRequest()throws IOException{return session.abortPausedRequest();}
                public MdoBrowserCdpResponse pausedResponse()throws IOException{return session.pausedResponse();}
                public MdoBrowserCdpSession.Offer retainPausedResponse()throws IOException{return session.retainPausedResponse();}
                public int pendingOffers()throws IOException{return session.pendingOffers();}
                public void cancelTransfers()throws IOException{session.cancelTransfers();}
                public MdoBrowserCdpSession.Body takePausedBody(long limit,int timeout)throws IOException{return session.takePausedBody(limit,timeout);}
                public MdoBrowserCdpSession.BodyChunk readBody(MdoBrowserCdpSession.Body body,int timeout)throws IOException{return session.readBody(body,timeout);}
                public void cancelBody(MdoBrowserCdpSession.Body body,int timeout)throws IOException{session.cancelBody(body,timeout);}
                public int pendingBodies()throws IOException{return session.pendingBodies();}
                public void requestBodyRead(MdoBrowserCdpSession.Body body)throws IOException{session.requestBodyRead(body);}
                public MdoBrowserCdpSession.BodyChunk takeBodyChunk(MdoBrowserCdpSession.Body body)throws IOException{return session.takeBodyChunk(body);}
                public boolean serviceBodies(int timeout)throws IOException{return session.serviceBodies(timeout);}
                public int pendingControls()throws IOException{return session.pendingControls();}
                public MdoBrowserCdpSession.Navigation navigateRoot(String url,String frame,int timeout)throws IOException{return session.navigateRoot(url,frame,timeout);}
                public MdoBrowserCdpSession.Navigation historyRoot(String frame,int entry,int timeout)throws IOException{return session.historyRoot(frame,entry,timeout);}
                public MdoBrowserCdpSession.Navigation reloadRoot(String frame,String loader,int timeout)throws IOException{return session.reloadRoot(frame,loader,timeout);}
                public void close(){session.close();}
                public int closeState(){return session.closeState();}
            };
        }catch(RuntimeException|Error failure){session.close();throw failure;}
    };
    static List<String> targets(int timeoutMs) throws IOException {
        return parseTargets(MdoBrowserCdpNative.discover(timeoutMs));
    }
    static List<String> parseTargets(byte[] bytes) throws IOException {
        if(bytes==null)throw problem("cdp_binding_discovery_invalid");
        try {
            MdoBrowserCdpNative.validate(bytes,MdoBrowserCdpJson.SEND_MAX);
            JSONArray rows=new JSONArray(new String(bytes,java.nio.charset.StandardCharsets.UTF_8));
            if(rows.length()>64)throw problem("cdp_binding_target_limit");
            List<String> ids=new ArrayList<>();Set<String> seen=new HashSet<>();
            for(int i=0;i<rows.length();i++) {
                JSONObject row=rows.getJSONObject(i);Object type=row.get("type");
                if(!(type instanceof String))throw problem("cdp_binding_discovery_invalid");
                if(!"page".equals(type))continue;
                Object id=row.get("id");
                if(!(id instanceof String)||!MdoBrowserCdpJson.token((String)id)||!seen.add((String)id))throw problem("cdp_binding_discovery_invalid");
                ids.add((String)id);
            }
            return ids;
        } catch(JSONException failure){throw problem("cdp_binding_discovery_invalid");}
        finally {Arrays.fill(bytes,(byte)0);}
    }
    private static IOException problem(String code){return new IOException(code);}
    private static boolean identity(Object value) {
        if(!(value instanceof String)||((String)value).isEmpty()||((String)value).length()>128)return false;
        for(char c:((String)value).toCharArray())if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||"_.:-".indexOf(c)>=0))return false;
        return true;
    }
    private static final class Context {
        final int id;final String unique,frame;String loader;
        Context(int id,String unique,String frame){this.id=id;this.unique=unique;this.frame=frame;}
    }
    private static final class Contexts implements MdoBrowserCdpSession.Events {
        final Map<Integer,Context> contexts=new HashMap<>();final Set<Integer> seen=new HashSet<>();
        final Map<Integer,String> identities=new HashMap<>();
        volatile boolean dirty;boolean unsupported,network;Context armed;volatile MdoBrowserCdpSession.Events forward;
        public void event(String method,String session,JSONObject params) throws IOException {
            try {
                if(network){
                    // Ownership was proved before promotion on the original
                    // connection. New document metadata grants no frame/world
                    // authority and must not accumulate over a view lifetime.
                    if("Inspector.detached".equals(method)||"Inspector.targetCrashed".equals(method)||"Inspector.targetReloadedAfterCrash".equals(method))dirty=true;
                    MdoBrowserCdpSession.Events receiver=forward;if(receiver!=null)receiver.event(method,session,params);return;
                }
                if(session!=null){MdoBrowserCdpSession.Events childReceiver=forward;
                    if(childReceiver==null)throw problem("cdp_binding_context_invalid");childReceiver.event(method,session,params);return;}
                if("Runtime.executionContextCreated".equals(method)) {
                    JSONObject value=params.getJSONObject("context");Object number=value.get("id");
                    if(!(number instanceof Integer)||((Integer)number)<=0||!seen.add((Integer)number)||seen.size()>256)throw problem("cdp_binding_context_invalid");
                    identities.put((Integer)number,identity(value.opt("uniqueId"))?(String)value.opt("uniqueId"):null);
                    JSONObject aux=value.optJSONObject("auxData");
                    if(aux!=null&&Boolean.TRUE.equals(aux.opt("isDefault"))) {
                        Object unique=value.opt("uniqueId"),frame=aux.opt("frameId");
                        if(!identity(unique)||!identity(frame)){unsupported=true;throw problem("cdp_binding_context_unsupported");}
                        if(contexts.size()>=64)throw problem("cdp_binding_context_limit");
                        Context context=new Context((Integer)number,(String)unique,(String)frame);contexts.put(context.id,context);
                        if(armed!=null&&armed.frame.equals(context.frame))dirty=true;
                    }
                } else if("Runtime.executionContextDestroyed".equals(method)) {
                    int id=params.getInt("executionContextId");contexts.remove(id);identities.remove(id);if(armed!=null&&armed.id==id)dirty=true;
                } else if("Runtime.executionContextsCleared".equals(method)) {contexts.clear();identities.clear();if(armed!=null)dirty=true;}
                else if("Inspector.detached".equals(method))dirty=true;
                MdoBrowserCdpSession.Events receiver=forward;if(receiver!=null)receiver.event(method,session,params);
            } catch(JSONException failure){throw problem("cdp_binding_context_invalid");}
        }
        Context root(String frame) throws IOException {
            Context root=null;for(Context context:contexts.values())if(frame.equals(context.frame)) {
                if(root!=null)throw problem("cdp_binding_context_invalid");root=context;
            }
            if(root==null)throw problem("cdp_binding_context_unsupported");armed=root;return root;
        }
        void network(){network=true;armed=null;contexts.clear();identities.clear();seen.clear();}
    }
    private final Peer peer;private final MdoBrowserCdpMarker.Lease lease;private final Contexts contexts;private final String target;
    private final Thread owner=Thread.currentThread();private boolean controllerInstalled;private volatile MdoBrowserCdpFrames frames;private Network sourceNavigation;
    private final java.util.concurrent.atomic.AtomicBoolean closed=new java.util.concurrent.atomic.AtomicBoolean();
    private final java.util.concurrent.atomic.AtomicBoolean cleanupFailed=new java.util.concurrent.atomic.AtomicBoolean();
    private final java.util.concurrent.atomic.AtomicBoolean closeFinished=new java.util.concurrent.atomic.AtomicBoolean();
    private final Object readAuthority=new Object();
    final String rootFrame,rootLoader,uniqueContext;final int contextId;
    // Fresh namespace per binding. Specific names belong to native frame/
    // loader proofs; old or possibly privileged worlds are never adopted.
    final String worldName="mdo.browser-use.frames.v1."+java.util.UUID.randomUUID();
    private MdoBrowserCdpBinding(Peer peer,MdoBrowserCdpMarker.Lease lease,Contexts contexts,Context root,String target) {
        this.peer=peer;this.lease=lease;this.contexts=contexts;this.target=target;rootFrame=root.frame;rootLoader=root.loader;uniqueContext=root.unique;contextId=root.id;
    }
    static MdoBrowserCdpBinding bind(MdoBrowserCdpMarker marker,int timeoutMs,MdoBrowserCdpSession.Events events) throws IOException {
        return bind(marker,timeoutMs,events,MdoBrowserCdpBinding::targets,NATIVE);
    }
    static MdoBrowserCdpBinding bind(MdoBrowserCdpMarker marker,int timeoutMs,MdoBrowserCdpSession.Events events,Targets targets,Factory factory) throws IOException {
        return bind(marker,timeoutMs,events,targets,factory,false);
    }
    private static MdoBrowserCdpBinding bind(MdoBrowserCdpMarker marker,int timeoutMs,MdoBrowserCdpSession.Events events,Targets targets,Factory factory,boolean network) throws IOException {
        return bind(marker,timeoutMs,events,targets,factory,network,null);
    }
    private static MdoBrowserCdpBinding bind(MdoBrowserCdpMarker marker,int timeoutMs,MdoBrowserCdpSession.Events events,Targets targets,Factory factory,boolean network,java.util.function.BooleanSupplier operation) throws IOException {
        return bind(marker,timeoutMs,events,targets,factory,network,operation,false);
    }
    private static MdoBrowserCdpBinding bind(MdoBrowserCdpMarker marker,int timeoutMs,MdoBrowserCdpSession.Events events,Targets targets,Factory factory,boolean network,java.util.function.BooleanSupplier operation,boolean attached) throws IOException {
        if(marker==null||events==null||targets==null||factory==null||timeoutMs<100||timeoutMs>15000)throw problem("cdp_binding_arguments_invalid");
        long started=android.os.SystemClock.elapsedRealtime();MdoBrowserCdpMarker.Lease lease=marker.lease();
        if(lease.attached()&&!attached)throw problem("cdp_attached_binding_required");
        if(lease.physicalNetwork()!=network)throw problem("cdp_network_scope_invalid");
        if(lease.busy())throw problem("cdp_binding_busy");
        MdoBrowserCdpMarker.Ticket ticket=null;Peer peer=null;boolean success=false,cleanupAttempted=false;
        Context cleanupRoot=null;Contexts cleanupOwner=null;
        try {
            List<String> candidates=targets.discover(remaining(started,timeoutMs,lease));
            if(candidates.isEmpty()||candidates.size()>64||new HashSet<>(candidates).size()!=candidates.size())throw problem("cdp_binding_targets_unavailable");
            for(String id:candidates)if(!MdoBrowserCdpJson.token(id))throw problem("cdp_binding_discovery_invalid");
            remaining(started,timeoutMs,lease);operation(operation);ticket=marker.request(operation);
            if(!ticket.await(remaining(started,timeoutMs,lease))) {
                remaining(started,timeoutMs,lease);
                throw problem(ticket.pending()?"cdp_binding_timeout":ticket.failure());
            }
            boolean unsupported=false;
            for(String id:candidates) {
                operation(operation);
                Contexts contexts=new Contexts();Context root;
                try {
                    peer=factory.open(id,remaining(started,timeoutMs,lease),()->lease.current()&&!contexts.dirty,contexts);
                    lease.track(peer);
                    root=match(peer,contexts,ticket,started,timeoutMs,lease);
                } catch(IOException|JSONException missing) {
                    unsupported|=contexts.unsupported||"cdp_binding_context_unsupported".equals(missing.getMessage());
                    unsupported|=missing instanceof MdoBrowserCdpSession.Failure&&((MdoBrowserCdpSession.Failure)missing).protocolCode==-32601;
                    if(peer!=null){lease.untrack(peer);peer.close();peer=null;}
                    operation(operation);remaining(started,timeoutMs,lease);continue;
                }
                if(root==null){lease.untrack(peer);peer.close();peer=null;continue;}
                // Prove the provider honors uniqueContextId before revealing the
                // random identifier in an expression. Unknown parameters must
                // never silently retarget to the current default document.
                boolean rejectsUnique=false;
                try {peer.call("Runtime.evaluate",evaluation("void 0","",true,remaining(started,timeoutMs,lease)),remaining(started,timeoutMs,lease));}
                catch(MdoBrowserCdpSession.Failure failure){rejectsUnique=failure.uniqueContextRejected;}
                if(!rejectsUnique)throw problem("cdp_binding_unique_context_unsupported");
                JSONObject check=peer.call("Runtime.evaluate",evaluation("void 0",root.unique,true,remaining(started,timeoutMs,lease)),remaining(started,timeoutMs,lease));
                if(check.has("exceptionDetails")||!"undefined".equals(check.getJSONObject("result").opt("type")))throw problem("cdp_binding_context_invalid");
                cleanupRoot=root;cleanupOwner=contexts;
                JSONObject proof=peer.call("Runtime.evaluate",evaluation(ticket.name,root.unique,true,remaining(started,timeoutMs,lease)),remaining(started,timeoutMs,lease));
                JSONObject value=proof.getJSONObject("result");
                if(proof.has("exceptionDetails")||!"string".equals(value.opt("type"))||!ticket.value.equals(value.opt("value")))throw problem("cdp_binding_proof_invalid");
                cleanupAttempted=true;
                JSONObject clean=peer.call("Runtime.evaluate",evaluation(ticket.name+"=void 0",root.unique,false,remaining(started,timeoutMs,lease)),remaining(started,timeoutMs,lease));
                if(clean.has("exceptionDetails")||!"undefined".equals(clean.getJSONObject("result").opt("type")))throw problem("cdp_binding_cleanup_failed");
                JSONObject finalFrame=frame(peer,started,timeoutMs,lease);
                if(!root.frame.equals(finalFrame.opt("id"))||!root.loader.equals(finalFrame.opt("loaderId")))throw problem("cdp_binding_context_changed");
                if(contexts.dirty||!ticket.confirmed())throw problem("cdp_binding_owner_changed");
                remaining(started,timeoutMs,lease);contexts.forward=events;
                MdoBrowserCdpBinding bound=new MdoBrowserCdpBinding(peer,lease,contexts,root,id);
                lease.transfer(peer,bound);
                if(network)contexts.network();
                ticket.close();success=true;return bound;
            }
            throw problem(unsupported?"cdp_binding_context_unsupported":"cdp_binding_target_not_found");
        } catch(JSONException failure){throw problem("cdp_binding_packet_invalid");}
        catch(RuntimeException|Error failure){throw problem("cdp_binding_failed");}
        finally {
            if(!success){
                if(peer!=null){
                    // One bounded attempt on this exact proven context only.
                    // Never resend uncertain cleanup or target a default view.
                    if(cleanupRoot!=null&&!cleanupAttempted&&lease.current()&&!cleanupOwner.dirty)try {
                        int left=remaining(started,timeoutMs,lease);
                        peer.call("Runtime.evaluate",evaluation(ticket.name+"=void 0",cleanupRoot.unique,false,left),left);
                    }catch(IOException|JSONException|RuntimeException|Error ignored){}
                    lease.untrack(peer);peer.close();
                }
                if(ticket!=null)marker.discard(ticket);
            }
        }
    }
    private static void operation(java.util.function.BooleanSupplier scope)throws IOException {
        if(scope!=null&&!scope.getAsBoolean())throw problem("cdp_binding_owner_changed");
    }
    /** Use the existing document witness on the original proved channel.
     * No discovery or new connection; native document/unique-context checks
     * still precede frame/world operations. */
    static MdoBrowserCdpBinding bindAttached(MdoBrowserCdpPopupBroker.DocumentPeer peer,MdoBrowserCdpPopupNative.Document document,MdoBrowserCdpMarker marker,int timeout)throws IOException {
        if(peer==null||document==null||marker==null||marker.lease()!=document.lease||!peer.matches(document)||!document.current())throw problem("cdp_popup_document_owner_invalid");
        final String target=peer.target();
        MdoBrowserCdpBinding bound=bind(marker,timeout,(method,child,params)->{},ignored->java.util.Collections.singletonList(target),
            (id,budget,current,events)->{if(!target.equals(id))throw problem("cdp_popup_document_owner_invalid");peer.observe(events);return peer;},false,document::current,true);
        try{peer.controller(bound);return bound;}catch(IOException|RuntimeException|Error error){bound.close();throw error;}
    }
    static MdoBrowserCdpBinding bindSource(MdoBrowserCdpPopupSource peer,MdoBrowserCdpPopupNative.Source source,MdoBrowserCdpMarker marker,int timeout)throws IOException {
        if(peer==null||source==null||marker==null||source.lease!=marker.lease()||!source.current()||!peer.matches(source))throw problem("cdp_popup_source_owner_invalid");
        Contexts contexts=new Contexts();contexts.network();Context root=new Context(0,"",peer.frame());root.loader=peer.loader();
        MdoBrowserCdpBinding bound=new MdoBrowserCdpBinding(peer,source.lease,contexts,root,peer.target());boolean done=false;
        try{source.lease.track(bound);peer.observe(contexts);peer.bootstrap(timeout);done=true;return bound;}finally{if(!done)bound.close();}
    }
    /** A separate physical-view proof, never a promotion of an ordinary
     * document binding. This private facade exposes no page/frame RPC. */
    static Network bindNetwork(MdoBrowserCdpMarker marker,int timeoutMs,MdoBrowserCdpSession.Events events)throws IOException {
        return bindNetwork(marker,timeoutMs,events,MdoBrowserCdpBinding::targets,NATIVE);
    }
    static Network bindNetwork(MdoBrowserCdpMarker marker,int timeoutMs,MdoBrowserCdpSession.Events events,Targets targets,Factory factory)throws IOException {
        return bindNetwork(marker,timeoutMs,events,targets,factory,null);
    }
    private static Network bindNetwork(MdoBrowserCdpMarker marker,int timeoutMs,MdoBrowserCdpSession.Events events,Targets targets,Factory factory,java.util.function.BooleanSupplier operation)throws IOException {
        long start=android.os.SystemClock.elapsedRealtime();MdoBrowserCdpBinding bound=bind(marker,timeoutMs,events,targets,factory,true,operation);
        try{
            // Runtime metadata is only needed for the initial exact target
            // proof. Stop its stream before a long-lived network connection.
            bound.peer.call("Runtime.disable",new JSONObject(),remaining(start,timeoutMs,bound.lease));
            bound.current();return new Network(bound);
        }catch(IOException|RuntimeException|Error failure){bound.close();throw failure;}
    }
    /** Same physical-marker proof on only the frozen retained target. A second
     * private target connection cannot resume the Browser connection's wait.
     * No discovery guess, caller target ID, navigation or body access occurs. */
    static Network bindPopupNative(MdoBrowserCdpPopupNative.Child child,List<MdoBrowserCdpSession.AttachedTarget> originals,int timeout,
                                  java.util.function.BooleanSupplier current,Factory factory)throws IOException {
        if(child==null||originals==null||originals.isEmpty()||originals.size()>7||current==null||!current.getAsBoolean()||!child.current())throw problem("cdp_popup_native_child_invalid");
        List<String> targets=new ArrayList<>();
        for(MdoBrowserCdpSession.AttachedTarget original:originals){
            if(original==null||!original.waiting||!"page".equals(original.type)||!original.protectionConfirmed()||original.resumeConfirmed()||original.detachConfirmed()||targets.contains(original.target))throw problem("cdp_popup_native_child_invalid");
            targets.add(original.target);
        }
        Factory transport=factory==null?NATIVE:factory;
        java.util.function.BooleanSupplier operation=()->child.current()&&current.getAsBoolean();
        Network bound=bindNetwork(child.marker(),timeout,(m,s,p)->{},ignored->targets,
            (target,wait,alive,events)->{
                if(!operation.getAsBoolean()||!alive.getAsBoolean())throw problem("cdp_popup_native_child_changed");
                return transport.open(target,wait,()->alive.getAsBoolean()&&operation.getAsBoolean(),events);
            },operation);
        if(!child.current()||!current.getAsBoolean()){bound.close();throw problem("cdp_popup_native_child_changed");}return bound;
    }
    static final class Network implements AutoCloseable {
        /** Constructed only after the original physical WebView proof. It is
         * an opener registry key, never a proof of any new child WebView. */
        static final class PopupSource {
            private final Network original;
            private PopupSource(Network original){this.original=original;}
            String target()throws IOException{original.active();if(original.retiring)throw problem("cdp_network_retiring");return original.bound.target;}
            boolean current(){return original.enabled&&!original.retiring&&!original.bound.closed.get()&&original.bound.lease.current()&&!original.bound.contexts.dirty;}
            boolean owns(MdoBrowserCdpMarker.Lease lease){return current()&&original.bound.lease==lease;}
        }
        private PopupSource popupSource;
        boolean physicalCurrent(){return !retiring&&!bound.closed.get()&&bound.lease.current()&&!bound.contexts.dirty;}
        boolean matchesPopup(MdoBrowserCdpSession.AttachedTarget original){return original!=null&&bound.target.equals(original.target)&&physicalCurrent();}
        String sourceFrame(){return bound.rootFrame;}String sourceLoader(){return bound.rootLoader;}
        PopupSource popupSource()throws IOException{ordinary();active();if(retiring)throw problem("cdp_network_retiring");if(popupSource==null)popupSource=new PopupSource(this);return popupSource;}
        /** Ephemeral native selection, not a public entry-ID or URL lookup. */
        private static final class Selection {
            final int current,entry;final byte[] currentDestination,destination;final String action;
            private Selection(int current,int entry,byte[] currentDestination,byte[] destination,String action){this.current=current;this.entry=entry;this.currentDestination=currentDestination;this.destination=destination;this.action=action;}
            static Selection read(JSONObject snapshot,String action)throws IOException {
                try{
                    if(snapshot==null||!(snapshot.opt("currentIndex") instanceof Integer))throw problem("cdp_history_snapshot_invalid");
                    JSONArray entries=snapshot.getJSONArray("entries");int index=snapshot.getInt("currentIndex"),size=entries.length();
                    if(size>512)throw problem("cdp_history_limit");
                    if(size==0&&index==-1)throw problem("cdp_history_unavailable");
                    if(index<0||index>=size)throw problem("cdp_history_snapshot_invalid");
                    Set<Integer> identifiers=new HashSet<>();
                    for(int i=0;i<size;i++){
                        JSONObject item=entries.getJSONObject(i);Object id=item.opt("id"),url=item.opt("url");
                        if(!(id instanceof Integer)||!identifiers.add((Integer)id)||!(url instanceof String)||((String)url).isEmpty()||((String)url).length()>8192)
                            throw problem("cdp_history_snapshot_invalid");
                    }
                    int selected=index+(action.equals("back")?-1:action.equals("forward")?1:0);
                    if(selected<0||selected>=size)throw problem("cdp_history_unavailable");
                    JSONObject current=entries.getJSONObject(index),target=entries.getJSONObject(selected);
                    String currentUrl=current.getString("url"),targetUrl=target.getString("url");
                    if(!MdoBrowserCdpSession.navigationUrl(currentUrl)||!MdoBrowserCdpSession.navigationUrl(targetUrl))throw problem("cdp_history_destination_unsupported");
                    return new Selection(current.getInt("id"),target.getInt("id"),MdoBrowserCdpSession.navigationDigest(currentUrl),MdoBrowserCdpSession.navigationDigest(targetUrl),action);
                }catch(JSONException invalid){throw problem("cdp_history_snapshot_invalid");}
            }
        }
        static final class Navigation {
            private final Network owner;private final long revision;private MdoBrowserCdpSession.Navigation original;
            private final long requestRevision;private final String requestReference="nr-"+java.util.UUID.randomUUID();
            private final String previousLoader;private final MdoBrowserCdpNetworkFrames.Origin previous,unchanged;private final byte[] destination;
            private final Selection selection;private final long born;private final int timeout;
            private long inspectedRevision=Long.MIN_VALUE;private volatile MdoBrowserCdpNetworkFrames.Traversal proven;
            private byte[] committedDestination;
            private MdoBrowserCdpNetworkFrames.Origin noOp;
            private Navigation(Network owner,long revision,String previousLoader,MdoBrowserCdpNetworkFrames.Origin previous,byte[] destination,int timeout)throws IOException {
                this(owner,revision,previousLoader,previous,destination,null,android.os.SystemClock.elapsedRealtime(),timeout);
            }
            private Navigation(Network owner,long revision,String previousLoader,MdoBrowserCdpNetworkFrames.Origin previous,byte[] destination,Selection selection,long born,int timeout)throws IOException {
                this.owner=owner;this.revision=revision;this.previousLoader=previousLoader;this.previous=previous;this.destination=destination;this.selection=selection;this.born=born;this.timeout=timeout;
                requestRevision=owner.requests.revision();
                unchanged=selection==null?owner.frames.sameLocationOrigin(destination):null;
            }
            String requestReference(){return requestReference;}
            String requestAssociationError(){return owner.requests.failure();}
            MdoBrowserCdpSession.NavigationResult result(){return original==null?null:original.result();}
            boolean pending(){return result()==null;}
            boolean receiptOnly(){return original!=null&&original.receiptOnly;}
            boolean sameDocument(){MdoBrowserCdpNetworkFrames.Traversal completed=proven;return selection!=null?completed!=null&&completed.sameDocument:original!=null&&original.matchesSameDocument(owner.bound.rootFrame);}
            void cancelAttached(){if(owner.attachedSource==null)throw new IllegalStateException("An attached navigation source is required");if(original!=null)original.cancelAttached();}
            private boolean matchesDocument(String frame,String loader){
                if(selection!=null){MdoBrowserCdpNetworkFrames.Traversal completed=proven;return completed!=null&&completed.origin.current()&&owner.bound.rootFrame.equals(frame)&&completed.loader.equals(loader);}
                return sameDocument()?previous.current()&&owner.bound.rootFrame.equals(frame)&&previousLoader.equals(loader):original!=null&&original.matchesCommittedLoader(frame,loader);}
            void checkDocument(MdoBrowserCdpBinding document,MdoBrowserCdpWorker.Deadline deadline)throws IOException {
                owner.active();document.current();
                if(owner.attachedSource!=null&&!owner.attachedSource.ownsDocument(document.peer))throw problem("cdp_navigation_owner_changed");
                if(selection!=null){
                    MdoBrowserCdpNetworkFrames.Traversal completed=proven;if(completed==null)throw problem("cdp_navigation_document_unproven");
                    if(!owner.bound.target.equals(document.target)||!matchesDocument(document.rootFrame,document.rootLoader))throw problem("cdp_navigation_document_changed");
                    Selection current=Selection.read(document.call("Page.getNavigationHistory",new JSONObject(),null,Math.min(5000,deadline.remaining()),false),"reload");
                    try{
                        JSONObject root=frame(document.peer,android.os.SystemClock.elapsedRealtime(),deadline.remaining(),document.lease);
                        if(current.current!=selection.entry||!java.security.MessageDigest.isEqual(current.currentDestination,committedDestination)||!matchesDocument(root.optString("id"),root.optString("loaderId")))throw problem("cdp_navigation_document_changed");
                        byte[] digest=MdoBrowserCdpSession.navigationFrameDigest(root);
                        try{if(!java.security.MessageDigest.isEqual(digest,committedDestination)||root.has("unreachableUrl")&&!"".equals(root.opt("unreachableUrl")))throw problem("cdp_navigation_destination_changed");}
                        finally{Arrays.fill(digest,(byte)0);}
                        owner.active();document.current();if(!completed.origin.current())throw problem("cdp_navigation_document_changed");return;
                    }catch(JSONException invalid){throw problem("cdp_navigation_document_unproven");}
                    finally{Arrays.fill(current.currentDestination,(byte)0);Arrays.fill(current.destination,(byte)0);}
                }
                if(!owner.bound.target.equals(document.target)||!matchesDocument(document.rootFrame,document.rootLoader))
                    throw problem("cdp_navigation_document_changed");
                try{
                    JSONObject root=frame(document.peer,android.os.SystemClock.elapsedRealtime(),deadline.remaining(),document.lease);
                    if(!matchesDocument(root.optString("id"),root.optString("loaderId")))throw problem("cdp_navigation_document_changed");
                    if(sameDocument()){
                        byte[] digest=MdoBrowserCdpSession.navigationFrameDigest(root);
                        try{if(!java.security.MessageDigest.isEqual(destination,digest))throw problem("cdp_navigation_destination_changed");}
                        finally{java.util.Arrays.fill(digest,(byte)0);}
                    }
                }catch(JSONException invalid){throw problem("cdp_navigation_document_unproven");}
                owner.active();document.current();
            }
        }
        private final MdoBrowserCdpBinding bound;private final MdoBrowserCdpNetworkFrames frames;private final MdoBrowserCdpNetworkRequests requests;private final MdoBrowserCdpPopupSource attachedSource;private boolean enableAttempted,responsesAttempted,enabled,retiring,disableAttempted,responsesDisabled;
        private Network(MdoBrowserCdpBinding bound)throws IOException {
            this.bound=bound;attachedSource=null;frames=new MdoBrowserCdpNetworkFrames(bound.rootFrame,bound.rootLoader);requests=new MdoBrowserCdpNetworkRequests(bound.rootFrame);
            final MdoBrowserCdpNetworkFrames ancestry=frames;final MdoBrowserCdpNetworkRequests lineage=requests;
            bound.observe((method,child,params)->{ancestry.event(method,child,params);lineage.event(method,child,params);});bound.lease.transfer(bound,this);
        }
        /** Navigation-only view of the original proved physical source. Its
         * existing ledgers, response guard and lease ownership are retained;
         * never enable/disable Fetch or promote a document binding. */
        private Network(MdoBrowserCdpBinding bound,MdoBrowserCdpPopupSource source)throws IOException {
            this.bound=bound;attachedSource=source;frames=source.navigationFrames();requests=source.navigationRequests();enabled=true;
        }
        private void ordinary()throws IOException {if(attachedSource!=null)throw problem("cdp_network_scope_invalid");}
        void enableResponses(int timeoutMs)throws IOException {
            ordinary();
            bound.current();if(enableAttempted)throw problem("cdp_network_enable_replayed");
            if(timeoutMs<10||timeoutMs>5000)throw problem("cdp_arguments_invalid");
            enableAttempted=true;
            try{
                long started=android.os.SystemClock.elapsedRealtime();
                bound.call("Page.enable",new JSONObject(),null,remaining(started,timeoutMs,bound.lease),false);
                boolean proved=false;
                for(int attempt=0;attempt<8;attempt++){
                    long before=frames.revision();JSONObject tree=bound.call("Page.getFrameTree",new JSONObject(),null,remaining(started,timeoutMs,bound.lease),false).getJSONObject("frameTree");
                    if(frames.revision()!=before)continue;frames.initialize(tree,before);proved=true;break;
                }
                if(!proved)throw problem("download_frame_bootstrap_changed");
                bound.call("Network.enable",new JSONObject().put("maxTotalBufferSize",0).put("maxResourceBufferSize",0).put("maxPostDataSize",0),null,remaining(started,timeoutMs,bound.lease),false);
                JSONObject pattern=new JSONObject().put("urlPattern","*").put("requestStage","Response");
                JSONObject params=new JSONObject().put("patterns",new JSONArray().put(pattern)).put("handleAuthRequests",false);
                int remaining=remaining(started,timeoutMs,bound.lease);responsesAttempted=true;
                bound.call("Fetch.enable",params,null,remaining,true);bound.current();enabled=true;
            }catch(JSONException failure){close();throw problem("cdp_network_arguments_invalid");}
            catch(IOException|RuntimeException|Error failure){close();throw failure;}
        }
        boolean poll(int timeoutMs)throws IOException {active();return bound.poll(timeoutMs);}
        boolean poll(int receiveMs,int timeoutMs)throws IOException {active();return bound.peer.poll(receiveMs,timeoutMs);}
        boolean idle()throws IOException {active();return bound.peer.idle();}
        private void active()throws IOException {bound.current();if(!enabled)throw problem("cdp_network_not_enabled");if(attachedSource!=null)attachedSource.navigationCurrent();}
        private void control()throws IOException {bound.current();if(!responsesAttempted)throw problem("cdp_network_not_enabled");}
        MdoBrowserCdpSession.ControlReceipt continuePausedRequest()throws IOException {control();return bound.peer.continuePausedRequest();}
        MdoBrowserCdpSession.ControlReceipt abortPausedRequest()throws IOException {control();return bound.peer.abortPausedRequest();}
        MdoBrowserCdpResponse pausedResponse()throws IOException {control();return bound.peer.pausedResponse();}
        MdoBrowserCdpNetworkFrames.Origin pausedOrigin()throws IOException {control();return frames.origin(bound.peer.pausedResponse().frame);}
        MdoBrowserCdpSession.Offer retainPausedResponse()throws IOException {
            control();if(retiring)throw problem("cdp_network_retiring");MdoBrowserCdpNetworkFrames.Origin source=pausedOrigin();MdoBrowserCdpSession.Offer offer=bound.peer.retainPausedResponse();
            try{
                MdoBrowserCdpNetworkRequests.Link link=requests.link(offer.networkIdentity(),offer.metadata().frame);offer.associateRequest(link);String kind="response_frame";
                if(link!=null)try{MdoBrowserCdpNetworkFrames.Origin original=link.epoch();if(original!=null){source=original;kind="navigation_candidates";}}
                    catch(IOException ambiguous){if(!"download_frame_unproven".equals(ambiguous.getMessage()))throw ambiguous;source=MdoBrowserCdpNetworkFrames.unproven(source);kind="unproven";}
                else if(requests.failure()!=null&&bound.rootFrame.equals(offer.metadata().frame)){source=MdoBrowserCdpNetworkFrames.unproven(source);kind="unproven";}
                offer.bindSourceEpoch(source,kind);return offer;
            }
            catch(IOException|RuntimeException|Error failure){offer.cancel();offer.retireRequestAssociation();throw failure;}
        }
        int pendingOffers()throws IOException {bound.current();return bound.peer.pendingOffers();}
        MdoBrowserCdpSession.Body takePausedBody(long maxBytes,int timeoutMs)throws IOException {control();if(retiring)throw problem("cdp_network_retiring");pausedOrigin();return bound.peer.takePausedBody(maxBytes,timeoutMs);}
        MdoBrowserCdpSession.BodyChunk readBody(MdoBrowserCdpSession.Body body,int timeoutMs)throws IOException {active();return bound.peer.readBody(body,timeoutMs);}
        void cancelBody(MdoBrowserCdpSession.Body body,int timeoutMs)throws IOException {active();bound.peer.cancelBody(body,timeoutMs);}
        int pendingBodies()throws IOException {bound.current();return bound.peer.pendingBodies();}
        void requestBodyRead(MdoBrowserCdpSession.Body body)throws IOException {active();if(retiring)throw problem("cdp_network_retiring");bound.peer.requestBodyRead(body);}
        MdoBrowserCdpSession.BodyChunk takeBodyChunk(MdoBrowserCdpSession.Body body)throws IOException {active();return bound.peer.takeBodyChunk(body);}
        boolean serviceBodies(int sendTimeoutMs)throws IOException {active();return bound.peer.serviceBodies(sendTimeoutMs);}
        int pendingControls()throws IOException {bound.current();return bound.peer.pendingControls();}
        Navigation navigateRoot(String url,int timeoutMs)throws IOException {
            active();if(retiring)throw problem("cdp_network_retiring");frames.origin(bound.rootFrame);
            Navigation ticket=new Navigation(this,frames.revision(),frames.rootLoader(),frames.origin(bound.rootFrame),MdoBrowserCdpSession.navigationDigest(url),timeoutMs);
            ticket.original=bound.peer.navigateRoot(url,bound.rootFrame,timeoutMs);requests.admit(ticket.requestRevision,ticket.original,ticket.requestReference,ticket.previous);return ticket;
        }
        Navigation traverseRoot(String action,int timeoutMs)throws IOException {
            active();if(retiring)throw problem("cdp_network_retiring");
            if(!Arrays.asList("back","forward","reload").contains(action)||timeoutMs<100||timeoutMs>600000)
                throw new MdoBrowserCdpSession.Failure("cdp_navigation_arguments_invalid",0,false);
            Navigation ticket;int budget;long started=android.os.SystemClock.elapsedRealtime();
            try{
                MdoBrowserCdpNetworkFrames.Origin previous=frames.origin(bound.rootFrame);String loader=frames.rootLoader();
                Selection selected=Selection.read(bound.call("Page.getNavigationHistory",new JSONObject(),null,remaining(started,timeoutMs,bound.lease),false),action);
                JSONObject root=frame(bound.peer,started,timeoutMs,bound.lease);
                if(!previous.current()||!bound.rootFrame.equals(root.opt("id"))||!loader.equals(root.opt("loaderId")))throw problem("cdp_history_snapshot_changed");
                byte[] current=MdoBrowserCdpSession.navigationFrameDigest(root);
                try{if(!java.security.MessageDigest.isEqual(current,selected.currentDestination))throw problem("cdp_history_snapshot_changed");}
                finally{Arrays.fill(current,(byte)0);}
                bound.current();long used=android.os.SystemClock.elapsedRealtime()-started;
                if(used<0||used>timeoutMs-100)throw problem("cdp_navigation_timeout");budget=(int)(timeoutMs-used);
                ticket=new Navigation(this,frames.revision(),loader,previous,selected.destination,selected,started,timeoutMs);
            }catch(MdoBrowserCdpSession.Failure failure){throw new MdoBrowserCdpSession.Failure(failure.getMessage(),failure.protocolCode,false);}
            catch(IOException failure){throw new MdoBrowserCdpSession.Failure("cdp_binding_timeout".equals(failure.getMessage())?"cdp_navigation_timeout":failure.getMessage(),0,false);}
            catch(JSONException invalid){throw new MdoBrowserCdpSession.Failure("cdp_history_snapshot_invalid",0,false);}
            ticket.original=action.equals("reload")?bound.peer.reloadRoot(bound.rootFrame,ticket.previousLoader,budget):bound.peer.historyRoot(bound.rootFrame,ticket.selection.entry,budget);
            return ticket;
        }
        MdoBrowserCdpNetworkFrames.Origin navigationOrigin(Navigation ticket)throws IOException {
            bound.current();if(ticket==null||ticket.owner!=this)throw problem("cdp_navigation_owner_changed");
            if(ticket.selection!=null){
                if(ticket.proven!=null)return ticket.proven.origin;
                MdoBrowserCdpNetworkFrames.Traversal candidate=frames.traversal(ticket.original,ticket.revision,ticket.selection.action,ticket.previousLoader,ticket.previous,ticket.destination);
                if(candidate==null)return null;
                long revision=frames.revision(),used=android.os.SystemClock.elapsedRealtime()-ticket.born;
                if(ticket.inspectedRevision==revision||used<0||used>ticket.timeout-10)return null;ticket.inspectedRevision=revision;
                try{
                    Selection current=Selection.read(bound.call("Page.getNavigationHistory",new JSONObject(),null,remaining(ticket.born,ticket.timeout,bound.lease),false),"reload");
                    try{
                        JSONObject root=frame(bound.peer,ticket.born,ticket.timeout,bound.lease);byte[] digest=MdoBrowserCdpSession.navigationFrameDigest(root);
                        try{
                            if(current.current!=ticket.selection.entry||!bound.rootFrame.equals(root.opt("id"))||!candidate.loader.equals(root.opt("loaderId"))||
                               !java.security.MessageDigest.isEqual(digest,current.currentDestination)||root.has("unreachableUrl")&&!"".equals(root.opt("unreachableUrl"))||
                               revision!=frames.revision()||!candidate.origin.current())return null;
                            bound.current();ticket.committedDestination=Arrays.copyOf(digest,digest.length);ticket.proven=candidate;return candidate.origin;
                        }finally{Arrays.fill(digest,(byte)0);}
                    }finally{Arrays.fill(current.currentDestination,(byte)0);Arrays.fill(current.destination,(byte)0);}
                }catch(JSONException invalid){throw problem("cdp_history_snapshot_invalid");}
                catch(IOException failure){if("cdp_binding_timeout".equals(failure.getMessage())){bound.current();return null;}throw failure;}
            }
            if(!ticket.sameDocument())return frames.navigationOrigin(ticket.original,ticket.revision);
            if(ticket.noOp!=null)return ticket.noOp;
            MdoBrowserCdpNetworkFrames.Origin event=frames.sameDocumentOrigin(ticket.original,ticket.revision,ticket.previousLoader,ticket.destination,ticket.previous);
            if(event!=null)return event;
            if(ticket.unchanged==null||!ticket.unchanged.current()||ticket.inspectedRevision!=Long.MIN_VALUE)return null;
            ticket.inspectedRevision=frames.revision();
            try{
                JSONObject root=frame(bound.peer,ticket.born,ticket.timeout,bound.lease);
                if(!bound.rootFrame.equals(root.opt("id"))||!ticket.previousLoader.equals(root.opt("loaderId"))||root.has("unreachableUrl")&&!"".equals(root.opt("unreachableUrl")))return null;
                byte[] digest=MdoBrowserCdpSession.navigationFrameDigest(root);
                try{if(java.security.MessageDigest.isEqual(digest,ticket.destination)&&ticket.unchanged.current()){bound.current();ticket.noOp=ticket.unchanged;}}
                finally{Arrays.fill(digest,(byte)0);}
                return ticket.noOp;
            }catch(JSONException invalid){return null;}
            catch(IOException failure){if(Arrays.asList("cdp_binding_timeout","cdp_navigation_document_unproven","cdp_navigation_destination_invalid").contains(failure.getMessage())){bound.current();return null;}throw failure;}
        }
        boolean responsesMayHaveOccurred(){return responsesAttempted;}
        void cancelTransfers()throws IOException {ordinary();active();retiring=true;bound.peer.cancelTransfers();}
        boolean transfersDrained()throws IOException {active();return bound.peer.pendingOffers()==0&&bound.peer.pendingBodies()==0&&bound.peer.pendingControls()==0;}
        void disableResponses(int timeoutMs)throws IOException {
            ordinary();
            active();if(timeoutMs<10||timeoutMs>5000)throw problem("cdp_arguments_invalid");
            if(disableAttempted)throw problem("cdp_network_disable_replayed");
            if(!transfersDrained())throw problem("cdp_network_transfers_pending");disableAttempted=true;
            bound.call("Fetch.disable",new JSONObject(),null,timeoutMs,true);bound.current();responsesDisabled=true;enabled=false;
        }
        void drainDisabledControls(int receiveMs,int timeoutMs)throws IOException {
            ordinary();
            bound.current();if(!responsesDisabled)throw problem("cdp_network_disable_unconfirmed");bound.peer.poll(receiveMs,timeoutMs);
        }
        boolean responsesDisabled(){return responsesDisabled;}
        int cleanupState(){if(!bound.closed.get())return 0;if(!bound.closeFinished.get())return 1;if(bound.cleanupFailed.get())return 3;return bound.peer.closeState();}
        boolean cleanupConfirmed(){return cleanupState()==2;}
        boolean cleanupPending(){return cleanupState()==1;}
        @Override public void close(){if(attachedSource!=null)throw new IllegalStateException("Navigation view does not own the physical source");frames.close();requests.close();bound.lease.untrack(this);bound.close();}
    }
    private static Context match(Peer peer,Contexts contexts,MdoBrowserCdpMarker.Ticket ticket,long started,int timeout,MdoBrowserCdpMarker.Lease lease) throws IOException,JSONException {
        peer.call("Runtime.enable",new JSONObject(),remaining(started,timeout,lease));
        JSONObject snapshot=frame(peer,started,timeout,lease);Object frame=snapshot.get("id"),loader=snapshot.get("loaderId");
        if(!identity(frame)||!(loader instanceof String)||((String)loader).length()>128)throw problem("cdp_binding_context_invalid");
        Context root=contexts.root((String)frame);root.loader=(String)loader;
        JSONObject names=peer.call("Runtime.globalLexicalScopeNames",new JSONObject().put("executionContextId",root.id),remaining(started,timeout,lease));
        JSONArray entries=names.getJSONArray("names");if(entries.length()>8192)throw problem("cdp_binding_name_limit");
        int matches=0;
        for(int i=0;i<entries.length();i++){Object name=entries.get(i);if(!(name instanceof String))throw problem("cdp_binding_names_invalid");if(ticket.name.equals(name))matches++;}
        if(matches>1)throw problem("cdp_binding_names_invalid");return matches==1?root:null;
    }
    private static JSONObject frame(Peer peer,long started,int timeout,MdoBrowserCdpMarker.Lease lease)throws IOException,JSONException {
        return peer.call("Page.getFrameTree",new JSONObject(),remaining(started,timeout,lease)).getJSONObject("frameTree").getJSONObject("frame");
    }
    private static JSONObject evaluation(String expression,String unique,boolean readOnly,int timeoutMs) throws JSONException {
        return new JSONObject().put("expression",expression).put("uniqueContextId",unique).put("returnByValue",true)
            .put("silent",true).put("throwOnSideEffect",readOnly).put("timeout",timeoutMs);
    }
    private static int remaining(long start,int timeout,MdoBrowserCdpMarker.Lease lease) throws IOException {
        if(!lease.current())throw problem("cdp_binding_owner_changed");long used=android.os.SystemClock.elapsedRealtime()-start;
        if(used<0||used>timeout-10)throw problem("cdp_binding_timeout");return (int)Math.min(5000,timeout-used);
    }
    JSONObject call(String method,JSONObject params,int timeoutMs) throws IOException {
        current();
        return peer.call(method,params,timeoutMs);
    }
    void current()throws IOException {
        if(Thread.currentThread()!=owner)throw problem("cdp_wrong_worker");
        if(closed.get()||!lease.current()||contexts.dirty){
            boolean retired=closed.get()||!lease.current();close();
            if(retired&&peer instanceof MdoBrowserCdpPopupSource)((MdoBrowserCdpPopupSource)peer).bindingClosed();
            throw problem("cdp_binding_owner_changed");
        }
    }
    JSONObject call(String method,JSONObject params,String child,int timeoutMs,boolean effect)throws IOException {
        current();return peer.call(method,params,child,timeoutMs,effect);
    }
    void admitChild(String child)throws IOException {current();peer.admitChild(child);}
    void retireChild(String child)throws IOException {current();peer.retireChild(child);}
    boolean poll(int timeoutMs)throws IOException {current();return peer.poll(timeoutMs);}
    boolean reserveWorldName()throws IOException {current();return lease.worldName();}
    Map<Integer,String> contextIdentities()throws IOException {current();return new HashMap<>(contexts.identities);}
    void observe(MdoBrowserCdpSession.Events controller)throws IOException {
        current();if(controller==null||controllerInstalled)throw problem("cdp_controller_busy");controllerInstalled=true;
        MdoBrowserCdpSession.Events original=contexts.forward;
        contexts.forward=(method,session,params)->{if(original!=null)original.event(method,session,params);controller.event(method,session,params);};
    }
    MdoBrowserCdpFrames frames()throws IOException {current();if(peer instanceof MdoBrowserCdpPopupSource)throw problem("cdp_network_scope_invalid");if(frames==null)frames=new MdoBrowserCdpFrames(this);return frames;}
    boolean responseReady(String raw)throws IOException{current();return frames!=null&&frames.responseReady(rootFrame)&&frames.responseReady(raw);}
    MdoBrowserCdpSession.AttachedResponse nextAttachedResponse()throws IOException {
        current();if(peer instanceof MdoBrowserCdpPopupSource)return ((MdoBrowserCdpPopupSource)peer).next();if(!(peer instanceof MdoBrowserCdpPopupBroker.DocumentPeer))throw problem("cdp_attached_binding_required");return ((MdoBrowserCdpPopupBroker.DocumentPeer)peer).nextResponse();
    }
    public MdoBrowserCdpSession.AttachedResponse next()throws IOException{return nextAttachedResponse();}
    public MdoBrowserCdpSession.Offer retain(MdoBrowserCdpSession.AttachedResponse response)throws IOException {
        current();if(peer instanceof MdoBrowserCdpPopupSource)return ((MdoBrowserCdpPopupSource)peer).retain(response);MdoBrowserCdpNetworkFrames.Origin origin=responseOrigin(response.metadata().frame);MdoBrowserCdpSession.Offer offer=response.offer();offer.bindSourceEpoch(origin,"response_frame");return offer;
    }
    MdoBrowserCdpNetworkFrames.Origin responseOrigin(String raw)throws IOException {current();if(frames==null)throw problem("download_frame_unproven");return frames.responseOrigin(raw);}
    MdoBrowserCdpNetworkFrames.Origin issueResponseOrigin(String frame,String document,boolean root,java.util.concurrent.atomic.AtomicBoolean[] scopes)throws IOException {
        current();if(!(peer instanceof MdoBrowserCdpPopupBroker.DocumentPeer))throw problem("cdp_attached_binding_required");return MdoBrowserCdpNetworkFrames.attached(frame,document,root,lease,scopes);
    }
    void attachDownloads(MdoBrowserCdpDownloads.Connection downloads)throws IOException {
        current();if(peer instanceof MdoBrowserCdpPopupSource){((MdoBrowserCdpPopupSource)peer).downloads(downloads);return;}if(!(peer instanceof MdoBrowserCdpPopupBroker.DocumentPeer))throw problem("cdp_attached_binding_required");((MdoBrowserCdpPopupBroker.DocumentPeer)peer).downloads(downloads);
    }
    Network sourceNavigation()throws IOException {
        current();if(!lease.physicalNetwork()||!(peer instanceof MdoBrowserCdpPopupSource))throw problem("cdp_popup_navigation_source_required");
        MdoBrowserCdpPopupSource original=(MdoBrowserCdpPopupSource)peer;original.navigationCurrent();
        if(sourceNavigation==null)sourceNavigation=new Network(this,original);return sourceNavigation;
    }
    void startSourceNavigation(MdoBrowserCdpNetworks.Navigation navigation)throws IOException {
        Network view=sourceNavigation();((MdoBrowserCdpPopupSource)peer).startNavigation(view,navigation);
    }
    /** Data-only fence on the original physical root, never a navigation ACK
     * or ownership of a document binding/session/UI graph. */
    static final class CurrentDocument {
        private final Object owner;private final MdoBrowserCdpNetworkFrames.Origin origin;private final String loader;
        private CurrentDocument(Object owner,MdoBrowserCdpNetworkFrames.Origin origin,String loader){this.owner=owner;this.origin=origin;this.loader=loader;}
        boolean current(){return origin.current();}
    }
    CurrentDocument currentDocument(MdoBrowserCdpBinding document,MdoBrowserCdpWorker.Deadline deadline,CurrentDocument previous)throws IOException {
        current();document.current();if(!(peer instanceof MdoBrowserCdpPopupSource))throw problem("cdp_popup_navigation_source_required");
        MdoBrowserCdpPopupSource source=(MdoBrowserCdpPopupSource)peer;
        if(!source.ownsDocument(document.peer)||!target.equals(document.target)||!rootFrame.equals(document.rootFrame))throw problem("cdp_navigation_owner_changed");
        MdoBrowserCdpNetworkFrames original=source.navigationFrames();
        CurrentDocument selected=previous;
        if(selected==null)try{selected=new CurrentDocument(readAuthority,original.documentOrigin(),original.rootLoader());}
        catch(IOException unavailable){
            // Preserve a specific denial through the worker's bounded CDP
            // error vocabulary; this does not grant recovery authority.
            if("download_frame_unproven".equals(unavailable.getMessage())||"download_frame_parent_unproven".equals(unavailable.getMessage()))throw problem("cdp_navigation_document_unproven");throw unavailable;
        }
        if(selected.owner!=readAuthority||!selected.current()||!selected.loader.equals(original.rootLoader())||!selected.loader.equals(document.rootLoader))throw problem("cdp_navigation_document_changed");
        try{
            JSONObject root=frame(document.peer,android.os.SystemClock.elapsedRealtime(),deadline.remaining(),document.lease);
            if(!rootFrame.equals(root.opt("id"))||!selected.loader.equals(root.opt("loaderId"))||root.has("unreachableUrl")&&!"".equals(root.opt("unreachableUrl")))throw problem("cdp_navigation_document_changed");
        }catch(JSONException invalid){throw problem("cdp_navigation_document_unproven");}
        current();document.current();source.navigationCurrent();if(!selected.current())throw problem("cdp_navigation_document_changed");return selected;
    }
    /** Data-only UI snapshot. Rebinding still needs a fresh original-source
     * document guard on the worker; this is never ownership authority. */
    boolean documentUnavailable(){MdoBrowserCdpFrames graph=frames;return peer instanceof MdoBrowserCdpPopupBroker.DocumentPeer&&(closed.get()||contexts.dirty||graph!=null&&graph.documentUnavailable());}
    boolean matchesSource(MdoBrowserCdpPopupNative.Source proof){return !closed.get()&&peer instanceof MdoBrowserCdpPopupSource&&((MdoBrowserCdpPopupSource)peer).matches(proof);}
    void retireResponseOrigins(){if(frames!=null)frames.retire();}
    @Override public void close(){if(closed.compareAndSet(false,true)){
        Throwable problem=null;
        try{contexts.forward=null;contexts.dirty=true;if(frames!=null)frames.retire();lease.untrack(this);lease.untrack(peer);}catch(RuntimeException|Error failure){cleanupFailed.set(true);problem=failure;}
        try{peer.close();}catch(RuntimeException|Error failure){cleanupFailed.set(true);if(problem==null)problem=failure;}finally{closeFinished.set(true);}
        if(problem instanceof RuntimeException)throw (RuntimeException)problem;if(problem instanceof Error)throw (Error)problem;
    }}
}
