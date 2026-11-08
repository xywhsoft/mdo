package org.xserver.android;

import java.io.IOException;
import java.lang.ref.WeakReference;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import java.util.function.BooleanSupplier;
import java.util.function.LongSupplier;

/** UI-only rendezvous with the ORIGINAL native callback. No URL/timing/active
 * frame joins, raw URI authority, serialized permits or automatic retries.
 * The future DOM controller must supply a native frame/world verifier and keep
 * its own lease alive through that check; this class cannot infer that proof. */
final class MdoBrowserPrivatePicker implements AutoCloseable {
    interface Scheduler {long now();void at(Runnable task,long elapsedDeadline);void remove(Runnable task);}
    interface Checked {void done(boolean current);}
    interface Verify {void check(Checked done);}
    interface Response {void send(MdoBrowserUploadStore.ReadLease[] files,BooleanSupplier current);}
    interface Completion {void done(Result value);}
    static final String PREFIX="application/x-mdo-browseruse-";
    static final class CancelledGrant extends RuntimeException {
        final String code;private CancelledGrant(boolean ownerChanged){super("Native file grant was cancelled");code=ownerChanged?"upload_picker_owner_changed":"upload_picker_grant_unconfirmed";}
    }
    static CancelledGrant cancelledGrant(boolean ownerChanged){return new CancelledGrant(ownerChanged);}
    static final class Result {
        final boolean ok,callbackAnswered,filesGranted,effectMayHaveOccurred,cleanupConfirmed;final String code;
        final MdoBrowserUploadStore.PickerGrant grant;
        Result(boolean ok,String code,boolean answered,boolean granted,boolean effect,boolean clean){
            this(ok,code,answered,granted,effect,clean,null);
        }
        Result(boolean ok,String code,boolean answered,boolean granted,boolean effect,boolean clean,MdoBrowserUploadStore.PickerGrant grant){
            this.ok=ok;this.code=code;callbackAnswered=answered;filesGranted=granted;effectMayHaveOccurred=effect;cleanupConfirmed=clean;this.grant=grant;
        }
    }
    static final class Lease {
        final String key="native-picker-"+UUID.randomUUID();final long expires;
        private final Object claim=new Object();private final MdoBrowserUploadQueue queue;private final MdoBrowserUploadStore.Scope scope;
        private final MdoBrowserUploadStore.ReadLease[] files;private final long born;private long last;
        private String nonce;private int mode=-1,phase;private boolean cancelled,attempted,submitting,checked,verdict;private Runnable timer;
        private BooleanSupplier current;private Verify verify;private Completion complete;private Response response;private Result result;
        private Lease(MdoBrowserUploadQueue queue,MdoBrowserUploadStore.Scope scope,MdoBrowserUploadStore.ReadLease[] files,
                      BooleanSupplier current,Verify verify,Completion complete,long born,long expires){
            this.queue=queue;this.scope=scope;this.files=files;this.current=current;this.verify=verify;this.complete=complete;
            this.born=born;last=born;this.expires=expires;
        }
    }
    private static final class Expiry implements Runnable {
        final WeakReference<MdoBrowserPrivatePicker> owner;final WeakReference<Lease> lease;
        Expiry(MdoBrowserPrivatePicker owner,Lease lease){this.owner=new WeakReference<>(owner);this.lease=new WeakReference<>(lease);}
        public void run(){MdoBrowserPrivatePicker value=owner.get();Lease original=lease.get();if(value!=null&&original!=null)value.expire(original);}
    }
    private final Object view;private final MdoBrowserChoosers ledger;private final Scheduler scheduler;
    private final BooleanSupplier owned;private final LongSupplier navigation;private final Thread thread=Thread.currentThread();
    private final List<Lease> leases=new ArrayList<>();private Lease active;private boolean closed,destroyed,failed;
    MdoBrowserPrivatePicker(Object view,MdoBrowserChoosers ledger,Scheduler scheduler,BooleanSupplier owned,LongSupplier navigation){
        if(view==null||ledger==null||scheduler==null||owned==null||navigation==null)throw new IllegalArgumentException("Missing private native picker owner");
        this.view=view;this.ledger=ledger;this.scheduler=scheduler;this.owned=owned;this.navigation=navigation;
    }
    private void thread(){if(Thread.currentThread()!=thread)throw new IllegalStateException("Private pickers require their original UI thread");}
    private static IOException failure(String code){return new IOException(code);}
    private void poison(){if(!destroyed){failed=true;ledger.rejectionFailed();}}
    private boolean original(Lease lease){return lease!=null&&leases.contains(lease);}
    private boolean live(Lease lease){
        if(closed||destroyed||failed||active!=lease||lease.cancelled||lease.current==null||!ledger.clean())return false;
        try{long now=scheduler.now();if(now<lease.last||now>=lease.expires)return false;lease.last=now;
            if(!owned.getAsBoolean()||navigation.getAsLong()!=lease.scope.navigation||!lease.current.getAsBoolean()||!lease.scope.live()||!lease.queue.ready())return false;
            for(MdoBrowserUploadStore.ReadLease file:lease.files)if(!file.live(now)||!lease.queue.owns(file)||file.scope!=lease.scope)return false;
            return true;
        }catch(RuntimeException|OutOfMemoryError error){return false;}
    }
    Lease begin(MdoBrowserUploadQueue queue,MdoBrowserUploadStore.Scope scope,MdoBrowserUploadStore.ReadLease[] readers,
                BooleanSupplier current,Verify verify,int timeout,Completion complete)throws IOException {
        thread();if(closed||destroyed)throw failure("upload_picker_closed");if(failed||!ledger.clean())throw failure("upload_picker_cleanup_failed");
        if(active!=null||!ledger.ready())throw failure("upload_picker_busy");if(leases.size()>=64)throw failure("upload_picker_resource_limit");
        if(queue==null||scope==null||readers==null||readers.length<1||readers.length>16||current==null||verify==null||complete==null||timeout<1||timeout>120000)
            throw failure("upload_picker_arguments_invalid");
        long now=scheduler.now(),expires=now+timeout,total=0;
        if(now<0||now>Long.MAX_VALUE-timeout||!owned.getAsBoolean()||navigation.getAsLong()!=scope.navigation||!scope.live()||!current.getAsBoolean()||!queue.ready())
            throw failure("upload_picker_owner_changed");
        MdoBrowserUploadStore.ReadLease[] files=readers.clone();
        for(int i=0;i<files.length;i++){
            MdoBrowserUploadStore.ReadLease file=files[i];if(file==null||file.scope!=scope||!queue.owns(file)||!file.live(now))throw failure("upload_picker_files_invalid");
            for(int j=0;j<i;j++)if(files[j]==file)throw failure("upload_picker_files_invalid");
            total+=file.file.size;expires=Math.min(expires,file.expires);
        }
        if(total>67108864L)throw failure("upload_picker_resource_limit");
        Lease lease=new Lease(queue,scope,files,current,verify,complete,now,expires);lease.timer=new Expiry(this,lease);
        leases.add(lease);int claimed=0;
        try{for(;claimed<files.length;claimed++)if(!files[claimed].claimPicker(lease.claim,now))throw failure("upload_picker_files_claimed");}
        catch(IOException|RuntimeException|Error error){for(int i=0;i<claimed;i++)files[i].revoke();leases.remove(lease);throw error;}
        active=lease;
        try{scheduler.at(lease.timer,lease.expires);if(active==lease&&!live(lease))abort(lease,"upload_picker_owner_changed");}
        catch(RuntimeException|OutOfMemoryError error){abort(lease,"upload_picker_scheduler_failed");}
        return lease;
    }
    void expect(Lease lease,String nonce,boolean multiple)throws IOException {
        thread();if(!original(lease)||!live(lease))throw failure("upload_picker_owner_changed");
        if(nonce==null||!nonce.matches(PREFIX+"[a-f0-9]{32}")||!multiple&&lease.files.length!=1)throw failure("upload_picker_arguments_invalid");
        if(lease.phase!=0){if(lease.phase==1&&lease.nonce.equals(nonce)&&lease.mode==(multiple?1:0))return;throw failure("upload_picker_identity_conflict");}
        lease.nonce=nonce;lease.mode=multiple?1:0;lease.phase=1;
    }
    boolean handles(String[] types){
        thread();if(active!=null)return true;
        if(types!=null)for(int i=0;i<Math.min(types.length,16);i++)if(types[i]!=null&&types[i].trim().startsWith(PREFIX))return true;
        return false;
    }
    private boolean reject(Response response){
        try{response.send(null,()->false);return true;}
        catch(RuntimeException|OutOfMemoryError error){poison();return false;}
    }
    boolean rejectMetadata(Object source,Object token,Response response){
        thread();if(source!=view||token==null||response==null||destroyed)return false;if(ledger.known(token))return true;
        if(!ledger.claimPrivate(token)){if(active!=null)abort(active,"upload_picker_callback_limit");return false;}
        Lease lease=active;
        if(lease!=null&&lease.phase<2){lease.response=response;abort(lease,"upload_picker_callback_invalid");}
        else{reject(response);if(active!=null)abort(active,"upload_picker_callback_invalid");}
        return true;
    }
    boolean open(Object source,Object token,Response response,int mode,String[] types,boolean capture){
        thread();if(source!=view||token==null||response==null||destroyed)return false;if(ledger.known(token))return true;
        if(!handles(types))return false;
        if(!ledger.claimPrivate(token)){if(active!=null)abort(active,"upload_picker_callback_limit");return false;}
        Lease lease=active;
        if(lease==null){reject(response);return true;}
        if(lease.phase!=1||capture||mode!=lease.mode||types==null||types.length!=1||!lease.nonce.equals(types[0])||!live(lease)){
            reject(response);abort(lease,lease.phase==2?"upload_picker_callback_ambiguous":"upload_picker_callback_mismatch");return true;
        }
        lease.response=response;lease.phase=2;
        lease.submitting=true;
        try{
            WeakReference<MdoBrowserPrivatePicker> weak=new WeakReference<>(this);WeakReference<Lease> ticket=new WeakReference<>(lease);
            lease.verify.check(current->{MdoBrowserPrivatePicker original=weak.get();Lease exact=ticket.get();if(original!=null&&exact!=null)original.checked(exact,current);});
            lease.submitting=false;if(active==lease&&lease.phase==2&&lease.checked)verified(lease,lease.verdict);
        }catch(RuntimeException|OutOfMemoryError error){lease.submitting=false;if(active==lease&&lease.phase==2)abort(lease,"upload_picker_validation_failed");}
        return true;
    }
    private void checked(Lease lease,boolean current){
        thread();if(active!=lease||lease.phase!=2||lease.checked)return;lease.checked=true;lease.verdict=current;
        // A synchronous success followed by verifier failure must not deliver
        // files before check() itself returns successfully.
        if(!lease.submitting)verified(lease,current);
    }
    private void verified(Lease lease,boolean current){
        thread();if(active!=lease||lease.phase!=2)return;
        if(!current||!live(lease)){abort(lease,"upload_picker_target_changed");return;}
        MdoBrowserUploadStore.ReadLease[] files;
        try{files=lease.files.clone();}catch(OutOfMemoryError error){abort(lease,"upload_picker_allocation_failed");return;}
        Response response=lease.response;lease.response=null;lease.phase=3;lease.attempted=true;boolean answered=false;
        try{response.send(files,()->live(lease));answered=true;
            if(!live(lease)){revoke(lease);retire(lease,new Result(false,"upload_picker_owner_changed",true,false,true,true));return;}
            MdoBrowserUploadStore.PickerGrant grant=MdoBrowserUploadStore.pickerGrant(lease.files,lease.claim,scheduler.now());
            retire(lease,new Result(true,null,true,true,true,true,grant));
        }catch(CancelledGrant error){revoke(lease);retire(lease,new Result(false,error.code,true,false,false,!failed));}
        catch(IOException error){revoke(lease);retire(lease,new Result(false,"upload_picker_owner_changed",answered,false,true,!failed));}
        catch(RuntimeException|OutOfMemoryError error){revoke(lease);poison();retire(lease,new Result(false,"upload_picker_response_uncertain",answered,false,true,false));}
    }
    private static void revoke(Lease lease){for(MdoBrowserUploadStore.ReadLease file:lease.files)file.revoke();}
    private void retire(Lease lease,Result result){
        if(lease.phase==4)return;lease.phase=4;lease.result=result;
        try{scheduler.remove(lease.timer);}catch(RuntimeException|OutOfMemoryError ignored){/* Weak original-ID timer is inert after retirement. */}
        Completion complete=lease.complete;lease.complete=null;lease.current=null;lease.verify=null;lease.response=null;lease.nonce=null;
        if(active==lease)active=null;
        if(complete!=null)try{complete.done(result);}catch(RuntimeException|OutOfMemoryError error){revoke(lease);poison();lease.result=new Result(false,"upload_picker_completion_failed",result.callbackAnswered,false,result.effectMayHaveOccurred,false);}
    }
    private void abort(Lease lease,String code){
        if(!original(lease))return;lease.cancelled=true;
        if(lease.phase==4){for(MdoBrowserUploadStore.ReadLease file:lease.files)file.revokeStaging();}
        else revoke(lease);
        if(lease.phase==3)return; // Native response may reenter close/cancel. Never answer it again.
        if(lease.phase==4){if(lease.result.ok)lease.result=new Result(false,code,lease.result.callbackAnswered,false,lease.result.effectMayHaveOccurred,lease.result.cleanupConfirmed);return;}
        Response response=lease.response;lease.response=null;
        // Cancelling the original callback is also a native send. Keep the
        // completion behind its acknowledgement, including reentrant teardown.
        if(response!=null)lease.phase=3;
        boolean answered=response!=null&&reject(response);
        retire(lease,new Result(false,code,answered,false,lease.attempted, !failed));
    }
    private void expire(Lease lease){thread();if(active!=lease||lease.phase==4)return;
        if(live(lease))try{scheduler.at(lease.timer,lease.expires);return;}catch(RuntimeException|OutOfMemoryError ignored){}
        abort(lease,"upload_picker_expired");
    }
    Result status(Lease lease){thread();if(!original(lease))throw new IllegalArgumentException("Unknown native picker lease");
        if(active==lease&&!live(lease))abort(lease,"upload_picker_owner_changed");return lease.result;}
    boolean pending(){thread();return active!=null;}
    boolean clean(){thread();return !failed&&ledger.clean();}
    void cancel(Lease lease){thread();abort(lease,"upload_picker_cancelled");}
    void cancel(){thread();if(active!=null)abort(active,"upload_picker_cancelled");}
    void metadataFailed(){thread();if(active!=null)abort(active,"upload_picker_callback_invalid");}
    MdoBrowserUploadStore.Spec[] manifest(Lease lease)throws IOException {thread();if(!original(lease)||!live(lease))throw failure("upload_picker_owner_changed");
        MdoBrowserUploadStore.Spec[] specs=new MdoBrowserUploadStore.Spec[lease.files.length];for(int i=0;i<specs.length;i++)specs[i]=lease.files[i].file;return specs;}
    int remaining(Lease lease)throws IOException {thread();if(!original(lease)||!live(lease))throw failure("upload_picker_owner_changed");return(int)Math.max(0,lease.expires-lease.last);}
    @Override public void close(){thread();if(closed)return;closed=true;for(int i=0;i<leases.size();i++){abort(leases.get(i),"upload_picker_closed");if(destroyed)return;}}
    void viewDestroyed(Object original){thread();if(original!=view||!closed)throw new IllegalStateException("Close the exact private picker view first");
        if(destroyed)return;destroyed=true;for(Lease lease:leases)revoke(lease);active=null;leases.clear();failed=false;}
}
