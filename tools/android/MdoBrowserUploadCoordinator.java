package org.xserver.android;

import java.io.IOException;
import java.lang.ref.WeakReference;
import java.nio.charset.StandardCharsets;
import java.util.function.BooleanSupplier;
import java.util.function.LongSupplier;
import org.json.JSONException;
import org.json.JSONObject;

/** One original UI upload, joining actual file/worker/picker object authority.
 * Native-only: product admission and per-view single-operation publication are
 * the host's responsibility. No callback/frame inference or effect retry. */
final class MdoBrowserUploadCoordinator implements AutoCloseable {
    interface Scheduler {void at(Runnable task,long elapsedDeadline);void remove(Runnable task);}
    interface Completion {void done(Result result);}
    static final class Result {
        final String code,ticket,outcome;final boolean ok,accepted,completed,verified,dispatched,retained,cleanupConfirmed,stagingCleanupConfirmed;
        Result(String code,String ticket,String outcome,boolean accepted,boolean complete,boolean dispatched,boolean retained,boolean clean,boolean staging){
            this.code=code;this.ticket=ticket;this.outcome=outcome;ok=code==null;this.accepted=accepted;completed=complete;verified=complete;
            this.dispatched=dispatched;this.retained=retained;cleanupConfirmed=clean;stagingCleanupConfirmed=staging;
        }
    }
    private enum Step {PREPARE,OPEN,VERIFY,SELECT,COMMIT,CLEANUP}
    private final Thread ui=Thread.currentThread();private final Object claim=new Object();
    private final MdoBrowserUploadQueue files;private final MdoBrowserUploadStore.Scope scope;private final String ticket;
    private final MdoBrowserUploadStore.RetentionOwner dataOwner;private final MdoBrowserCdpWorker worker;private final MdoBrowserCdpMarker marker;
    private final MdoBrowserPrivatePicker picker;private final MdoBrowserCdpFileInput target;
    private final BooleanSupplier current;private final Scheduler scheduler;private final LongSupplier clock;private Completion complete;private final Result fallback;
    private final Runnable expiry,poll,retire;private long last,expires;private int delay=20,index;
    private MdoBrowserUploadStore.Spec[] manifest;private MdoBrowserUploadStore.ReadLease[] readers;private MdoBrowserCdpUpload nativeUpload;
    private MdoBrowserPrivatePicker.Lease lease;private MdoBrowserPrivatePicker.Checked verifier;private MdoBrowserPrivatePicker.Result granted;
    private MdoBrowserUploadStore.Retention retention;
    private boolean started,stopping,finished,cdpBusy,opening,startingPicker,assignmentQueued,retentionPending,sourceClaimed,claimRequested,sourceCleanupPending,cleanupAttempted,stagingClean;
    private volatile boolean nativeClean;private String failure;private Result result;
    MdoBrowserUploadCoordinator(MdoBrowserUploadQueue files,MdoBrowserUploadStore.Scope scope,String ticket,MdoBrowserUploadStore.RetentionOwner dataOwner,
            MdoBrowserCdpWorker worker,MdoBrowserCdpMarker marker,MdoBrowserPrivatePicker picker,MdoBrowserCdpFileInput target,
            BooleanSupplier current,Scheduler scheduler,LongSupplier clock,int timeout,Completion complete)throws IOException {
        if(files==null||scope==null||dataOwner==null||worker==null||marker==null||picker==null||target==null||current==null||scheduler==null||clock==null||complete==null||timeout<100||timeout>120000)
            throw new IOException("upload_coordinator_invalid");
        MdoBrowserUploadQueue.Request.ticket("status",ticket);this.files=files;this.scope=scope;this.ticket=ticket;this.dataOwner=dataOwner;this.worker=worker;
        this.marker=marker;this.picker=picker;this.target=target;this.current=current;this.scheduler=scheduler;this.clock=clock;this.complete=complete;
        long now=clock.getAsLong();if(now<0||now>Long.MAX_VALUE-timeout)throw new IOException("upload_clock_invalid");last=now;expires=now+timeout;
        fallback=new Result("upload_result_unconfirmed",ticket,null,false,false,true,true,false,false);
        WeakReference<MdoBrowserUploadCoordinator> weak=new WeakReference<>(this);
        expiry=()->{MdoBrowserUploadCoordinator value=weak.get();if(value!=null)value.expired();};
        poll=()->{MdoBrowserUploadCoordinator value=weak.get();if(value!=null)value.poll();};
        retire=()->{MdoBrowserUploadCoordinator value=weak.get();if(value!=null)value.finish();};
    }
    private void thread(){if(Thread.currentThread()!=ui)throw new IllegalStateException("Uploads require their original native UI owner");}
    private int remaining()throws IOException {
        long now=clock.getAsLong();if(now<last)throw new IOException("upload_clock_changed");last=now;if(now>=expires)throw new IOException("upload_timed_out");return(int)(expires-now);
    }
    private void live()throws IOException {
        thread();remaining();if(stopping||finished||!dataOwner.live()||!scope.live()||!files.ready()||!marker.lease().current()||!current.getAsBoolean())throw new IOException("upload_owner_changed");
    }
    private static String code(Throwable error){String value=error.getMessage();return value!=null&&value.matches("(?:cdp_|upload_)[a-z_]{1,80}")?value:"upload_coordinator_failed";}
    private void remove(Runnable task){try{scheduler.remove(task);}catch(RuntimeException|LinkageError|OutOfMemoryError ignored){}}
    void start(){
        thread();if(started)throw new IllegalStateException("Upload already started");started=true;
        try{live();scheduler.at(expiry,expires);if(!finished&&!stopping){claimRequested=true;queue(MdoBrowserUploadQueue.Request.claim(ticket,claim,false),this::claimed);}}
        catch(IOException|RuntimeException|LinkageError|OutOfMemoryError error){fail(code(error));}
    }
    private interface FileResult {void done(MdoBrowserUploadQueue.Outcome outcome)throws IOException,JSONException;}
    private void queue(MdoBrowserUploadQueue.Request request,FileResult callback)throws IOException {
        if(!files.submit(scope,request,outcome->{
            try{callback.done(outcome);}catch(IOException|JSONException|RuntimeException|LinkageError|OutOfMemoryError error){fail(code(error));}
        }))throw new IOException("upload_worker_busy");
    }
    private void claimed(MdoBrowserUploadQueue.Outcome outcome)throws IOException {
        if(!outcome.ok()){fail(outcome.code);return;}sourceClaimed=true;
        if(stopping||finished){settle();return;}live();MdoBrowserUploadStore.Snapshot source=outcome.value;
        if(source==null||!source.sealed||source.navigation!=scope.navigation||!ticket.equals(source.ticket)||source.files.length<1||source.files.length>16)throw new IOException("upload_source_invalid");
        expires=Math.min(expires,source.expires);remaining();remove(expiry);scheduler.at(expiry,expires);
        manifest=source.files.clone();readers=new MdoBrowserUploadStore.ReadLease[manifest.length];access();
    }
    private void access()throws IOException {
        live();if(index==readers.length){nativeUpload=new MdoBrowserCdpUpload(target,manifest,remaining(),clock);send(Step.PREPARE);return;}
        final int next=index;queue(MdoBrowserUploadQueue.Request.access(ticket,next),outcome->{
            if(stopping||finished){settle();return;}live();MdoBrowserUploadStore.ReadLease reader=outcome.reader;
            if(!outcome.ok()||reader==null||reader.scope!=scope||!files.owns(reader)||!reader.live(clock.getAsLong())||!reader.file.same(manifest[next]))throw new IOException(outcome.code==null?"upload_source_changed":outcome.code);
            readers[next]=reader;index++;access();
        });
    }
    private JSONObject operate(Step step,MdoBrowserCdpBinding binding,MdoBrowserCdpWorker.Deadline deadline)throws IOException,JSONException {
        switch(step){
            case PREPARE:return nativeUpload.prepare(binding,deadline);
            case OPEN:return nativeUpload.open(binding,deadline);
            case VERIFY:case SELECT:return nativeUpload.status(binding,deadline);
            case COMMIT:return nativeUpload.commit(binding,deadline);
            default:return nativeUpload.dispose(binding,deadline);
        }
    }
    private void send(Step step)throws IOException {
        thread();if(cdpBusy)throw new IOException("upload_operation_busy");if(step!=Step.CLEANUP)live();
        int budget=step==Step.CLEANUP?5000:Math.min(15000,remaining());if(budget<100)throw new IOException("upload_timed_out");
        cdpBusy=true;if(step==Step.COMMIT)assignmentQueued=true;if(step==Step.CLEANUP)cleanupAttempted=true;
        try{worker.submitExisting(marker,budget,step==Step.OPEN||step==Step.COMMIT,(binding,deadline)->{
            try{
                JSONObject value=operate(step,binding,deadline);
                if(step==Step.CLEANUP)nativeClean=Boolean.TRUE.equals(value.opt("cleanup_confirmed"));
                return value.toString().getBytes(StandardCharsets.UTF_8);
            }catch(IOException|JSONException|RuntimeException|LinkageError|OutOfMemoryError error){
                try{nativeClean=Boolean.TRUE.equals(nativeUpload.dispose(binding,deadline).opt("cleanup_confirmed"));}
                catch(IOException|RuntimeException|LinkageError|OutOfMemoryError ignored){nativeClean=false;}
                if(!nativeClean)binding.close();
                try{return new JSONObject().put("ok",false).put("code",code(error)).toString().getBytes(StandardCharsets.UTF_8);}
                catch(JSONException impossible){throw new IOException("cdp_upload_result_invalid");}
            }
        },outcome->{try{received(step,outcome);}catch(IOException|JSONException|RuntimeException|LinkageError|OutOfMemoryError error){fail(code(error));}});}
        catch(IOException|RuntimeException|LinkageError|OutOfMemoryError error){cdpBusy=false;if(step==Step.COMMIT)assignmentQueued=false;throw error;}
    }
    private void received(Step step,MdoBrowserCdpWorker.Result response)throws IOException,JSONException {
        thread();cdpBusy=false;
        if(finished)return;
        if(response.code!=null){fail(response.code);return;}
        if(response.bytes==null){fail("upload_result_unconfirmed");return;}
        JSONObject value=new JSONObject(new String(response.bytes,StandardCharsets.UTF_8));
        if(!Boolean.TRUE.equals(value.opt("ok"))){fail(value.optString("code","upload_result_unconfirmed"));return;}
        if(step==Step.CLEANUP){settle();return;}
        if(stopping){settle();return;}live();
        switch(step){
            case PREPARE:beginPicker();break;
            case OPEN:opening=false;if(verifier!=null)send(Step.VERIFY);break;
            case VERIFY:{MdoBrowserPrivatePicker.Checked checked=verifier;verifier=null;if(checked==null)throw new IOException("upload_picker_validation_changed");checked.done(true);break;}
            case SELECT:if(Boolean.TRUE.equals(value.opt("selected")))retain();else schedulePoll();break;
            case COMMIT:send(Step.CLEANUP);break;
            default:throw new IOException("upload_result_unconfirmed");
        }
    }
    private void beginPicker()throws IOException {
        live();MdoBrowserCdpUpload.Ready ready=nativeUpload.ready();if(ready==null)throw new IOException("upload_picker_prepare_unconfirmed");
        startingPicker=true;
        try{lease=picker.begin(files,scope,readers,()->!stopping&&!finished&&current.getAsBoolean(),this::verify,remaining(),this::granted);
            if(stopping||finished)return;picker.expect(lease,ready.nonce,ready.multiple);
        }finally{startingPicker=false;}
        if(granted!=null){grantReady();return;}opening=true;send(Step.OPEN);
    }
    private void verify(MdoBrowserPrivatePicker.Checked checked){
        thread();if(verifier!=null){checked.done(false);return;}verifier=checked;
        try{live();if(!opening&&!cdpBusy)send(Step.VERIFY);}
        catch(IOException|RuntimeException|LinkageError|OutOfMemoryError error){fail(code(error));}
    }
    private void granted(MdoBrowserPrivatePicker.Result value){
        try{thread();if(granted!=null)return;granted=value;if(!startingPicker)grantReady();}
        catch(IOException|RuntimeException|LinkageError|OutOfMemoryError error){fail(code(error));}
    }
    private void grantReady()throws IOException {
        if(stopping||finished){settle();return;}live();if(!granted.ok||granted.grant==null){fail(granted.code==null?"upload_picker_grant_unconfirmed":granted.code);return;}
        if(cdpBusy)throw new IOException("upload_operation_busy");send(Step.SELECT);
    }
    private void schedulePoll()throws IOException {
        live();long wait=Math.min(delay,remaining());delay=Math.min(400,delay*2);scheduler.at(poll,clock.getAsLong()+wait);
    }
    private void poll(){try{if(!stopping&&!finished)send(Step.SELECT);}catch(IOException|RuntimeException|LinkageError|OutOfMemoryError error){fail(code(error));}}
    private void retain()throws IOException {
        live();if(retentionPending||retention!=null||granted==null||granted.grant==null)throw new IOException("upload_retention_invalid");retentionPending=true;
        try{queue(MdoBrowserUploadQueue.Request.retain(granted.grant,dataOwner),outcome->{
            retentionPending=false;if(outcome.ok())retention=outcome.retention;
            if(stopping||finished){if(retention!=null)retention.abandon();settle();return;}
            live();if(!outcome.ok()||retention==null||!retention.adopt())throw new IOException(outcome.code==null?"upload_retention_unconfirmed":outcome.code);
            send(Step.COMMIT);
        });}catch(IOException|RuntimeException|LinkageError|OutOfMemoryError error){retentionPending=false;throw error;}
    }
    private void expired(){
        if(finished)return;try{remaining();scheduler.at(expiry,expires);}catch(IOException|RuntimeException|LinkageError|OutOfMemoryError error){fail(code(error));}
    }
    private void fail(String code){
        thread();if(finished)return;if(failure==null)failure=code!=null&&code.matches("(?:cdp_|upload_)[a-z_]{1,80}")?code:"upload_coordinator_failed";
        if(!stopping){stopping=true;retentionPending=false;remove(poll);MdoBrowserPrivatePicker.Checked checked=verifier;verifier=null;
            if(checked!=null)try{checked.done(false);}catch(RuntimeException|LinkageError|OutOfMemoryError ignored){}
            if(lease!=null)try{picker.cancel(lease);}catch(RuntimeException|LinkageError|OutOfMemoryError ignored){}
        }
        if(cdpBusy){worker.cancel(marker.lease());if(worker.pending(marker))return;cdpBusy=false;}settle();
    }
    private void settle(){
        thread();if(finished||cdpBusy||retentionPending||sourceCleanupPending)return;
        if(nativeUpload!=null&&!nativeClean&&!cleanupAttempted&&marker.lease().current()){
            try{send(Step.CLEANUP);return;}catch(IOException|RuntimeException|LinkageError|OutOfMemoryError error){if(failure==null)failure=code(error);}
        }
        if(!assignmentQueued){
            if(retention!=null)retention.close();
            if(claimRequested)try{sourceCleanupPending=true;queue(MdoBrowserUploadQueue.Request.claim(ticket,claim,true),outcome->{stagingClean=outcome.ok()&&outcome.value!=null&&outcome.value.cleanupConfirmed;finish();});scheduler.at(retire,clock.getAsLong()+1000);return;}
            catch(IOException|RuntimeException|LinkageError|OutOfMemoryError ignored){sourceCleanupPending=false;}
        }
        finish();
    }
    private void finish(){
        thread();if(finished)return;finished=true;remove(expiry);remove(poll);remove(retire);
        try{result=snapshot();}catch(RuntimeException|LinkageError|OutOfMemoryError error){result=fallback;}Completion callback=complete;complete=null;
        if(callback!=null)try{callback.done(result);}catch(RuntimeException|LinkageError|OutOfMemoryError ignored){/* Native receipt/retained data remain available; no effect replay. */}
    }
    private Result snapshot(){
        boolean accepted=false,completed=false;String outcome=null;
        try{MdoBrowserCdpUpload.Receipt receipt=nativeUpload==null?null:nativeUpload.receipt();if(receipt!=null){JSONObject value=receipt.value();accepted=value.optBoolean("accepted");completed=value.optBoolean("completed");outcome=value.optString("outcome",null);}}
        catch(JSONException|RuntimeException|LinkageError|OutOfMemoryError ignored){}
        boolean retained=retention!=null&&retention.adopted()&&dataOwner.live(),clean=false;
        try{retained=retained&&retention.live(clock.getAsLong());clean=(nativeUpload==null||nativeClean)&&picker.clean();}
        catch(RuntimeException|LinkageError|OutOfMemoryError ignored){}
        return new Result(failure,ticket,outcome,accepted,failure==null&&completed,assignmentQueued,retained,clean,stagingClean);
    }
    boolean done(){thread();return finished;}
    Result result(){thread();if(!finished)return null;try{return snapshot();}catch(RuntimeException|LinkageError|OutOfMemoryError error){return result==null?fallback:result;}}
    MdoBrowserUploadStore.Retention retained(){thread();return retention;}
    @Override public void close(){thread();if(started&&!finished)fail("upload_cancelled");}
}
