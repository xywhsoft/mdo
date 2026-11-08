package org.xserver.android;

import java.io.IOException;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.LongSupplier;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

/** Native coordinator's fixed worker steps. No JSON command route, supplied
 * script, activation flag, nonce, protocol node or device path is accepted.
 * This class does not answer an Android callback or mint a file permission. */
final class MdoBrowserCdpUpload {
    static final class Ready {
        final String nonce;final boolean multiple;
        private Ready(String nonce,boolean multiple){this.nonce=nonce;this.multiple=multiple;}
    }
    /** Immutable native receipt survives loss of a worker/UI reply. Reading it
     * cannot execute script, replay assignment or restore any file authority. */
    static final class Receipt {
        private final String body;final boolean cleanupConfirmed;
        private Receipt(String body,boolean cleanup){this.body=body;cleanupConfirmed=cleanup;}
        JSONObject value()throws JSONException {return new JSONObject(body).put("cleanup_confirmed",cleanupConfirmed);}
    }
    private final MdoBrowserCdpFileInput target;private final MdoBrowserUploadStore.Spec[] files;
    private final String key="up-"+UUID.randomUUID();private final LongSupplier clock;
    private final long born,expires;private long last;
    private final Thread ui=Thread.currentThread();private final AtomicReference<Thread> worker=new AtomicReference<>();
    private MdoBrowserCdpBinding original;private MdoBrowserCdpFrames frames;private MdoBrowserCdpWorlds.Pin world;
    private volatile String phase="new";private volatile Ready ready;private volatile Receipt receipt;private volatile boolean opening,assignment;
    private boolean helperTouched,cleanupIssued;private String cleanup;
    MdoBrowserCdpUpload(MdoBrowserCdpFileInput target,MdoBrowserUploadStore.Spec[] files,int timeout)throws IOException {
        this(target,files,timeout,android.os.SystemClock::elapsedRealtime);
    }
    MdoBrowserCdpUpload(MdoBrowserCdpFileInput target,MdoBrowserUploadStore.Spec[] files,int timeout,LongSupplier clock)throws IOException {
        if(target==null||clock==null||timeout<1||timeout>120000)throw problem("cdp_upload_request_invalid");
        try{MdoBrowserUploadStore.manifest(files);}catch(IOException error){throw problem("cdp_upload_request_invalid");}
        this.target=target;this.files=files.clone();this.clock=clock;
        born=clock.getAsLong();if(born<0||born>Long.MAX_VALUE-timeout)throw problem("cdp_upload_clock_invalid");last=born;expires=born+timeout;
    }
    private static IOException problem(String code){return new IOException(code);}
    Ready ready(){if(Thread.currentThread()!=ui)throw new IllegalStateException("Private picker metadata requires its native UI owner");return phase.equals("prepared")?ready:null;}
    Receipt receipt(){if(Thread.currentThread()!=ui)throw new IllegalStateException("Upload receipts require their native UI owner");return receipt;}
    boolean openingMayHaveOccurred(){return opening;}
    boolean assignmentMayHaveOccurred(){return assignment;}
    private void thread()throws IOException {
        Thread current=Thread.currentThread();if(current==ui)throw problem("cdp_requires_background_thread");
        worker.compareAndSet(null,current);if(worker.get()!=current)throw problem("cdp_upload_worker_changed");
    }
    private int remaining()throws IOException {
        long now=clock.getAsLong();if(now<last)throw problem("cdp_upload_clock_changed");last=now;
        if(now>=expires)throw problem("cdp_upload_expired");return(int)(expires-now);
    }
    private void exact(MdoBrowserCdpBinding binding)throws IOException {
        thread();if(original==null||binding!=original||binding.frames()!=frames)throw problem("cdp_upload_binding_changed");
    }
    private void guard(MdoBrowserCdpBinding binding,MdoBrowserCdpWorker.Deadline deadline)throws IOException {
        exact(binding);remaining();deadline.remaining();target.pins.validate(frames,deadline);MdoBrowserCdpFileInput.parents(target.pins,frames,deadline);
        if(world!=null)frames.validateWorld(world,deadline);remaining();
    }
    private static String expression(String method,JSONObject params){
        return "(p=>{const b=globalThis.__mdo_browser_use_upload_picker_v1;return b&&b.documentNode===document?b."+method+"(p):{ok:false,error:{code:'upload_picker_runtime_unavailable'}};})("+params.toString()+")";
    }
    private JSONObject parameters()throws JSONException {return new JSONObject().put("key",key);}
    private JSONObject value(JSONObject response)throws IOException {
        JSONObject value=response.optJSONObject("value");
        if(!Boolean.TRUE.equals(response.opt("ok"))||value==null||!(value.opt("ok") instanceof Boolean))throw problem("cdp_upload_result_invalid");
        if(!Boolean.TRUE.equals(value.opt("ok"))){
            JSONObject error=value.optJSONObject("error");String code=error==null?"":error.optString("code","");
            // Canonical helper codes only. No provider/page text enters a reply.
            if(!(code.matches("upload_(picker|target|directory|assignment|arguments|key|budget|resource)[a-z_]{0,64}")||code.equals("stale_document")||code.equals("element_disabled")))
                code="upload_picker_failed";
            throw problem("cdp_"+code);
        }
        return value;
    }
    private JSONObject helper(String method,MdoBrowserCdpWorker.Deadline deadline)throws IOException,JSONException {
        return value(frames.evaluatePinned(world,expression(method,parameters()),method.equals("commit"),deadline));
    }
    private JSONObject step(String state)throws JSONException {return new JSONObject().put("ok",true).put("phase",state);}
    JSONObject prepare(MdoBrowserCdpBinding binding,MdoBrowserCdpWorker.Deadline deadline)throws IOException {
        thread();if(!phase.equals("new"))throw problem("cdp_upload_operation_retired");phase="preparing";
        try{
            original=binding;frames=binding.frames();guard(binding,deadline);
            JSONObject info=target.inspect(frames,deadline).getJSONObject("value").getJSONObject("element");
            boolean multiple=info.getBoolean("multiple");
            if(info.getBoolean("directory"))throw problem("cdp_upload_directory_unsupported");
            if(files.length>1&&!multiple)throw problem("cdp_upload_target_invalid");
            MdoBrowserCdpFrameChain.Pin leaf=target.pins.chain.get(0);world=frames.pinWorld(leaf.frame,leaf.document,deadline);guard(binding,deadline);
            helperTouched=true;
            JSONObject installed=frames.evaluatePinned(world,"("+MdoBrowserCdpUploadScripts.INSTALL+",true)",false,deadline);
            if(!Boolean.TRUE.equals(installed.opt("value")))throw problem("cdp_upload_runtime_invalid");guard(binding,deadline);
            JSONArray manifest=new JSONArray();for(MdoBrowserUploadStore.Spec file:files)manifest.put(new JSONObject().put("name",file.name).put("size",file.size));
            JSONObject params=parameters().put("document",target.document).put("ref",target.ref).put("files",manifest).put("timeout_ms",remaining());
            JSONObject prepared=value(frames.evaluatePinned(world,expression("prepare",params),false,deadline));
            Object nonce=prepared.opt("nonce");
            if(!(nonce instanceof String)||!((String)nonce).matches("application/x-mdo-browseruse-[a-f0-9]{32}")||
                !key.equals(prepared.opt("key"))||!target.document.equals(prepared.opt("document"))||!target.ref.equals(prepared.opt("ref"))||
                !"prepared".equals(prepared.opt("phase"))||!Boolean.FALSE.equals(prepared.opt("dispatched"))||!Boolean.FALSE.equals(prepared.opt("opened")))
                throw problem("cdp_upload_result_invalid");
            JSONObject originalInfo=target.inspect(frames,deadline).getJSONObject("value").getJSONObject("element");
            if(!Boolean.valueOf(multiple).equals(originalInfo.opt("multiple"))||!Boolean.FALSE.equals(originalInfo.opt("directory")))throw problem("cdp_upload_target_changed");
            guard(binding,deadline);ready=new Ready((String)nonce,multiple);phase="prepared";return step(phase);
        }catch(JSONException error){phase="failed";ready=null;throw problem("cdp_upload_result_invalid");}
        catch(IOException|RuntimeException|Error error){phase="failed";ready=null;throw error;}
    }
    JSONObject open(MdoBrowserCdpBinding binding,MdoBrowserCdpWorker.Deadline deadline)throws IOException {
        exact(binding);
        if(!phase.equals("prepared"))throw problem("cdp_upload_operation_retired");
        try{
            guard(binding,deadline);
            // Original event/input target was pinned above. The private picker
            // must not be mistaken for a duplicate original page chooser event.
            frames.disarmChoosers(deadline);guard(binding,deadline);
            opening=true;phase="opening";ready=null;
            JSONObject opened=value(frames.openPrivatePicker(world,key,deadline));guard(binding,deadline);
            if(!key.equals(opened.opt("key"))||!target.document.equals(opened.opt("document"))||!target.ref.equals(opened.opt("ref"))||
                !Boolean.TRUE.equals(opened.opt("opened"))||!Boolean.FALSE.equals(opened.opt("dispatched")))throw problem("cdp_upload_result_invalid");
            phase="opened";return step(phase);
        }catch(JSONException error){phase="failed";ready=null;throw problem("cdp_upload_result_invalid");}
        catch(IOException|RuntimeException|Error error){phase="failed";ready=null;throw error;}
    }
    JSONObject status(MdoBrowserCdpBinding binding,MdoBrowserCdpWorker.Deadline deadline)throws IOException {
        exact(binding);
        if(!phase.equals("opened")&&!phase.equals("selected"))throw problem("cdp_upload_operation_retired");
        try{
            guard(binding,deadline);JSONObject status=helper("status",deadline);guard(binding,deadline);
            if(!key.equals(status.opt("key"))||!target.document.equals(status.opt("document"))||!target.ref.equals(status.opt("ref"))||
                !(status.opt("selected") instanceof Boolean)||!Boolean.TRUE.equals(status.opt("opened"))||!Boolean.FALSE.equals(status.opt("dispatched")))throw problem("cdp_upload_result_invalid");
            boolean selected=status.getBoolean("selected");phase=selected?"selected":"opened";return step(phase).put("selected",selected);
        }catch(JSONException error){phase="failed";throw problem("cdp_upload_result_invalid");}
        catch(IOException|RuntimeException|Error error){phase="failed";throw error;}
    }
    /** The UI coordinator must adopt its original native data grant immediately
     * before submitting this step. A lost reply is a possible effect, not retry. */
    JSONObject commit(MdoBrowserCdpBinding binding,MdoBrowserCdpWorker.Deadline deadline)throws IOException {
        exact(binding);
        if(!phase.equals("selected"))throw problem("cdp_upload_not_selected");
        try{
            guard(binding,deadline);phase="committing";assignment=true;
            JSONObject committed=helper("commit",deadline);guard(binding,deadline);
            if(!key.equals(committed.opt("key"))||!target.document.equals(committed.opt("document"))||!target.ref.equals(committed.opt("ref"))||
                !Boolean.TRUE.equals(committed.opt("accepted"))||!Boolean.TRUE.equals(committed.opt("dispatched"))||!(committed.opt("completed") instanceof Boolean)||
                !Boolean.valueOf(committed.optBoolean("completed")).equals(committed.opt("verified"))||!Boolean.FALSE.equals(committed.opt("events_trusted"))||
                !"file_list_handoff".equals(committed.opt("verification"))||!"native_picker_dom_filelist".equals(committed.opt("input_method")))throw problem("cdp_upload_result_invalid");
            String outcome=committed.optString("outcome","");boolean complete=committed.getBoolean("completed");
            if(!(complete?outcome.equals("file_list_confirmed"):outcome.matches("target_changed_after_(assignment|input|change)")))throw problem("cdp_upload_result_invalid");
            phase=complete?"committed":"partial";
            JSONObject result=step(phase).put("accepted",true).put("dispatched",true).put("completed",complete).put("verified",complete)
                .put("outcome",outcome).put("events_trusted",false).put("verification","file_list_handoff").put("input_method","native_picker_dom_filelist");
            receipt=new Receipt(result.toString(),false);return result;
        }catch(JSONException error){phase="uncertain";throw problem("cdp_upload_result_invalid");}
        catch(IOException|RuntimeException|Error error){phase=assignment?"uncertain":"failed";throw error;}
    }
    JSONObject dispose(MdoBrowserCdpBinding binding,MdoBrowserCdpWorker.Deadline deadline)throws IOException {
        exact(binding);ready=null;
        try{
            if(cleanup!=null)return new JSONObject(cleanup);
            if(cleanupIssued)throw problem("cdp_upload_cleanup_unconfirmed");cleanupIssued=true;
            if(helperTouched){
                // Cleanup ignores transient expiry/disabled ancestors, but can
                // never create/rebind a world or evaluate a replacement DOM.
                deadline.remaining();frames.validateWorld(world,deadline);JSONObject disposed=helper("dispose",deadline);frames.validateWorld(world,deadline);
                if(!key.equals(disposed.opt("key"))||!Boolean.TRUE.equals(disposed.opt("disposed"))||!Boolean.TRUE.equals(disposed.opt("cleanup_confirmed")))throw problem("cdp_upload_cleanup_unconfirmed");
            }
            phase="disposed";JSONObject result=step(phase).put("cleanup_confirmed",true).put("assignment_may_have_occurred",assignment);cleanup=result.toString();
            Receipt saved=receipt;if(saved!=null)receipt=new Receipt(saved.body,true);return result;
        }catch(JSONException error){phase="failed";throw problem("cdp_upload_cleanup_unconfirmed");}
        catch(IOException|RuntimeException|Error error){phase="failed";throw error;}
    }
}
