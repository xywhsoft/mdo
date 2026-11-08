package org.xserver.android;

import android.os.Looper;
import java.io.IOException;
import java.lang.ref.WeakReference;
import java.util.function.BooleanSupplier;

/** UI-owned preparation for one exact physical opener/document. The native
 * worker keeps only Forward's weak handle; effects run once on the original UI
 * after the existing process broker has acknowledged its protection setup. */
final class MdoBrowserCdpPopupPreparation implements AutoCloseable {
    interface Failure {void failed(String code,boolean effectMayHaveOccurred);}
    interface Lost {void lost(String code);}
    interface Allocated {void ready(MdoBrowserCdpPopupNative source,MdoBrowserCdpPopupWorker.Allocation allocation)throws IOException;}
    interface Proven {void ready(MdoBrowserCdpPopupNative.Child child)throws IOException;}
    interface Consumed {void ready(MdoBrowserCdpPopupNative source,MdoBrowserCdpPopupNative.Created child,MdoBrowserPopups.Adoption receipt)throws IOException;}
    private static final int NEW=0,STARTING=1,READY=2,RETIRING=3,TERMINAL=4;
    private static final class Pending {
        final Runnable effect;final Failure failure;
        Pending(Runnable effect,Failure failure){this.effect=effect;this.failure=failure;}
    }
    private static final class Forward implements MdoBrowserCdpPopupWorker.Events {
        private final WeakReference<MdoBrowserCdpPopupPreparation> owner;
        Forward(MdoBrowserCdpPopupPreparation owner){this.owner=new WeakReference<>(owner);}
        public void state(MdoBrowserCdpPopupWorker.Result result){MdoBrowserCdpPopupPreparation value=owner.get();if(value!=null)value.state(result);}
        public void candidate(MdoBrowserCdpPopupBroker.Candidate candidate){MdoBrowserCdpPopupPreparation value=owner.get();if(value!=null)value.candidate(candidate);}
        public void protection(MdoBrowserCdpPopupBroker.Candidate candidate){MdoBrowserCdpPopupPreparation value=owner.get();if(value!=null)value.protection(candidate);}
        public void nativeProof(MdoBrowserCdpPopupNative.Child child){MdoBrowserCdpPopupPreparation value=owner.get();if(value!=null)value.nativeProof(child);}
    }
    private static final class AllocationWait {
        final MdoBrowserPopups.Pending pending;final long deadline;final Allocated ready;final Failure failure;
        AllocationWait(MdoBrowserPopups.Pending pending,long deadline,Allocated ready,Failure failure){this.pending=pending;this.deadline=deadline;this.ready=ready;this.failure=failure;}
    }
    private static final class ProofWait {
        final long deadline;final Proven ready;final Failure failure;
        ProofWait(long deadline,Proven ready,Failure failure){this.deadline=deadline;this.ready=ready;this.failure=failure;}
    }
    private static final class AdoptionWait {
        final MdoBrowserCdpPopupNative.Created child;final long deadline;final Consumed ready;final Failure failure;
        MdoBrowserPopups.Adoption receipt;MdoBrowserCdpPopupNative.Consumption subscription;
        AdoptionWait(MdoBrowserCdpPopupNative.Created child,long deadline,Consumed ready,Failure failure){this.child=child;this.deadline=deadline;this.ready=ready;this.failure=failure;}
    }
    private final MdoBrowserCdpPopupNative source;
    private final BooleanSupplier owner;private final Lost lost;
    private final android.os.Handler handler=new android.os.Handler(Looper.getMainLooper());
    private final MdoBrowserCdpPopupNative.Child[] children=new MdoBrowserCdpPopupNative.Child[7];
    private final ProofWait[] proofs=new ProofWait[7];
    private final AdoptionWait[] adoptions=new AdoptionWait[7];
    private final Runnable watch=this::tick;
    private AllocationWait allocation;private MdoBrowserPopups.Pending allocated;private boolean allocating,watching;
    private MdoBrowserCdpPopupWorker.Request request;private Pending pending;
    private int phase=NEW;private String code;private boolean notified,everReady,running;
    MdoBrowserCdpPopupPreparation(MdoBrowserCdpPopupNative source,BooleanSupplier owner,Lost lost){
        thread();if(source==null||owner==null||lost==null)throw new IllegalArgumentException("Missing original popup preparation owner");this.source=source;this.owner=owner;this.lost=lost;
    }
    private static void thread(){if(Looper.myLooper()!=Looper.getMainLooper())throw new IllegalStateException("Popup preparation requires its original UI");}
    private boolean owned(){try{return owner.getAsBoolean()&&source.current();}catch(RuntimeException|Error error){return false;}}
    void start(MdoBrowserCdpWorker worker,int timeout)throws IOException {
        start(worker,timeout,null,null);
    }
    void startAttached(MdoBrowserCdpWorker worker,int timeout,MdoBrowserCdpPopupBroker.Handoff handoff,MdoBrowserCdpPopupNative.Source proof)throws IOException {
        if(handoff==null||proof==null)throw new IOException("cdp_popup_attached_opener_invalid");start(worker,timeout,handoff,proof);
    }
    private void start(MdoBrowserCdpWorker worker,int timeout,MdoBrowserCdpPopupBroker.Handoff handoff,MdoBrowserCdpPopupNative.Source proof)throws IOException {
        thread();if(phase!=NEW||worker==null)throw new IOException("cdp_popup_preparation_replayed");phase=STARTING;
        try{if(!owner.getAsBoolean())throw new IOException("cdp_popup_preparation_owner_changed");
            request=handoff==null?worker.prepareNativePopups(source,timeout,new Forward(this)):worker.prepareAttachedNativePopups(source,handoff,proof,timeout,new Forward(this));}
        catch(IOException|RuntimeException|Error error){phase=TERMINAL;code=error instanceof IOException?safe(error.getMessage()):"cdp_popup_preparation_failed";throw error;}
    }
    private static String safe(String value){return value!=null&&value.matches("cdp_[a-z_]{1,80}")?value:"cdp_popup_preparation_failed";}
    private static long deadline(int timeout)throws IOException {
        if(timeout<1||timeout>3000)throw new IOException("cdp_popup_delivery_arguments_invalid");return android.os.SystemClock.elapsedRealtime()+timeout;
    }
    private static void failed(Failure callback,String code,boolean effect){try{callback.failed(code,effect);}catch(RuntimeException|Error ignored){/* never replay a failed UI delivery */}}
    void allocate(String pendingId,int timeout,Allocated ready,Failure failure)throws IOException {
        thread();if(ready==null||failure==null||pendingId==null||pendingId.isEmpty()||pendingId.length()>160)throw new IOException("cdp_popup_delivery_arguments_invalid");
        long end=deadline(timeout);
        if(allocation!=null||allocating)throw new IOException("cdp_popup_native_allocation_busy");
        if(phase!=READY||!owned()||request==null)throw new IOException("cdp_popup_native_allocation_unavailable");
        MdoBrowserPopups.Pending pending=source.pending();
        if(pending==null||!pending.current()||!pending.id.equals(pendingId))throw new IOException("cdp_popup_native_request_changed");
        if(pending==allocated)throw new IOException("cdp_popup_native_allocation_replayed");
        allocation=new AllocationWait(pending,end,ready,failure);drainAllocation();schedule();
    }
    private void drainAllocation(){
        AllocationWait original=allocation;if(original==null)return;
        if(phase!=READY||!owned()){close("cdp_popup_preparation_owner_changed");return;}
        if(android.os.SystemClock.elapsedRealtime()>=original.deadline){allocation=null;failed(original.failure,"cdp_popup_native_allocation_timed_out",false);return;}
        if(source.pending()!=original.pending||!original.pending.current()){allocation=null;failed(original.failure,"cdp_popup_native_request_changed",false);return;}
        MdoBrowserCdpPopupWorker.Allocation captured;
        try{captured=request.allocateNative(source);}
        catch(IOException error){
            if("cdp_popup_native_capture_pending".equals(error.getMessage())||"cdp_popup_interception_required".equals(error.getMessage()))return;
            allocation=null;failed(original.failure,safe(error.getMessage()),false);return;
        }catch(RuntimeException|Error error){allocation=null;close("cdp_popup_preparation_failed");failed(original.failure,"cdp_popup_preparation_failed",false);return;}
        if(captured.pending!=original.pending){allocation=null;close("cdp_popup_native_request_changed");failed(original.failure,"cdp_popup_native_request_changed",false);return;}
        allocation=null;allocated=original.pending;allocating=true;
        try{original.ready.ready(source,captured);}
        catch(IOException|RuntimeException|Error error){close("cdp_popup_native_allocation_callback_failed");failed(original.failure,"cdp_popup_native_allocation_callback_failed",true);}
        finally{allocating=false;}
    }
    void adopt(MdoBrowserCdpPopupNative.Created child,int timeout,Consumed ready,Failure failure)throws IOException {
        thread();if(child==null||ready==null||failure==null)throw new IOException("cdp_popup_delivery_arguments_invalid");long end=deadline(timeout);
        if(phase!=READY||!owned())throw new IOException("cdp_popup_native_adoption_unavailable");
        int slot=-1;for(int i=0;i<adoptions.length;i++){if(adoptions[i]!=null&&adoptions[i].child==child)throw new IOException("cdp_popup_native_adoption_replayed");if(adoptions[i]==null)slot=i;}
        if(slot<0)throw new IOException("cdp_popup_native_adoption_limit");if(!source.adoptable(child))throw new IOException("cdp_popup_native_adoption_unavailable");
        AdoptionWait original=new AdoptionWait(child,end,ready,failure);adoptions[slot]=original;
        try{
            original.receipt=source.adopt(child);
            if(!active(original))return;
            MdoBrowserCdpPopupNative.Consumption subscription=source.consume(child,original.receipt,new MdoBrowserCdpPopupNative.ConsumptionEvents(){
                public void consumed(MdoBrowserPopups.Adoption receipt){nativeConsumed(original,receipt);}
                public void failed(String code){if(active(original))close(code);}
            });
            if(active(original))original.subscription=subscription;else subscription.close();
        }catch(IOException|RuntimeException|Error error){
            if(active(original)){removeAdoption(original);close("cdp_popup_native_adoption_unconfirmed");failed(failure,"cdp_popup_native_adoption_unconfirmed",true);}
        }
        schedule();
    }
    private boolean active(AdoptionWait original){for(AdoptionWait row:adoptions)if(row==original)return true;return false;}
    private void removeAdoption(AdoptionWait original){for(int i=0;i<adoptions.length;i++)if(adoptions[i]==original)adoptions[i]=null;if(original.subscription!=null)original.subscription.close();}
    private void nativeConsumed(AdoptionWait original,MdoBrowserPopups.Adoption receipt){
        thread();if(!active(original))return;
        if(phase!=READY||!owned()){close("cdp_popup_preparation_owner_changed");return;}
        if(android.os.SystemClock.elapsedRealtime()>=original.deadline){close("cdp_popup_native_consumption_timed_out");return;}
        if(receipt!=original.receipt||!receipt.ok()||!receipt.nativeConsumed()){close("cdp_popup_native_adoption_unconfirmed");return;}
        removeAdoption(original);schedule();
        try{original.ready.ready(source,original.child,receipt);}
        catch(IOException|RuntimeException|Error error){close("cdp_popup_native_consumption_callback_failed");failed(original.failure,"cdp_popup_native_consumption_callback_failed",true);}
    }
    void prove(MdoBrowserCdpPopupNative.Child child,int timeout,Proven ready,Failure failure)throws IOException {
        thread();if(child==null||ready==null||failure==null)throw new IOException("cdp_popup_delivery_arguments_invalid");long end=deadline(timeout);
        if(phase!=READY||!owned()||request==null)throw new IOException("cdp_popup_native_proof_unavailable");
        int slot=-1;for(int i=0;i<children.length;i++){if(children[i]==child)throw new IOException("cdp_popup_native_proof_replayed");if(children[i]==null)slot=i;}
        if(slot<0)throw new IOException("cdp_popup_native_proof_limit");
        ProofWait original=new ProofWait(end,ready,failure);children[slot]=child;proofs[slot]=original;
        try{if(!request.proveNative(child))throw new IOException("cdp_popup_native_proof_replayed");}
        catch(IOException|RuntimeException|Error error){children[slot]=null;proofs[slot]=null;throw error;}
        schedule();
    }
    private void nativeProof(MdoBrowserCdpPopupNative.Child child){
        thread();int slot=-1;for(int i=0;i<children.length;i++)if(children[i]==child)slot=i;
        if(slot<0)return;ProofWait original=proofs[slot];
        if(phase!=READY||!owned()){close("cdp_popup_preparation_owner_changed");return;}
        if(android.os.SystemClock.elapsedRealtime()>=original.deadline){close("cdp_popup_native_proof_delivery_timed_out");return;}
        MdoBrowserCdpSession.ControlResult result=child.proof();
        if(result==null||!result.ok){close(result==null?"cdp_popup_native_proof_invalid":safe(result.code));return;}
        children[slot]=null;proofs[slot]=null;schedule();
        try{original.ready.ready(child);}
        catch(IOException|RuntimeException|Error error){close("cdp_popup_native_proof_callback_failed");failed(original.failure,"cdp_popup_native_proof_callback_failed",true);}
    }
    private void schedule(){
        boolean proofPending=false;for(ProofWait value:proofs)if(value!=null)proofPending=true;
        boolean adoptionPending=false;for(AdoptionWait value:adoptions)if(value!=null)adoptionPending=true;
        if(allocation==null&&!proofPending&&!adoptionPending){if(watching){handler.removeCallbacks(watch);watching=false;}return;}
        if(watching)return;watching=true;
        try{if(!handler.postDelayed(watch,50)){watching=false;close("cdp_popup_native_delivery_timer_unavailable");}}
        catch(RuntimeException|Error error){watching=false;close("cdp_popup_native_delivery_timer_unavailable");}
    }
    private void tick(){
        thread();watching=false;if(phase!=READY||!owned()){close("cdp_popup_preparation_owner_changed");return;}
        long now=android.os.SystemClock.elapsedRealtime();
        for(ProofWait value:proofs)if(value!=null&&now>=value.deadline){close("cdp_popup_native_proof_delivery_timed_out");return;}
        for(AdoptionWait value:adoptions)if(value!=null){
            if(now>=value.deadline){close("cdp_popup_native_consumption_timed_out");return;}
            if(value.subscription!=null&&!value.subscription.current()){close("cdp_popup_native_consumption_retired");return;}
        }
        drainAllocation();schedule();
    }
    void await(Runnable effect,Failure failure)throws IOException {
        thread();if(effect==null||failure==null)throw new IOException("cdp_arguments_invalid");if(pending!=null||running)throw new IOException("cdp_popup_preparation_busy");
        if(phase!=STARTING&&phase!=READY){failure.failed(code==null?"cdp_popup_preparation_retiring":code,false);return;}
        pending=new Pending(effect,failure);if(phase==READY)runPending();
    }
    private void runPending(){
        if(pending==null)return;if(!owned()){close("cdp_popup_preparation_owner_changed");return;}
        Pending original=pending;pending=null;running=true;
        try{original.effect.run();}catch(RuntimeException|Error error){close("cdp_popup_action_failed");original.failure.failed("cdp_popup_action_failed",true);}finally{running=false;}
    }
    private void state(MdoBrowserCdpPopupWorker.Result result){
        thread();if(result.terminal){phase=TERMINAL;
            if(code==null||java.util.Arrays.asList("cdp_popup_preparation_owner_changed","cdp_popup_preparation_failed","cdp_popup_protection_unavailable").contains(code)&&result.code!=null&&!result.code.endsWith("_cancelled"))code=result.code;
            failPending(code==null?"cdp_popup_preparation_cancelled":code);notifyLost();return;}
        if(!result.ready||phase!=STARTING)return;
        if(!owned()){close("cdp_popup_preparation_owner_changed");return;}phase=READY;everReady=true;runPending();
    }
    private void candidate(MdoBrowserCdpPopupBroker.Candidate candidate){
        thread();if(phase!=READY||!owned()){close("cdp_popup_preparation_owner_changed");return;}
        try{candidate.requestProtection();}catch(IOException|RuntimeException|Error error){close("cdp_popup_preparation_failed");}
    }
    private void protection(MdoBrowserCdpPopupBroker.Candidate candidate){thread();MdoBrowserCdpSession.ControlResult result=candidate.protection();if(result==null||!result.ok)close("cdp_popup_protection_unavailable");else{drainAllocation();schedule();}}
    private void failPending(String reason){
        Pending original=pending;pending=null;AllocationWait waiting=allocation;allocation=null;
        if(watching){handler.removeCallbacks(watch);watching=false;}
        if(original!=null)failed(original.failure,reason,false);if(waiting!=null)failed(waiting.failure,reason,false);
        for(int i=0;i<proofs.length;i++){ProofWait value=proofs[i];children[i]=null;proofs[i]=null;if(value!=null)failed(value.failure,reason,true);}
        for(int i=0;i<adoptions.length;i++){AdoptionWait value=adoptions[i];adoptions[i]=null;if(value!=null){if(value.subscription!=null)value.subscription.close();failed(value.failure,reason,true);}}
    }
    private void notifyLost(){if(everReady&&!notified){notified=true;lost.lost(code==null?"cdp_popup_preparation_retired":code);}}
    /** UI-only original callback evidence; this grants no document execution. */
    MdoBrowserPopups.Pending originalNativePending(){thread();if(phase!=READY||!source.current())return null;MdoBrowserPopups.Pending value=source.pending();return value!=null&&value.current()?value:null;}
    boolean nativeTurnSettled(){
        thread();if(phase!=READY||!source.current()||source.pending()!=null||allocation!=null||allocating||pending!=null||running)return false;
        for(ProofWait value:proofs)if(value!=null)return false;for(AdoptionWait value:adoptions)if(value!=null)return false;return true;
    }
    boolean ready(){thread();if(phase==READY&&!owned())close("cdp_popup_preparation_owner_changed");return phase==READY;}
    boolean settled(){thread();if(phase!=TERMINAL)return false;return request==null?!source.live():request.result()!=null&&request.result().sourceReleased&&source.released();}
    String phase(){thread();return new String[]{"new","preparing","ready","retiring","terminal"}[phase];}
    String failure(){thread();return code;}
    void close(String reason){
        thread();if(phase==TERMINAL)return;if(code==null)code=safe(reason);phase=RETIRING;
        if(request!=null)request.close();else phase=TERMINAL;failPending(code);notifyLost();
    }
    @Override public void close(){close("cdp_popup_preparation_cancelled");}
}
