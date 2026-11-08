package org.xserver.android;

import java.io.IOException;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.HashMap;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.BooleanSupplier;
import java.util.function.LongSupplier;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

/** One process-wide browser attachment controller on the existing CDP worker.
 * Every waiting page is captured before trusting any opener metadata. A frozen
 * set of possible registrations is not native child/transport ownership proof.
 * The fixed Fetch guard blocks requests without granting native-child proof.
 * Only fixed resume/request flow is available for handed-off native children;
 * no body/page data, document/world admission or Agent execution is exposed.
 * The host must service turns and complete the original native adoption join. */
final class MdoBrowserCdpPopupBroker implements AutoCloseable {
    interface Connector {MdoBrowserCdpSession open(int timeout,BooleanSupplier current,MdoBrowserCdpSession.Events events)throws IOException;}
    private static final Object UNCERTAIN=new Object();
    private static final AtomicReference<Object> PROCESS=new AtomicReference<>();
    static boolean creationBlocked(){return PROCESS.get()!=null;}
    private static final int NEW=0,STARTING=1,ARMED=2,STOPPING=3,STOPPED=4,FAILED=5;
    private static final Connector NATIVE=(timeout,current,events)->{
        long born=android.os.SystemClock.elapsedRealtime();MdoBrowserCdpNative.BrowserEndpoint endpoint=MdoBrowserCdpNative.browserEndpoint(timeout);
        long used=android.os.SystemClock.elapsedRealtime()-born;
        if(used<0||used>timeout-10)throw new IOException("cdp_popup_broker_timeout");
        return MdoBrowserCdpSession.openBrowser(endpoint,timeout-(int)used,current,events);
    };
    static final class Registration implements AutoCloseable {
        private final MdoBrowserCdpBinding.Network.PopupSource source;private final String target;
        private final Handoff handoff;private final MdoBrowserCdpPopupNative.Source attached;
        private final AtomicBoolean active=new AtomicBoolean(true);
        private Registration(MdoBrowserCdpBinding.Network.PopupSource source,String target){this.source=source;this.target=target;handoff=null;attached=null;}
        private Registration(Handoff handoff,MdoBrowserCdpPopupNative.Source attached,String target){source=null;this.handoff=handoff;this.attached=attached;this.target=target;}
        boolean current(){return active.get()&&(source!=null?source.current():handoff.current()&&handoff.owns(attached)&&attached.current());}
        boolean owns(MdoBrowserCdpMarker.Lease lease){return current()&&(source!=null?source.owns(lease):attached.lease==lease);}
        @Override public void close(){active.set(false);}
    }
    /** Single-use native/UI reservation for this exact already protected
     * Browser attachment. Only opaque identity, clock/stop data and the native
     * ownership receipt cross threads; no broker, session or parent is rooted.
     * The debugger/requests stay paused until explicit activation. Response
     * retention is opaque; this grants no document/world or body access. */
    static final class Handoff implements AutoCloseable {
        private final Object proof;private final AtomicBoolean brokerClosed;
        private final long born;private final LongSupplier clock;
        private final AtomicBoolean cancelled=new AtomicBoolean(),committed=new AtomicBoolean();
        private final AtomicReference<Long> closeBorn=new AtomicReference<>();
        private final AtomicBoolean detached=new AtomicBoolean();
        private final AtomicReference<MdoBrowserCdpPopupNative.Promotion> nativeOwner=new AtomicReference<>();
        private final AtomicBoolean activationRequested=new AtomicBoolean();
        private final AtomicReference<MdoBrowserCdpSession.ControlResult> activationResult=new AtomicReference<>(),flowFailure=new AtomicReference<>();
        private final AtomicInteger continuedRequests=new AtomicInteger();
        private final AtomicBoolean networkAdmitted=new AtomicBoolean();
        private final AtomicBoolean humanDetachRequested=new AtomicBoolean();
        private final AtomicReference<MdoBrowserCdpSession.ControlResult> humanDetachResult=new AtomicReference<>();
        private Handoff(MdoBrowserCdpPopupNative.Child child,AtomicBoolean closed,long born,LongSupplier clock){proof=child.proofIdentity();brokerClosed=closed;this.born=born;this.clock=clock;}
        private boolean eligible(){try{long age=clock.getAsLong()-born;return !cancelled.get()&&!brokerClosed.get()&&age>=0&&(age<15000||networkAdmitted.get());}catch(RuntimeException|Error error){return false;}}
        void reserve(MdoBrowserCdpPopupNative.Child child,MdoBrowserCdpPopupNative.Promotion nativeReceipt)throws IOException {
            if(!eligible()||child==null||!child.matchesProof(proof)||!child.current()||child.proof()==null||!child.proof().ok||nativeReceipt==null||!nativeReceipt.matches(child)||!nativeOwner.compareAndSet(null,nativeReceipt))throw problem("cdp_popup_handoff_unavailable");
            if(!eligible()){close();throw problem("cdp_popup_handoff_unavailable");}
        }
        void commit(MdoBrowserCdpPopupNative.Promotion original)throws IOException {
            if(nativeOwner.get()!=original||!committed.compareAndSet(false,true)){close();throw problem("cdp_popup_handoff_unavailable");}
            if(!current()){close();throw problem("cdp_popup_handoff_unavailable");}
        }
        boolean transferred(){MdoBrowserCdpPopupNative.Promotion original=nativeOwner.get();return committed.get()||original!=null&&original.current();}
        boolean current(){MdoBrowserCdpPopupNative.Promotion original=nativeOwner.get();return eligible()&&original!=null&&original.current();}
        /** Deliberate closure revokes authority immediately, but the original
         * attachment must still receive its native detach on this same wire. */
        boolean retiring(){return committed.get()&&cancelled.get();}
        boolean detached(){return detached.get();}
        boolean draining(){try{return retiring()&&!brokerClosed.get()&&retirementCurrent(clock.getAsLong());}catch(RuntimeException|Error error){return false;}}
        private boolean retirementCurrent(long now){Long born=closeBorn.get();MdoBrowserCdpPopupNative.Promotion original=nativeOwner.get();
            return born!=null&&now-born>=0&&now-born<5000&&original!=null&&!original.failed();}
        boolean owns(MdoBrowserCdpPopupNative.Promotion original){return original!=null&&nativeOwner.get()==original;}
        void takeControl(MdoBrowserCdpPopupNative.Promotion original)throws IOException {
            if(!committed.get()||!owns(original))throw problem("cdp_popup_human_owner_invalid");original.takeControl(this);
        }
        MdoBrowserCdpPopupNative.HumanControl prepareControl(MdoBrowserCdpPopupNative.Promotion original,android.webkit.WebView exact)throws IOException {
            if(!committed.get()||!owns(original))throw problem("cdp_popup_human_owner_invalid");return original.prepareControl(this,exact);
        }
        /** Request only the original human-owned attachment's retirement.
         * Source/body/descendant readiness is independently checked by its
         * existing worker, never asserted by a UI boolean or target string. */
        void detachForHuman(MdoBrowserCdpPopupNative.Promotion original)throws IOException {
            if(!owns(original)||!original.humanTransferred()||!draining())throw problem("cdp_popup_human_owner_invalid");
            if(!humanDetachRequested.compareAndSet(false,true))throw problem("cdp_popup_human_detach_replayed");
        }
        MdoBrowserCdpSession.ControlResult humanDetachResult(){return humanDetachResult.get();}
        boolean humanDetached(){MdoBrowserCdpSession.ControlResult receipt=humanDetachResult.get();return humanDetachRequested.get()&&receipt!=null&&receipt.ok&&detached.get();}
        boolean owns(MdoBrowserCdpPopupNative.Document original){return original!=null&&original.matches(nativeOwner.get());}
        boolean owns(MdoBrowserCdpPopupNative.Source original){return original!=null&&original.matches(nativeOwner.get());}
        void admitNetwork(){networkAdmitted.set(true);}
        void activate()throws IOException{if(!committed.get()||!current())throw problem("cdp_popup_activation_unavailable");if(!activationRequested.compareAndSet(false,true))throw problem("cdp_popup_activation_replayed");}
        MdoBrowserCdpSession.ControlResult activationResult(){return activationResult.get();}
        MdoBrowserCdpSession.ControlResult flowFailure(){return flowFailure.get();}
        void sourceFailure(Throwable error){
            String code=error instanceof IOException?safe(error.getMessage()):"cdp_worker_failed";
            int protocol=error instanceof MdoBrowserCdpSession.Failure?((MdoBrowserCdpSession.Failure)error).protocolCode:0;
            boolean effect=error instanceof MdoBrowserCdpSession.Failure&&((MdoBrowserCdpSession.Failure)error).effectMayHaveOccurred;
            flowFailure.compareAndSet(null,new MdoBrowserCdpSession.ControlResult(false,code,protocol,effect));
        }
        int continuedRequests(){return continuedRequests.get();}
        /** Revoke data authority before native destruction invalidates its UI
         * proof. This first half must not notify/reenter a native disposer. */
        void retireAuthority(){if(!cancelled.get()){
                try{closeBorn.compareAndSet(null,clock.getAsLong());}catch(RuntimeException|Error ignored){/* absent clock fails retirement closed */}
                cancelled.set(true);
            }}
        @Override public void close(){retireAuthority();
            MdoBrowserCdpPopupNative.Promotion original=nativeOwner.get();if(original!=null)original.close();}
    }
    static final class Candidate {
        private static final Registration[] NO_OPENERS=new Registration[0];
        final String id="popup-candidate-"+UUID.randomUUID();
        private volatile Registration[] possibleOpeners;private volatile Registration provenOpener;
        private final MdoBrowserCdpSession.AttachedTarget original;private final long born;private final AtomicBoolean brokerClosed;
        private volatile boolean retired;private boolean rootDetached;
        private final AtomicBoolean protectionRequested=new AtomicBoolean();private final AtomicReference<MdoBrowserCdpSession.ControlResult> protection=new AtomicReference<>();
        private volatile boolean protectionStarted;
        private volatile MdoBrowserCdpBinding.Network nativeBound;private volatile MdoBrowserCdpPopupNative.Child nativeChild;
        private volatile Handoff handoff;
        private volatile DocumentPeer documentPeer;
        private volatile MdoBrowserCdpPopupSource sourcePeer;private String sourceFrame,sourceLoader;
        private MdoBrowserCdpDownloads.Connection downloads;
        private Candidate(Registration[] possibleOpeners,MdoBrowserCdpSession.AttachedTarget original,long born,AtomicBoolean brokerClosed){this.possibleOpeners=possibleOpeners;this.original=original;this.born=born;this.brokerClosed=brokerClosed;}
        boolean current(){
            if(retired||brokerClosed.get())return false;Handoff transfer=handoff;if(transfer!=null&&transfer.transferred())return transfer.current();
            Registration proven=provenOpener;if(proven!=null){boolean original=proven.current();return original||transfer!=null&&transfer.current();}
            Registration[] possible=possibleOpeners;for(Registration original:possible)if(!original.current())return transfer!=null&&transfer.current();return possible.length!=0||transfer!=null&&transfer.current();
        }
        /** Before physical proof this means only "cannot yet exclude". New
         * registrations never acquire an already captured waiting target. */
        boolean mayBelongTo(Registration original){Handoff transfer=handoff;if(transfer!=null&&transfer.transferred())return false;Registration proven=provenOpener;if(proven!=null)return proven==original;
            for(Registration possible:possibleOpeners)if(possible==original)return true;return false;}
        boolean unbound(){return nativeBound==null;}
        boolean nativeChildProven(){MdoBrowserCdpBinding.Network bound=nativeBound;MdoBrowserCdpPopupNative.Child child=nativeChild;
            Handoff transfer=handoff;return current()&&(transfer!=null&&transfer.current()||bound!=null&&child!=null&&child.current()&&bound.physicalCurrent());}
        boolean requestProtection()throws IOException{if(!current())throw problem("cdp_popup_candidate_retired");return protection.get()==null&&protectionRequested.compareAndSet(false,true);}
        boolean protectionRequested(){return protectionRequested.get();}
        MdoBrowserCdpSession.ControlResult protection(){return protection.get();}
        int retainedRequestCount(){return original.retainedRequestCount();}
        int retainedResponseCount(){return original.retainedResponseCount();}
        private void reconcileHandoff(){Handoff transfer=handoff;if(transfer!=null&&transfer.transferred()){possibleOpeners=NO_OPENERS;provenOpener=null;MdoBrowserCdpBinding.Network bound=nativeBound;nativeBound=null;nativeChild=null;if(bound!=null)bound.close();}}
        private void retired(){retired=true;MdoBrowserCdpPopupSource source=sourcePeer;sourcePeer=null;if(source!=null)source.close();sourceFrame=sourceLoader=null;MdoBrowserCdpDownloads.Connection transfers=downloads;downloads=null;if(transfers!=null)transfers.disconnect();DocumentPeer document=documentPeer;documentPeer=null;if(document!=null)document.retire();Handoff transfer=handoff;if(transfer!=null)transfer.close();possibleOpeners=NO_OPENERS;provenOpener=null;if(protectionRequested.get())protection.compareAndSet(null,new MdoBrowserCdpSession.ControlResult(false,"cdp_popup_candidate_retired",0,protectionStarted));
            MdoBrowserCdpBinding.Network bound=nativeBound;nativeBound=null;if(bound!=null)bound.close();nativeChild=null;}
    }
    /** Worker-owned document facade on the exact original Browser session.
     * Root events normalize to null; only native related-iframe events can
     * admit nested sessions. Closing a document never closes the shared wire. */
    static final class DocumentPeer implements MdoBrowserCdpBinding.Peer {
        private final MdoBrowserCdpPopupBroker host;private final Candidate candidate;private final Handoff handoff;private final MdoBrowserCdpPopupNative.Document document;
        private final AtomicBoolean closed=new AtomicBoolean();private volatile MdoBrowserCdpSession.Events receiver;
        private volatile MdoBrowserCdpBinding binding;
        private final Map<String,String> children=new HashMap<>(),nativeChildren=new HashMap<>();private String admitting,parent;private volatile String fault;
        private final Set<String> detachedChildren=new HashSet<>();
        private DocumentPeer(MdoBrowserCdpPopupBroker host,Candidate candidate,MdoBrowserCdpPopupNative.Document document){this.host=host;this.candidate=candidate;handoff=candidate.handoff;this.document=document;}
        private void guard()throws IOException{host.guard();if(fault!=null)throw problem(fault);if(closed.get()||!handoff.current()||!document.current())throw problem("cdp_popup_document_retired");}
        boolean matches(MdoBrowserCdpPopupNative.Document original){return document==original;}
        boolean belongsTo(Handoff original)throws IOException{guard();return handoff==original&&original.owns(document);}
        String target()throws IOException{guard();return candidate.original.target;}
        void observe(MdoBrowserCdpSession.Events next)throws IOException{guard();if(next==null||receiver!=null)throw problem("cdp_controller_busy");receiver=next;}
        void controller(MdoBrowserCdpBinding bound)throws IOException{guard();if(bound==null||binding!=null)throw problem("cdp_controller_busy");binding=bound;}
        void downloads(MdoBrowserCdpDownloads.Connection transfers)throws IOException {
            guard();if(candidate.sourcePeer!=null)throw problem("cdp_controller_busy");if(transfers==null||binding==null||!binding.responseReady(binding.rootFrame))throw problem("cdp_popup_response_admission_required");
            if(candidate.downloads!=null&&candidate.downloads!=transfers)throw problem("cdp_controller_busy");transfers.attach(handoff);candidate.downloads=transfers;
        }
        private MdoBrowserCdpBinding responseDocument(){
            if(closed.get()||fault!=null||!document.current())return null;MdoBrowserCdpBinding bound=binding;if(bound==null)return null;
            try{return bound.responseReady(bound.rootFrame)?bound:null;}catch(IOException error){close();return null;}
        }
        MdoBrowserCdpSession.AttachedResponse nextResponse()throws IOException {
            guard();MdoBrowserCdpBinding bound=binding;if(bound==null||!bound.responseReady(bound.rootFrame))throw problem("cdp_popup_response_admission_required");
            MdoBrowserCdpResponse next=host.session.peekAttachedResponse(candidate.original);if(next==null)return null;
            if(next.frame==null||!bound.responseReady(next.frame))throw problem("cdp_popup_response_frame_unproven");guard();final String frame=next.frame;
            MdoBrowserCdpSession.AttachedResponse response=host.session.nextAttachedResponse(candidate.original,handoff::current,()->{try{return !closed.get()&&document.current()&&bound.responseReady(frame);}catch(IOException error){return false;}},handoff::draining);
            handoff.networkAdmitted.set(true);return response;
        }
        public JSONObject call(String method,JSONObject params,int timeout)throws IOException{return call(method,params,null,timeout,false);}
        public JSONObject call(String method,JSONObject params,String child,int timeout,boolean effect)throws IOException{
            guard();if(child!=null&&!children.containsKey(child))throw problem("cdp_popup_document_session_invalid");
            JSONObject reply;
            try{reply=host.session.callAttachedDocument(candidate.original,handoff::current,handoff::draining,method,params,child,timeout,effect);}
            catch(MdoBrowserCdpSession.Failure error){if(error.attachedCommandCancelled)retire();throw error;}
            if(fault!=null)throw new MdoBrowserCdpSession.Failure(fault,0,effect);if(closed.get()||!document.current())throw new MdoBrowserCdpSession.Failure("cdp_popup_document_retired",0,effect);guard();return reply;
        }
        public boolean poll(int timeout)throws IOException{return poll(timeout,timeout);}
        public boolean poll(int receive,int timeout)throws IOException{guard();boolean progress=host.session.poll(receive,timeout);guard();return progress;}
        public boolean idle()throws IOException{guard();return host.session.idle();}
        public void admitChild(String child)throws IOException{
            guard();if(child==null||!child.equals(admitting)||children.containsKey(child)||host.nestedDocuments.containsKey(child))throw problem("cdp_popup_document_session_invalid");
            if(host.nativeDocuments.get(child)!=this||!nativeChildren.containsKey(child)||!java.util.Objects.equals(nativeChildren.get(child),parent))throw problem("cdp_popup_document_session_invalid");
            children.put(child,parent);host.nestedDocuments.put(child,this);
        }
        private void retireChildData(String child){children.remove(child);host.nestedDocuments.remove(child,this);}
        public void retireChild(String child)throws IOException{guard();if(host.nestedDocuments.get(child)!=this)throw problem("cdp_popup_document_session_invalid");host.session.fenceChild(child);retireChildData(child);}
        private boolean detached(String source,JSONObject params)throws IOException{
            Object id=params.opt("sessionId");if(!(id instanceof String)||!MdoBrowserCdpJson.token((String)id))throw problem("cdp_popup_document_attachment_invalid");
            DocumentPeer original=host.nativeDocuments.get(id);if(original==null)return false;
            if(original!=this||!java.util.Objects.equals(nativeChildren.get(id),source))throw problem("cdp_popup_document_session_invalid");
            if(!detachedChildren.add((String)id))return false;
            host.session.nativeChildDetached((String)id);
            // A parent detach may arrive before its child's detach. Retain
            // that routing ancestor until every original descendant drains.
            boolean progress;do{progress=false;for(String child:new ArrayList<>(detachedChildren))if(!nativeChildren.containsValue(child)){
                host.session.retireChild(child);nativeChildren.remove(child);host.nativeDocuments.remove(child,this);detachedChildren.remove(child);progress=true;
            }}while(progress);return true;
        }
        private void event(String method,String source,JSONObject params)throws IOException{
            if("Target.attachedToTarget".equals(method)){
                Object id=params.opt("sessionId"),info=params.opt("targetInfo");JSONObject metadata=info instanceof JSONObject?(JSONObject)info:null;
                if(!(id instanceof String)||!MdoBrowserCdpJson.token((String)id)||metadata==null||!"iframe".equals(metadata.opt("type"))||!Boolean.FALSE.equals(params.opt("waitingForDebugger")))throw problem("cdp_popup_document_attachment_invalid");
                if(source!=null&&(host.nativeDocuments.get(source)!=this||detachedChildren.contains(source))||host.nativeDocuments.containsKey(id)||host.nativeDocuments.size()>=64)throw problem("cdp_popup_document_session_invalid");
                host.session.admitChild((String)id);nativeChildren.put((String)id,source);host.nativeDocuments.put((String)id,this);
                // Admission to the wire is only a native cleanup obligation.
                // A retired document never gains a listener or DOM authority.
                if(closed.get()||!document.current()){host.session.fenceChild((String)id);return;}
                admitting=(String)id;parent=source;
            }
            if("Target.detachedFromTarget".equals(method)&&!detached(source,params))return;
            try{
                if(closed.get()||!document.current()){
                    if("Target.detachedFromTarget".equals(method))retireChildData((String)params.opt("sessionId"));return;
                }
                MdoBrowserCdpSession.Events action=receiver;if(action==null)throw problem("cdp_popup_document_listener_missing");
                try{action.event(method,source,params);}catch(IOException error){if(!closed.get()&&document.current()){fault=safe(error.getMessage());close();}}
            }finally{
                admitting=null;parent=null;
            }
        }
        private void retire(){close();for(String child:new ArrayList<>(children.keySet())){try{host.session.fenceChild(child);}catch(IOException ignored){}retireChildData(child);}}
        @Override public void close(){closed.set(true);receiver=null;MdoBrowserCdpBinding original=binding;binding=null;if(original!=null)original.retireResponseOrigins();}
    }
    private static final class Release {
        final MdoBrowserCdpSession.ControlReceipt receipt;
        Release(MdoBrowserCdpSession.ControlReceipt receipt){this.receipt=receipt;}
    }
    private final Thread worker=Thread.currentThread();private final BooleanSupplier hostCurrent;private final LongSupplier clock;private final Connector connector;
    private final MdoBrowserCdpBinding.Factory nativeFactory;
    private final List<Registration> registrations=new ArrayList<>();private final List<Candidate> candidates=new ArrayList<>();
    private final List<Release> releases=new ArrayList<>();private final Set<String> seenTargets=new HashSet<>();
    private final Map<String,DocumentPeer> nestedDocuments=new HashMap<>();
    /** Native ancestry survives document cancellation and reset. Only an
     * authenticated detach of that exact session removes its obligation. */
    private final Map<String,DocumentPeer> nativeDocuments=new HashMap<>();
    private final AtomicBoolean closed=new AtomicBoolean();private volatile MdoBrowserCdpSession session;
    private MdoBrowserCdpPopupNative.CloseGate nativeClose;
    private volatile int state=NEW;private volatile boolean enabledMayHaveOccurred,cleanupConfirmed=true;
    private volatile String failure;private boolean attachedSeen,bodyTurn;private int managedTurn;
    MdoBrowserCdpPopupBroker(BooleanSupplier hostCurrent){this(hostCurrent,android.os.SystemClock::elapsedRealtime,NATIVE);}
    MdoBrowserCdpPopupBroker(BooleanSupplier hostCurrent,LongSupplier clock){this(hostCurrent,clock,NATIVE);}
    MdoBrowserCdpPopupBroker(BooleanSupplier hostCurrent,LongSupplier clock,Connector connector){
        this(hostCurrent,clock,connector,null);
    }
    MdoBrowserCdpPopupBroker(BooleanSupplier hostCurrent,LongSupplier clock,Connector connector,MdoBrowserCdpBinding.Factory nativeFactory){
        if(hostCurrent==null||clock==null||connector==null)throw new IllegalArgumentException("Popup broker requires its original worker");
        this.hostCurrent=hostCurrent;this.clock=clock;this.connector=connector;this.nativeFactory=nativeFactory;
    }
    private static IOException problem(String code){return new IOException(code);}
    private void thread()throws IOException{if(Thread.currentThread()!=worker)throw problem("cdp_wrong_worker");}
    private boolean current(){try{return !closed.get()&&hostCurrent.getAsBoolean();}catch(RuntimeException|Error error){return false;}}
    private void guard()throws IOException {
        thread();if(closed.get())throw problem(failure==null?"cdp_popup_broker_closed":failure);
        if(!current())throw fail("cdp_popup_broker_owner_changed");
    }
    private long now()throws IOException{try{long value=clock.getAsLong();if(value<0)throw fail("cdp_popup_broker_clock_invalid");return value;}catch(RuntimeException|Error error){throw fail("cdp_popup_broker_clock_invalid");}}
    private int remaining(long born,int timeout)throws IOException{
        guard();long used=now()-born;if(used<0||used>timeout-10)throw fail("cdp_popup_broker_timeout");return timeout-(int)used;
    }
    void guardNativeClose()throws IOException{guard();if(state!=NEW||nativeClose!=null)throw problem("cdp_popup_close_scope_invalid");nativeClose=new MdoBrowserCdpPopupNative.CloseGate();}
    void retainNative(MdoBrowserCdpPopupNative.Retirement original)throws IOException{guard();if(nativeClose==null)throw problem("cdp_popup_close_scope_invalid");nativeClose.add(original);}
    Registration register(MdoBrowserCdpBinding.Network owner)throws IOException {
        guard();if(state!=NEW&&state!=ARMED)throw problem("cdp_popup_broker_state_invalid");
        if(owner==null)throw problem("cdp_popup_opener_invalid");prune();
        MdoBrowserCdpBinding.Network.PopupSource source=owner.popupSource();String target=source.target();
        for(Registration original:registrations)if(original.source==source||original.target.equals(target))throw problem("cdp_popup_opener_busy");
        if(registrations.size()>=8)throw problem("cdp_popup_opener_limit");Registration added=new Registration(source,target);registrations.add(added);return added;
    }
    /** Only the original native handoff can become another opener. Its target
     * comes from this broker's own retained attachment, never caller discovery. */
    Registration registerAttached(Handoff handoff,MdoBrowserCdpPopupNative.Source proof)throws IOException {
        guard();prune();if(state!=ARMED||handoff==null||proof==null||!handoff.current()||!handoff.owns(proof)||!proof.current())throw problem("cdp_popup_attached_opener_invalid");
        Candidate original=null;for(Candidate candidate:candidates)if(candidate.handoff==handoff)original=candidate;
        MdoBrowserCdpSession.ControlResult activation=handoff.activationResult();
        if(original==null||!original.current()||!original.original.resumeConfirmed()||activation==null||!activation.ok||handoff.flowFailure()!=null)throw problem("cdp_popup_attached_opener_unavailable");
        for(Registration row:registrations)if(row.handoff==handoff||row.target.equals(original.original.target))throw problem("cdp_popup_opener_busy");
        if(registrations.size()>=8)throw problem("cdp_popup_opener_limit");
        Registration added=new Registration(handoff,proof,original.original.target);registrations.add(added);return added;
    }
    void arm(int timeout)throws IOException {
        guard();if(state!=NEW)throw problem("cdp_popup_broker_state_invalid");
        if(timeout<10||timeout>5000)throw problem("cdp_arguments_invalid");prune();if(registrations.isEmpty())throw problem("cdp_popup_opener_unavailable");
        if(!PROCESS.compareAndSet(null,this))throw problem(PROCESS.get()==UNCERTAIN?"cdp_popup_cleanup_unconfirmed":"cdp_popup_broker_process_busy");
        state=STARTING;long born=now();
        try{
            session=connector.open(remaining(born,timeout),this::current,this::event);guard();
            if(session==null)throw problem("cdp_popup_broker_connection_invalid");prune();if(registrations.isEmpty())throw problem("cdp_popup_opener_unavailable");
            if(nativeClose!=null)session.deferPopupClose(nativeClose);
            JSONObject params=new JSONObject().put("autoAttach",true).put("waitForDebuggerOnStart",true).put("flatten",true)
                .put("filter",new JSONArray().put(new JSONObject().put("type","page").put("exclude",false)).put(new JSONObject().put("exclude",true)));
            cleanupConfirmed=false;enabledMayHaveOccurred=true;
            JSONObject reply=session.call("Target.setAutoAttach",params,null,remaining(born,timeout),true);
            if(reply.length()!=0)throw problem("cdp_popup_broker_reply_invalid");
            drain(born,timeout);remaining(born,timeout);prune();state=ARMED;
            if(registrations.isEmpty()){disarm(remaining(born,timeout));throw problem("cdp_popup_opener_unavailable");}
        }catch(MdoBrowserCdpSession.Failure error){
            if(error.protocolCode==-32601&&"cdp_method_unsupported".equals(error.getMessage())&&!attachedSeen){enabledMayHaveOccurred=false;cleanupConfirmed=true;}
            throw fail(safe(error.getMessage()));
        }catch(JSONException error){throw fail("cdp_popup_broker_arguments_invalid");}
        catch(IOException error){throw fail(safe(error.getMessage()));}
        catch(RuntimeException|Error error){throw fail("cdp_popup_broker_failed");}
    }
    private static String safe(String code){return code!=null&&code.matches("cdp_[a-z_]{1,80}")?code:"cdp_popup_broker_failed";}
    private void event(String method,String child,JSONObject params)throws IOException {
        guard();if(session==null)throw fail("cdp_popup_broker_connection_invalid");
        if(child!=null){
            for(Candidate candidate:candidates)if(candidate.original.matchesNativeSession(child)){
                boolean live=candidate.current();
                if(retiringCandidate(candidate)){
                    // Never deliver closing-page data or resume its requests.
                    // Keep the existing bounded pause until exact native detach.
                    if("Fetch.requestPaused".equals(method))session.retainAttachedRequest(candidate.original);
                    else if(method.equals("Target.attachedToTarget")||method.equals("Target.detachedFromTarget")){
                        DocumentPeer document=candidate.documentPeer;
                        if(document==null)for(DocumentPeer retained:nativeDocuments.values())if(retained.candidate==candidate){document=retained;break;}
                        if(document==null)throw fail("cdp_popup_broker_child_unproven");document.event(method,null,params);finishDetached(candidate);
                    }return;
                }
                if(!live)throw fail("cdp_popup_candidate_retired");
                MdoBrowserCdpPopupSource source=candidate.sourcePeer;
                if("Fetch.requestPaused".equals(method)){
                    try{if(source!=null)source.capture();else session.retainAttachedRequest(candidate.original);}
                    catch(MdoBrowserCdpPopupSource.Retired ended){if(!ended.issuedBy(source)||!retiringCandidate(candidate))throw ended;session.retainAttachedRequest(candidate.original);}
                    return;
                }
                if(source!=null)try{source.event(method,params);}
                    catch(MdoBrowserCdpPopupSource.Retired ended){if(!ended.issuedBy(source)||!retiringCandidate(candidate))throw ended;return;}
                DocumentPeer document=candidate.documentPeer;if(document!=null)document.event(method,null,params);
                else if(source==null||!(method.startsWith("Page.")||method.startsWith("Network.")||method.startsWith("Inspector.")))throw fail("cdp_popup_broker_child_unproven");return;
            }
            DocumentPeer document=nativeDocuments.get(child);if(document!=null){
                if(document.detachedChildren.contains(child)&&!method.equals("Target.detachedFromTarget")&&!method.equals("Target.attachedToTarget"))return;
                if("Fetch.requestPaused".equals(method))throw fail("cdp_popup_document_response_unqualified");
                document.event(method,child,params);finishDetached(document.candidate);return;}
            throw fail("cdp_popup_broker_child_unproven");
        }
        if("Target.attachedToTarget".equals(method)){
            attachedSeen=true;MdoBrowserCdpSession.AttachedTarget original=session.attachedTarget();
            if(state==STOPPING){
                if(original.waiting)throw fail("cdp_popup_broker_disable_race");
                session.ignoreAttachedTargetDuringDisable();return;
            }
            if(seenTargets.size()>=256||!seenTargets.add(original.target))throw fail("cdp_popup_broker_target_limit");
            if(!original.waiting){
                if(releases.size()>=64)throw fail("cdp_popup_broker_release_limit");
                releases.add(new Release(session.releaseUnclaimedAttachedTarget()));return;
            }
            if(!"page".equals(original.type))throw fail("cdp_popup_broker_target_type_invalid");
            if(candidates.size()>=7)throw fail("cdp_popup_broker_candidate_limit");
            Registration[] possible=registrations.toArray(new Registration[0]);
            if(possible.length==0)throw fail("cdp_popup_opener_unavailable");
            for(Registration item:possible)if(!item.current())throw fail("cdp_popup_broker_opener_lost");
            MdoBrowserCdpSession.AttachedTarget held=session.retainAttachedTarget();candidates.add(new Candidate(possible,held,now(),closed));return;
        }
        if("Target.detachedFromTarget".equals(method)&&child==null){
            Object id=params.opt("sessionId");
            DocumentPeer nested=nativeDocuments.get(id);if(nested!=null){nested.event(method,nested.nativeChildren.get(id),params);finishDetached(nested.candidate);return;}
            for(int i=candidates.size()-1;i>=0;i--){Candidate candidate=candidates.get(i);if(candidate.original.matchesNativeSession(id)){
                candidate.rootDetached=true;session.nativeAttachedTargetDetached(candidate.original);candidate.retired();finishDetached(candidate);
            }}return;
        }
        if("Fetch.requestPaused".equals(method)&&child!=null){
            for(Candidate candidate:candidates)if(candidate.original.matchesNativeSession(child)){if(!candidate.current())throw fail("cdp_popup_candidate_retired");session.retainAttachedRequest(candidate.original);return;}
            throw fail("cdp_popup_broker_request_scope_invalid");
        }
        if(child!=null)throw fail("cdp_popup_broker_child_unproven");
        if(method.startsWith("Fetch."))throw fail("cdp_popup_broker_response_unproven");
    }
    private boolean descendants(Candidate original){for(DocumentPeer peer:nativeDocuments.values())if(peer.candidate==original)return true;return false;}
    private void finishDetached(Candidate original)throws IOException{
        if(!original.rootDetached||descendants(original))return;
        session.retireAttachedTarget(original.original);
        if(session.originalCleanupPending(original.original))return;
        if(original.handoff!=null)original.handoff.detached.set(true);candidates.remove(original);
    }
    MdoBrowserCdpBinding bindDocument(Handoff handoff,MdoBrowserCdpPopupNative.Document proof,MdoBrowserCdpMarker marker,int timeout)throws IOException{
        guard();prune();if(handoff==null||proof==null||marker==null||marker.lease()!=proof.lease||!handoff.current()||!handoff.owns(proof)||!proof.current()||timeout<100||timeout>15000)throw problem("cdp_popup_document_owner_invalid");long born=now();
        MdoBrowserCdpSession.ControlResult active=handoff.activationResult();if(active==null||!active.ok)throw problem("cdp_popup_activation_required");
        Candidate original=null;for(Candidate candidate:candidates)if(candidate.handoff==handoff)original=candidate;if(original==null)throw problem("cdp_popup_candidate_retired");
        DocumentPeer old=original.documentPeer;
        if(old!=null){
            if(!old.closed.get()&&old.document.current())throw problem("cdp_popup_document_busy");old.close();
            try{
                JSONObject disable=new JSONObject().put("autoAttach",false).put("waitForDebuggerOnStart",false).put("flatten",true);
                JSONObject reply=session.callAttachedDocument(original.original,handoff::current,handoff::draining,"Target.setAutoAttach",disable,null,Math.min(5000,remaining(born,timeout)),true);if(reply.length()!=0)throw fail("cdp_popup_document_reset_invalid");
                if(descendants(original))throw problem("cdp_popup_document_descendants_pending");
                reply=session.callAttachedDocument(original.original,handoff::current,handoff::draining,"Runtime.disable",new JSONObject(),null,Math.min(5000,remaining(born,timeout)),false);if(reply.length()!=0)throw fail("cdp_popup_document_reset_invalid");
                old.retire();original.documentPeer=null;
            }catch(MdoBrowserCdpSession.Failure error){if(error.attachedCommandCancelled)old.retire();throw error;
            }catch(JSONException error){throw fail("cdp_popup_document_reset_invalid");}
        }
        DocumentPeer peer=new DocumentPeer(this,original,proof);original.documentPeer=peer;
        int left=remaining(born,timeout);if(left<100){peer.close();throw problem("cdp_popup_document_timeout");}
        return MdoBrowserCdpBinding.bindAttached(peer,proof,marker,left);
    }
    MdoBrowserCdpBinding bindSource(Handoff handoff,MdoBrowserCdpPopupNative.Source proof,MdoBrowserCdpMarker marker,int timeout)throws IOException {
        guard();prune();if(handoff==null||proof==null||marker==null||marker.lease()!=proof.lease||!handoff.current()||!handoff.owns(proof)||!proof.current()||timeout<100||timeout>15000)throw problem("cdp_popup_source_owner_invalid");
        Candidate original=null;for(Candidate candidate:candidates)if(candidate.handoff==handoff)original=candidate;if(original==null)throw problem("cdp_popup_candidate_retired");
        if(handoff.activationRequested.get()||original.original.resumeConfirmed())throw problem("cdp_popup_source_activation_started");
        if(original.sourcePeer!=null)throw problem("cdp_controller_busy");
        MdoBrowserCdpPopupSource peer=new MdoBrowserCdpPopupSource(session,original.original,handoff,proof,original.sourceFrame,original.sourceLoader);original.sourcePeer=peer;
        try{return MdoBrowserCdpBinding.bindSource(peer,proof,marker,timeout);}catch(IOException|RuntimeException|Error error){handoff.sourceFailure(error);peer.close();throw error;}
    }
    /** Read deliberate retirement after the native/current snapshot. A UI
     * close can occur inside that snapshot; an older clock sample must not
     * turn its newly recorded closeBorn into a backwards-clock failure. */
    private boolean retiringCandidate(Candidate candidate)throws IOException {
        Handoff closing=candidate.handoff;if(closing==null||!closing.retiring())return false;
        if(!closing.retirementCurrent(now()))throw fail("cdp_popup_handoff_cleanup_unconfirmed");return true;
    }
    private void prune()throws IOException{
        for(int i=candidates.size()-1;i>=0;i--){Candidate original=candidates.get(i);if(original.rootDetached)finishDetached(original);}
        long time=now();for(Candidate candidate:candidates){long age=time-candidate.born;
            candidate.reconcileHandoff();
            boolean live=candidate.current();if(retiringCandidate(candidate))continue;
            if(!live)throw fail("cdp_popup_broker_opener_lost");
            if(age<0||age>=15000&&(candidate.handoff==null||!candidate.handoff.networkAdmitted.get()))throw fail("cdp_popup_broker_candidate_expired");
        }
        for(int i=registrations.size()-1;i>=0;i--)if(!registrations.get(i).current())registrations.remove(i);
        for(int i=releases.size()-1;i>=0;i--){MdoBrowserCdpSession.ControlResult result=releases.get(i).receipt.result();
            if(result!=null){if(!result.ok)throw fail(result.code==null?"cdp_popup_broker_release_failed":safe(result.code));releases.remove(i);}
        }
    }
    private void drain(long born,int timeout)throws IOException{
        while(session.pendingControls()!=0||session.pendingBodies()!=0||session.pendingOffers()!=0){
            int left=remaining(born,timeout);session.serviceBodies(Math.min(250,left));
            left=remaining(born,timeout);session.poll(Math.min(20,left),left);prune();
        }prune();
    }
    /** One fixed flow/response decision per turn on the existing worker. Fair
     * across bounded children; Bodies still require native UI acceptance. */
    private boolean serviceManaged(int timeout)throws IOException {
        int count=candidates.size();for(int n=0;n<count;n++){
            int index=(managedTurn+n)%count;Candidate candidate=candidates.get(index);Handoff handoff=candidate.handoff;
            if(handoff==null||!handoff.activationRequested.get())continue;
            boolean live=handoff.current();if(retiringCandidate(candidate)){
                if(retireHuman(candidate,timeout)){managedTurn=(index+1)%count;return true;}continue;
            }
            if(!live)throw fail("cdp_popup_candidate_retired");
            try{
                if(handoff.activationResult.get()==null){
                    session.resumeProtectedAttachment(candidate.original,handoff::current,handoff::draining,timeout);
                    handoff.activationResult.set(new MdoBrowserCdpSession.ControlResult(true,null,0,true));managedTurn=(index+1)%count;return true;
                }
                if(session.continueProtectedRequest(candidate.original,handoff::current,handoff::draining,timeout)){
                    handoff.continuedRequests.incrementAndGet();managedTurn=(index+1)%count;return true;
                }
                if(candidate.sourcePeer!=null){if(candidate.sourcePeer.turn(timeout)){managedTurn=(index+1)%count;return true;}}
                else if(candidate.downloads!=null){DocumentPeer document=candidate.documentPeer;MdoBrowserCdpBinding bound=document==null?null:document.responseDocument();
                    if(candidate.downloads.attachedTurn(handoff,bound,timeout)){managedTurn=(index+1)%count;return true;}}
            }catch(IOException|RuntimeException|Error error){
                int protocol=error instanceof MdoBrowserCdpSession.Failure?((MdoBrowserCdpSession.Failure)error).protocolCode:0;
                boolean effect=!(error instanceof MdoBrowserCdpSession.Failure)||((MdoBrowserCdpSession.Failure)error).effectMayHaveOccurred;
                MdoBrowserCdpSession.ControlResult result=new MdoBrowserCdpSession.ControlResult(false,error instanceof IOException?safe(error.getMessage()):"cdp_popup_activation_failed",protocol,effect);
                handoff.activationResult.compareAndSet(null,result);handoff.flowFailure.compareAndSet(null,result);
                boolean cancelled=error instanceof MdoBrowserCdpSession.Failure&&((MdoBrowserCdpSession.Failure)error).attachedCommandCancelled||
                    error instanceof MdoBrowserCdpPopupSource.Retired&&((MdoBrowserCdpPopupSource.Retired)error).issuedBy(candidate.sourcePeer)||
                    java.util.Arrays.asList("cdp_attach_owner_changed","cdp_attach_response_unavailable","cdp_popup_document_retired","cdp_download_owner_changed","cdp_download_connection_closed").contains(result.code);
                boolean idle=false;if(cancelled&&handoff.draining())try{idle=session.idle();}catch(IOException ignored){/* a failed shared wire is never recovered here */}
                if(idle){if(candidate.documentPeer!=null)candidate.documentPeer.retire();managedTurn=(index+1)%count;return true;}throw error;
            }
        }return false;
    }
    /** Retire only this original human-owned root: disable its child attachment,
     * await authentic descendant ends, drain exact pauses and existing sources,
     * then require root ACK AND native end. No data authority is reacquired. */
    private boolean retireHuman(Candidate candidate,int timeout)throws IOException {
        Handoff handoff=candidate.handoff;
        if(!handoff.humanDetachRequested.get()||handoff.humanDetachResult.get()!=null)return false;
        MdoBrowserCdpPopupNative.Promotion owner=handoff.nativeOwner.get();
        if(owner==null||!owner.humanTransferred()||owner.failed())throw problem("cdp_popup_human_owner_invalid");
        // A real host close can win before dispatch. Let its authenticated
        // native detach drain normally; do not detach again or poison peers.
        if(owner.nativeDestroyed()||candidate.rootDetached)return false;
        if(!owner.ended())return false;
        DocumentPeer document=candidate.documentPeer;
        if(document!=null)document.retire();
        MdoBrowserCdpPopupSource source=candidate.sourcePeer;if(source!=null)source.close();
        MdoBrowserCdpDownloads.Connection transfers=candidate.downloads;
        if(source!=null&&!source.retirementQuiescent()||transfers!=null&&!transfers.retirementQuiescent()||source!=null&&!source.downloadsRetired())return false;
        try{
            if((document!=null||descendants(candidate))&&!candidate.original.autoAttachStopConfirmed()){
                session.disableProtectedAutoAttach(candidate.original,handoff::draining,timeout);return true;
            }
            if(owner.nativeDestroyed()||candidate.rootDetached||descendants(candidate))return false;
            if(session.abortProtectedPause(candidate.original,handoff::draining,timeout))return true;
            session.detachProtectedAttachment(candidate.original,handoff::draining,timeout);
            handoff.humanDetachResult.set(new MdoBrowserCdpSession.ControlResult(true,null,0,true));return true;
        }catch(MdoBrowserCdpSession.Failure error){
            if("cdp_attach_retirement_busy".equals(error.getMessage())&&!error.effectMayHaveOccurred)return false;
            handoff.humanDetachResult.set(new MdoBrowserCdpSession.ControlResult(false,safe(error.getMessage()),error.protocolCode,error.effectMayHaveOccurred));throw error;
        }catch(IOException|RuntimeException|Error error){
            handoff.humanDetachResult.set(new MdoBrowserCdpSession.ControlResult(false,error instanceof IOException?safe(error.getMessage()):"cdp_popup_human_detach_failed",0,true));throw error;
        }
    }
    boolean turn(int receiveMs,int timeout)throws IOException {
        guard();if(state!=ARMED)throw problem("cdp_popup_broker_state_invalid");
        if(receiveMs<10||receiveMs>20||timeout<receiveMs||timeout>5000)throw problem("cdp_arguments_invalid");
        try{prune();if(registrations.isEmpty()&&candidates.isEmpty()){disarm(timeout);return true;}if(!session.idle())return false;
            if(bodyTurn&&session.serviceBodies(timeout)){bodyTurn=false;prune();return true;}
            if(serviceManaged(timeout)){bodyTurn=true;prune();return true;}
            if(session.serviceBodies(timeout)){bodyTurn=false;prune();return true;}
            boolean progress=session.poll(receiveMs,timeout);prune();return progress;
        }catch(IOException error){throw fail(safe(error.getMessage()));}
        catch(RuntimeException|Error error){throw fail("cdp_popup_broker_failed");}
    }
    /** Cooperative RPC progress may observe captures while this same Browser
     * wire is awaiting a reply. Defer protocol work until its original call
     * finishes; never turn a healthy serialized connection into a reentry. */
    boolean idle()throws IOException {guard();return state==ARMED&&session.idle();}
    List<Candidate> pending()throws IOException{guard();prune();return new ArrayList<>(candidates);}
    void refuseProtection(Candidate candidate,String code)throws IOException {
        guard();if(candidate==null||!candidates.contains(candidate)||!candidate.current())throw problem("cdp_popup_candidate_retired");
        candidate.protection.compareAndSet(null,new MdoBrowserCdpSession.ControlResult(false,safe(code),0,false));
    }
    void protect(Candidate candidate,int timeout)throws IOException {
        guard();prune();if(state!=ARMED||candidate==null||!candidates.contains(candidate)||!candidate.current()||!candidate.protectionRequested())throw problem("cdp_popup_candidate_retired");
        if(timeout<10||timeout>5000)throw problem("cdp_arguments_invalid");
        if(candidate.protection.get()!=null)throw problem("cdp_popup_protection_replayed");
        candidate.protectionStarted=true;
        try{session.protectAttachedTarget(candidate.original,timeout);prune();
            candidate.protection.compareAndSet(null,new MdoBrowserCdpSession.ControlResult(true,null,0,true));
        }catch(MdoBrowserCdpSession.Failure error){
            String code=error.protocolCode==-32601?"cdp_popup_interception_unsupported":error.protocolCode!=0?"cdp_popup_interception_rejected":safe(error.getMessage());
            candidate.protection.compareAndSet(null,new MdoBrowserCdpSession.ControlResult(false,code,error.protocolCode,error.effectMayHaveOccurred||candidate.retainedRequestCount()!=0));
            if(error.protocolCode==0)throw fail(code);
        }catch(IOException|RuntimeException|Error error){
            candidate.protection.compareAndSet(null,new MdoBrowserCdpSession.ControlResult(false,"cdp_popup_interception_failed",0,true));throw fail("cdp_popup_interception_failed");
        }
    }
    void proveNative(Registration opener,MdoBrowserCdpPopupNative.Child child,int timeout,BooleanSupplier requestCurrent)throws IOException {
        guard();prune();if(state!=ARMED||opener==null||!registrations.contains(opener)||!opener.current())throw problem("cdp_popup_candidate_retired");
        if(timeout<100||timeout>5000)throw problem("cdp_arguments_invalid");
        if(child==null||requestCurrent==null||!requestCurrent.getAsBoolean()||!child.ownedBy(opener)||!child.claim()){
            if(child!=null)child.finish(false,"cdp_popup_native_child_invalid",0,false);return;
        }
        MdoBrowserCdpBinding.Network bound=null;long born=now();
        try{
            List<Candidate> frozen=new ArrayList<>();List<MdoBrowserCdpSession.AttachedTarget> targets=new ArrayList<>();
            for(Candidate candidate:candidates)if(child.includes(candidate)&&candidate.mayBelongTo(opener)&&candidate.current()&&candidate.nativeBound==null){
                if(!candidate.original.protectionConfirmed())throw problem("cdp_popup_interception_required");frozen.add(candidate);targets.add(candidate.original);
            }
            bound=MdoBrowserCdpBinding.bindPopupNative(child,targets,remaining(born,timeout),()->current()&&opener.current()&&requestCurrent.getAsBoolean(),nativeFactory);
            Candidate matched=null;for(Candidate candidate:frozen)if(bound.matchesPopup(candidate.original)){if(matched!=null)throw problem("cdp_popup_native_child_ambiguous");matched=candidate;}
            if(matched==null)throw problem("cdp_popup_native_child_changed");
            session.confirmProtectedAttachment(matched.original,remaining(born,timeout));
            guard();prune();if(!requestCurrent.getAsBoolean()||!matched.current()||!child.current()||!bound.physicalCurrent())throw problem("cdp_popup_native_child_changed");
            Handoff prepared=new Handoff(child,closed,matched.born,clock);
            matched.sourceFrame=bound.sourceFrame();matched.sourceLoader=bound.sourceLoader();
            matched.provenOpener=opener;matched.nativeChild=child;matched.nativeBound=bound;matched.handoff=prepared;bound=null;child.prepareHandoff(prepared);child.finish(true,null,0,true);
        }catch(IOException error){child.finish(false,safe(error.getMessage()),error instanceof MdoBrowserCdpSession.Failure?((MdoBrowserCdpSession.Failure)error).protocolCode:0,true);}
        catch(RuntimeException|Error error){child.finish(false,"cdp_popup_native_proof_failed",0,true);}
        finally{if(bound!=null)bound.close();}
    }
    void disarm(int timeout)throws IOException {
        guard();if(state!=ARMED)throw problem("cdp_popup_broker_state_invalid");
        if(timeout<10||timeout>5000)throw problem("cdp_arguments_invalid");prune();
        // The native popup owner must first cancel/adopt/retire these exact
        // candidates. Silently detaching would release an unproven response.
        if(!candidates.isEmpty())throw problem("cdp_popup_broker_candidates_pending");
        long born=now();
        try{drain(born,timeout);}catch(IOException error){throw fail(safe(error.getMessage()));}
        catch(RuntimeException|Error error){throw fail("cdp_popup_broker_failed");}
        if(!candidates.isEmpty())throw problem("cdp_popup_broker_candidates_pending");state=STOPPING;
        try{
            JSONObject reply=session.call("Target.setAutoAttach",new JSONObject().put("autoAttach",false).put("waitForDebuggerOnStart",false).put("flatten",true),null,remaining(born,timeout),true);
            if(reply.length()!=0)throw problem("cdp_popup_broker_reply_invalid");remaining(born,timeout);
            cleanupConfirmed=true;state=STOPPED;closed.set(true);
            try{session.close();}finally{PROCESS.compareAndSet(this,null);clear();}
        }catch(JSONException error){throw fail("cdp_popup_broker_arguments_invalid");}
        catch(IOException error){throw fail(safe(error.getMessage()));}
        catch(RuntimeException|Error error){throw fail("cdp_popup_broker_failed");}
    }
    boolean armed(){return state==ARMED&&!closed.get();}
    boolean cleanupConfirmed(){return cleanupConfirmed&&(nativeClose==null||!nativeClose.failed());}
    boolean enabledMayHaveOccurred(){return enabledMayHaveOccurred;}
    private IOException fail(String code){if(failure==null)failure=safe(code);state=FAILED;try{close();}finally{if(Thread.currentThread()==worker)clear();}return problem(failure);}
    private void clear(){for(Candidate candidate:candidates)candidate.retired();for(Registration registration:registrations)registration.close();candidates.clear();nestedDocuments.clear();nativeDocuments.clear();registrations.clear();releases.clear();seenTargets.clear();}
    @Override public void close(){
        if(!closed.compareAndSet(false,true))return;
        // A socket close wakes the worker but is not an acknowledgement that
        // Chromium has removed its process-wide pause. Keep a small fence,
        // not this broker/its owners, until the process itself is replaced.
        PROCESS.compareAndSet(this,cleanupConfirmed?null:UNCERTAIN);
        MdoBrowserCdpSession original=session;if(original!=null)try{original.close();}catch(RuntimeException|Error error){if(failure==null)failure="cdp_popup_broker_close_failed";state=FAILED;}
    }
}
