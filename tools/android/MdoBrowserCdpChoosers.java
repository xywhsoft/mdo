package org.xserver.android;

import java.io.IOException;
import java.util.Arrays;
import java.util.Iterator;
import java.util.UUID;
import java.util.function.LongSupplier;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

/** Worker-only original native chooser events. A CDP event is NOT authority to
 * answer an Android ValueCallback; that original callback must be joined by the
 * UI controller before any future URI delivery. No paths/files/callbacks here. */
final class MdoBrowserCdpChoosers {
    static final class Candidate {
        final String id="nc-"+UUID.randomUUID(),mode;final MdoBrowserCdpFrames.Route route;final int backend;final long born,expires;
        Candidate(MdoBrowserCdpFrames.Route route,int backend,String mode,long now){this.route=route;this.backend=backend;this.mode=mode;born=now;expires=now+120000;}
    }
    static final class Request {
        final String action,chooser,document;final MdoBrowserCdpFrameChain pins;
        private Request(String action,String chooser,String document,MdoBrowserCdpFrameChain pins){this.action=action;this.chooser=chooser;this.document=document;this.pins=pins;}
        static Request parse(JSONObject request)throws IOException {
            String action=request.optString("action","");
            if(!Arrays.asList("chooser_arm","chooser_disarm","chooser_list","chooser_target").contains(action))throw problem("cdp_chooser_request_invalid");
            if(!action.equals("chooser_target"))return new Request(action,null,null,null);
            Object id=request.opt("chooser"),document=request.opt("local_document");
            if(!(id instanceof String)||!((String)id).matches("nc-"+MdoBrowserCdpFrameChain.UUID)||!(document instanceof String)||!((String)document).matches("d-"+MdoBrowserCdpFrameChain.UUID))
                throw problem("cdp_chooser_request_invalid");
            MdoBrowserCdpFrameChain pins=MdoBrowserCdpFrameChain.parse(request);MdoBrowserCdpFrameChain.Pin leaf=pins.chain.get(0);
            if(!leaf.frame.equals(request.opt("frame"))||!leaf.document.equals(request.opt("document")))throw problem("cdp_chooser_request_invalid");
            return new Request(action,(String)id,(String)document,pins);
        }
        JSONObject run(MdoBrowserCdpFrames frames,MdoBrowserCdpWorker.Deadline deadline)throws IOException {
            if(action.equals("chooser_arm"))return frames.armChoosers(deadline);
            if(action.equals("chooser_disarm"))return frames.disarmChoosers(deadline);
            if(action.equals("chooser_list"))return frames.listChoosers(deadline);
            pins.validate(frames,deadline);MdoBrowserCdpFileInput.parents(pins,frames,deadline);
            JSONObject result=frames.chooserTarget(chooser,pins.chain.get(0),document,deadline);
            pins.validate(frames,deadline);MdoBrowserCdpFileInput.parents(pins,frames,deadline);pins.validate(frames,deadline);
            frames.validateChooser(chooser);return result;
        }
    }
    private final MdoBrowserCdpFrames frames;private final LongSupplier clock;
    private boolean requested,arming,seen;private String fault;private Candidate active;
    MdoBrowserCdpChoosers(MdoBrowserCdpFrames frames){this(frames,android.os.SystemClock::elapsedRealtime);}
    MdoBrowserCdpChoosers(MdoBrowserCdpFrames frames,LongSupplier clock){if(frames==null||clock==null)throw new IllegalArgumentException("Missing native chooser owner");this.frames=frames;this.clock=clock;}
    private static IOException problem(String code){return new IOException(code);}
    private IOException fail(String code){fault=code;return problem(code);}
    private void healthy()throws IOException {if(fault!=null)throw problem(fault);}
    boolean requested(){return requested;}
    boolean begin()throws IOException {healthy();if(requested)return false;requested=true;arming=true;seen=false;active=null;return true;}
    void armed()throws IOException {healthy();if(!requested)throw problem("cdp_chooser_not_armed");arming=false;}
    void disable(){requested=false;arming=false;seen=false;active=null;fault=null;}
    void event(String session,JSONObject params)throws IOException {
        if(!requested)return;
        try{
            healthy();if(arming)throw fail("cdp_chooser_arming_event");if(seen)throw fail("cdp_chooser_ambiguous");
            for(Iterator<String> keys=params.keys();keys.hasNext();)if(!Arrays.asList("frameId","backendNodeId","mode").contains(keys.next()))throw fail("cdp_chooser_metadata_invalid");
            Object raw=params.opt("frameId"),backend=params.opt("backendNodeId"),mode=params.opt("mode");
            if(!(raw instanceof String)||!MdoBrowserCdpJson.token((String)raw)||!(backend instanceof Integer)||((Integer)backend)<=0||
                !(mode instanceof String)||!Arrays.asList("selectSingle","selectMultiple").contains(mode))throw fail("cdp_chooser_metadata_invalid");
            MdoBrowserCdpFrames.Route route=frames.eventRoute((String)raw,session);long now=clock.getAsLong();
            if(now>Long.MAX_VALUE-120000)throw fail("cdp_chooser_clock_invalid");
            Candidate pending=new Candidate(route,(Integer)backend,(String)mode,now);seen=true;active=pending;
        }catch(IOException error){String code=error.getMessage();if(!MdoBrowserCdpSession.EventFailure.allowed(code))code="cdp_chooser_frame_unavailable";
            fault=code;throw new MdoBrowserCdpSession.EventFailure(code);}
        catch(RuntimeException error){fault="cdp_chooser_metadata_invalid";throw new MdoBrowserCdpSession.EventFailure(fault);}
    }
    private long remaining(Candidate candidate)throws IOException {
        long now=clock.getAsLong();if(now<candidate.born)throw fail("cdp_chooser_clock_changed");return now>=candidate.expires?0:candidate.expires-now;
    }
    Candidate require(String id)throws IOException {
        healthy();if(!requested||arming)throw problem("cdp_chooser_not_armed");
        Candidate candidate=active;if(candidate==null||!candidate.id.equals(id))throw problem("cdp_chooser_changed");
        if(remaining(candidate)==0)throw problem("cdp_chooser_expired");frames.validate(candidate.route);return candidate;
    }
    void validate(Candidate candidate)throws IOException {if(candidate==null||require(candidate.id)!=candidate)throw problem("cdp_chooser_changed");}
    JSONObject snapshot()throws IOException {
        healthy();try{
            JSONArray rows=new JSONArray();Candidate candidate=active;
            if(requested&&!arming&&candidate!=null&&remaining(candidate)>0){
                validate(candidate);long left=remaining(candidate);if(left>0)rows.put(new JSONObject().put("id",candidate.id).put("frame",candidate.route.frame).put("frame_document",candidate.route.document)
                    .put("mode",candidate.mode).put("multiple",candidate.mode.equals("selectMultiple")).put("remaining_ms",left)
                    .put("frame_scope","native-frame-tree").put("assignment_supported",false));
            }
            return new JSONObject().put("ok",true).put("armed",requested&&!arming).put("events_requested",requested).put("event_verified",rows.length()==1)
                .put("choosers",rows).put("assignment_supported",false);
        }catch(JSONException error){throw problem("cdp_chooser_result_invalid");}
    }
}
