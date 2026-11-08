package org.xserver.android;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Iterator;
import java.util.Set;
import org.json.JSONException;
import org.json.JSONObject;

/** Immutable trusted-host command grammar, never a caller-picked CDP route.
 * The UI controller must apply product/profile/permission admission first. */
final class MdoBrowserCdpFrameRequest {
    final String action,frame,document,script,navigationRef;final int timeout;final boolean effect;private final double x,y;private final MdoBrowserCdpFileInput fileInput;private final MdoBrowserCdpChoosers.Request chooser;
    private MdoBrowserCdpFrameRequest(String action,String frame,String document,String script,int timeout,double x,double y,MdoBrowserCdpFileInput fileInput,MdoBrowserCdpChoosers.Request chooser,String navigationRef){
        this.action=action;this.frame=frame;this.document=document;this.script=script;this.timeout=timeout;this.x=x;this.y=y;effect="evaluate".equals(action)||"file_clear".equals(action);
        this.fileInput=fileInput;this.chooser=chooser;this.navigationRef=navigationRef;
    }
    private static IOException invalid(){return new IOException("cdp_frame_request_invalid");}
    private static String text(Object value,int limit)throws IOException {
        if(!(value instanceof String)||((String)value).isEmpty()||((String)value).length()>limit)throw invalid();String text=(String)value;
        for(int i=0;i<text.length();i++){char c=text.charAt(i);if(c==0)throw invalid();
            if(Character.isHighSurrogate(c)){if(++i>=text.length()||!Character.isLowSurrogate(text.charAt(i)))throw invalid();}
            else if(Character.isLowSurrogate(c))throw invalid();}return text;
    }
    static MdoBrowserCdpFrameRequest parse(JSONObject request)throws IOException {
        if(request==null||!"frames".equals(request.opt("command"))||!Boolean.TRUE.equals(request.opt("agent")))throw invalid();
        String action=text(request.opt("action"),32);boolean chooser=Arrays.asList("chooser_arm","chooser_list","chooser_disarm","chooser_target").contains(action);
        boolean list=action.equals("list")||chooser&&!action.equals("chooser_target"),evaluate=action.equals("evaluate"),owner=action.equals("owner");
        boolean file=action.equals("file_input")||action.equals("file_clear");if(!(list||chooser||evaluate||owner||file||action.equals("upload_policy")||action.equals("capture_owner")))throw invalid();
        Set<String> allowed=new HashSet<>(Arrays.asList("channel","id","browser","command","agent","action","timeout_ms","navigation_ref"));
        if(!list){allowed.add("frame");allowed.add("document");}if(evaluate)allowed.add("script");if(owner)allowed.add("point");
        if(file)allowed.addAll(Arrays.asList("local_document","ref","frame_chain"));
        if(action.equals("chooser_target"))allowed.addAll(Arrays.asList("local_document","chooser","frame_chain"));
        for(Iterator<String> keys=request.keys();keys.hasNext();)if(!allowed.contains(keys.next()))throw invalid();
        if(request.has("channel")&&!"mdo-browser".equals(request.opt("channel")))throw invalid();
        if(request.has("browser"))text(request.opt("browser"),100);
        String uuid="[a-fA-F0-9]{8}-[a-fA-F0-9]{4}-[a-fA-F0-9]{4}-[a-fA-F0-9]{4}-[a-fA-F0-9]{12}";
        String navigationRef=null;
        if(request.has("navigation_ref")){
            navigationRef=text(request.opt("navigation_ref"),39);
            if(!navigationRef.matches("nv-"+uuid)||!(action.equals("list")||evaluate))throw invalid();
        }
        if(request.has("id")&&!text(request.opt("id"),36).matches(uuid))throw invalid();
        int timeout=10000;
        if(request.has("timeout_ms")){
            Object value=request.opt("timeout_ms");if(!(value instanceof Number))throw invalid();double milliseconds=((Number)value).doubleValue();
            if(!Double.isFinite(milliseconds)||milliseconds<=0||milliseconds>60000)throw invalid();
            if(milliseconds<100)throw new IOException("cdp_worker_timeout");timeout=(int)Math.min(15000,Math.floor(milliseconds));
        }
        String frame=null,document=null,script=null;double x=0,y=0;
        if(!list){frame=text(request.opt("frame"),100);document=text(request.opt("document"),100);
            if(!(frame.equals("f0")||frame.matches("f-"+uuid))||!document.matches("fd-"+uuid))throw invalid();}
        if(evaluate){script=text(request.opt("script"),196608);if(script.getBytes(StandardCharsets.UTF_8).length>196608)throw invalid();}
        if(owner){JSONObject point=request.optJSONObject("point");
            if(point==null||point.length()!=2||!(point.opt("x") instanceof Number)||!(point.opt("y") instanceof Number))throw invalid();
            x=((Number)point.opt("x")).doubleValue();y=((Number)point.opt("y")).doubleValue();
            if(!Double.isFinite(x)||!Double.isFinite(y)||x<0||y<0||x>32768||y>32768)throw invalid();}
        return new MdoBrowserCdpFrameRequest(action,frame,document,script,timeout,x,y,file?MdoBrowserCdpFileInput.parse(request):null,chooser?MdoBrowserCdpChoosers.Request.parse(request):null,navigationRef);
    }
    boolean chooserContinuation(){return action.equals("chooser_list")||action.equals("chooser_target")||action.equals("chooser_disarm");}
    byte[] run(MdoBrowserCdpBinding binding,MdoBrowserCdpWorker.Deadline deadline)throws IOException {
        try{
            MdoBrowserCdpFrames frames=binding.frames();JSONObject result;
            if(action.equals("list"))result=frames.inventory(deadline);
            else if(action.equals("evaluate"))result=frames.evaluate(frame,document,script,true,deadline);
            else if(fileInput!=null)result=action.equals("file_clear")?fileInput.clear(frames,deadline):fileInput.inspect(frames,deadline);
            else if(chooser!=null)result=chooser.run(frames,deadline);
            else result=frames.owner(action,frame,document,action.equals("owner")?new JSONObject().put("x",x).put("y",y):null,deadline);
            byte[] bytes=result.toString().getBytes(StandardCharsets.UTF_8);
            if(bytes.length>1048576){Arrays.fill(bytes,(byte)0);throw new IOException("cdp_worker_result_invalid");}return bytes;
        }catch(JSONException error){throw new IOException("cdp_frame_result_invalid");}
    }
    static JSONObject failure(String nativeCode,boolean effect)throws JSONException {
        if(nativeCode==null||!nativeCode.matches("cdp_[a-z_]{1,80}"))nativeCode="cdp_frame_failed";
        String code="frame_unavailable",message="The native frame operation was not confirmed";
        if(nativeCode.equals("cdp_navigation_document_unproven")||nativeCode.equals("cdp_navigation_destination_invalid")||nativeCode.equals("cdp_navigation_digest_unavailable")){code="navigation_unproven";message="The original navigation has no current native document and destination proof";}
        else if(nativeCode.equals("cdp_navigation_document_changed")||nativeCode.equals("cdp_navigation_destination_changed")){code="stale_document";message="The reading connection no longer belongs to the original navigation document and destination";}
        else if(nativeCode.equals("cdp_file_clear_requires_upload_route")){code="unsupported_action";message="Native file clearing requires the dedicated upload.clear mutation and original receipt route";}
        else if(nativeCode.contains("unsupported")){code="unsupported_capability";message="The installed provider does not support this frame operation";}
        else if(nativeCode.contains("timeout")){code="timed_out";message="The native frame operation reached its deadline";}
        else if(nativeCode.contains("cancelled")||nativeCode.contains("owner_changed")){code="operation_cancelled";message="The original browser owner changed or the operation was cancelled";}
        else if(nativeCode.contains("limit")){code="resource_limit";message="The bounded native frame resource limit was reached";}
        else if(nativeCode.equals("cdp_file_input_ancestor_blocked")||nativeCode.equals("cdp_file_input_element_disabled")){code="element_disabled";message="The file input or one of its original iframe ancestors is disabled or inert";}
        else if(nativeCode.equals("cdp_file_input_request_invalid")){code="file_input_request_invalid";message="The native file input document, reference or frame pins are malformed";}
        else if(nativeCode.equals("cdp_file_input_cleanup_failed")){code="file_input_cleanup_failed";message="The original native object cleanup was not confirmed; inspect this browser before continuing";}
        else if(nativeCode.equals("cdp_file_input_node_invalid")){code="upload_target_changed";message="The pinned native element is no longer an original file input";}
        else if(nativeCode.equals("cdp_file_clear_unconfirmed")){code="file_clear_unconfirmed";message="The original file input was not confirmed empty; inspect its receipt and state before another action";}
        else if(nativeCode.equals("cdp_chooser_ambiguous")||nativeCode.equals("cdp_chooser_arming_event")){code="chooser_ambiguous";message="The native chooser cannot be uniquely bound to the original event; cancel and inspect before another selection";}
        else if(nativeCode.equals("cdp_chooser_expired")||nativeCode.equals("cdp_chooser_changed")){code="chooser_changed";message="The original native chooser or its lease is no longer current";}
        else if(nativeCode.contains("stale")||nativeCode.contains("changed")){code="stale_frame";message="The original frame or document changed";}
        else if(nativeCode.equals("cdp_frame_request_invalid")){code="frame_request_invalid";message="The native frame request is malformed";}
        return new JSONObject().put("ok",false).put("error",new JSONObject().put("code",code).put("native_code",nativeCode).put("message",message)
            .put("evaluation_may_have_occurred",effect));
    }
    static JSONObject response(MdoBrowserCdpWorker.Result result)throws JSONException {
        if(result.code!=null)return failure(result.code,result.effectMayHaveOccurred);
        if(result.bytes==null)return failure("cdp_frame_result_invalid",result.effectMayHaveOccurred);
        try{JSONObject value=new JSONObject(new String(result.bytes,StandardCharsets.UTF_8));if(!Boolean.TRUE.equals(value.opt("ok")))return failure("cdp_frame_result_invalid",result.effectMayHaveOccurred);return value;}
        catch(JSONException error){return failure("cdp_frame_result_invalid",result.effectMayHaveOccurred);}
    }
}
