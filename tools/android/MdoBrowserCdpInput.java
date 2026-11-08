package org.xserver.android;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Iterator;
import java.util.Set;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

/** UI-owned immutable input plus private-worker DOM/ancestry guards. All actual
 * input uses the existing owned-WebView sequencer. No CDP input or script from
 * the caller, no root/default-world fallback and no automatic input replay. */
final class MdoBrowserCdpInput {
    interface Current {String rejection();}
    static final class Layout {
        final int width,height,scrollX,scrollY;final double scale;
        Layout(int width,int height,int scrollX,int scrollY,double scale)throws IOException {
            if(width<1||height<1||width>16384||height>16384||!Double.isFinite(scale)||scale<=0||scale>32)throw problem("cdp_input_layout_invalid");
            this.width=width;this.height=height;this.scrollX=scrollX;this.scrollY=scrollY;this.scale=scale;
        }
        boolean matches(Layout other){return other!=null&&width==other.width&&height==other.height&&scrollX==other.scrollX&&scrollY==other.scrollY&&scale==other.scale;}
    }
    // Fixed scripts only. Native routes select the realm and fence documents.
    static final String ROOT="(p=>{try{const r=globalThis.__mdo_browser_use_local_frame_v1,v=visualViewport;"+
        "if(self!==top||!r||r.documentNode!==document)return {ok:false,code:'frame_changed'};"+
        "if(!v||![innerWidth,innerHeight,scrollX,scrollY,v.width,v.height,v.scale,v.offsetLeft,v.offsetTop].every(Number.isFinite)||v.scale<=0||"+
        "Math.abs(v.width-innerWidth)>1||Math.abs(v.height-innerHeight)>1||Math.abs(v.offsetLeft)>.01||Math.abs(v.offsetTop)>.01)return {ok:false,code:'input_viewport_unsupported'};"+
        "return Math.abs(innerWidth*p.scale-p.width)>2||Math.abs(innerHeight*p.scale-p.height)>2||Math.abs(scrollX*p.scale-p.scrollX)>1||Math.abs(scrollY*p.scale-p.scrollY)>1?"+
        "{ok:false,code:'input_viewport_changed'}:{ok:true};}catch{return {ok:false,code:'input_guard_failed'}}})";
    static final String TARGET="(p=>{try{const bad=code=>({ok:false,code}),r=globalThis.__mdo_browser_use_local_frame_v1;"+
        "if(!r||r.documentNode!==document||r.document!==p.document)return bad('frame_changed');"+
        "if(p.kind==='wheel'){const hit=r.scrollPoint({document:p.document,ref:p.ref,point:p.point});"+
        "return hit.ok&&hit.hit_ref===p.ref?{ok:true,point:hit.point}:bad('input_target_changed');}"+
        "const options={document:p.document,target:{ref:p.ref},op:'key'},found=r.locate(options);"+
        "if(!found.ok||found.element.ref!==p.ref||found.element.frame!=='f0')return bad('input_target_changed');"+
        "if(['focus','key','text'].includes(p.kind)){const checked=p.kind==='focus'?r.focus(options):r.verify(options);"+
        "if(!checked.ok||!checked.focused||!document.hasFocus())return bad('input_focus_lost');}"+
        "const prepared=r.prepare({...options,op:p.kind==='move'?'hover':p.kind==='click'?'click':'key',scroll:false});"+
        "if(!prepared.ok||prepared.document!==p.document||prepared.element.ref!==p.ref)return bad('input_target_changed');"+
        "if(p.kind==='click'||p.kind==='move'){const hit=r.visualPoint({document:p.document,point:prepared.point});"+
        "if(!hit.ok||hit.frame_ref!==null)return bad('input_target_changed');}"+
        "if(r.documentNode!==document||r.document!==p.document)return bad('frame_changed');return {ok:true,point:prepared.point};"+
        "}catch{return {ok:false,code:'input_guard_failed'}}})";
    final String kind,document,ref,text,key;final int modifiers;final double x,y,dx,dy;final Layout layout;final MdoBrowserCdpFrameChain frames;
    private boolean pending;private String refusal;
    private MdoBrowserCdpInput(String kind,String document,String ref,String text,String key,int modifiers,double x,double y,double dx,double dy,Layout layout,MdoBrowserCdpFrameChain frames){
        this.kind=kind;this.document=document;this.ref=ref;this.text=text;this.key=key;this.modifiers=modifiers;this.x=x;this.y=y;this.dx=dx;this.dy=dy;this.layout=layout;this.frames=frames;
    }
    private static IOException problem(String code){return new IOException(code);}
    private static String string(Object value,int max)throws IOException {
        if(!(value instanceof String)||((String)value).length()>max)throw problem("cdp_input_request_invalid");String text=(String)value;
        for(int i=0;i<text.length();i++){char c=text.charAt(i);if(c==0)throw problem("cdp_input_request_invalid");
            if(Character.isHighSurrogate(c)){if(++i>=text.length()||!Character.isLowSurrogate(text.charAt(i)))throw problem("cdp_input_request_invalid");}
            else if(Character.isLowSurrogate(c))throw problem("cdp_input_request_invalid");}return text;
    }
    private static double number(Object value)throws IOException {
        if(!(value instanceof Number)||!Double.isFinite(((Number)value).doubleValue()))throw problem("cdp_input_request_invalid");return((Number)value).doubleValue();
    }
    static MdoBrowserCdpInput parse(JSONObject request,Layout layout)throws IOException {
        if(request==null||layout==null||!"input".equals(request.opt("command"))||!Boolean.TRUE.equals(request.opt("agent")))throw problem("cdp_input_request_invalid");
        String kind=string(request.opt("kind"),16);if(!kind.matches("focus|click|move|wheel|key|text"))throw problem("cdp_input_kind_unsupported");
        Set<String> allowed=new HashSet<>(Arrays.asList("channel","id","browser","command","agent","navigation","kind","input_document","input_ref","frame_chain","frame_snapshot"));
        boolean point=kind.matches("click|move|wheel"),keyboard=kind.matches("focus|key|text");
        if(point)allowed.addAll(Arrays.asList("x","y","dx","dy","button","click_count"));
        if(keyboard)allowed.addAll(Arrays.asList("focus_document","focus_ref"));if(kind.equals("key"))allowed.add("key");if(kind.equals("text"))allowed.add("text");
        for(Iterator<String> keys=request.keys();keys.hasNext();)if(!allowed.contains(keys.next()))throw problem("cdp_input_request_invalid");
        if(request.has("channel")&&!"mdo-browser".equals(request.opt("channel")))throw problem("cdp_input_request_invalid");
        if(request.has("id")&&!string(request.opt("id"),36).matches(MdoBrowserCdpFrameChain.UUID))throw problem("cdp_input_request_invalid");
        if(request.has("browser")&&string(request.opt("browser"),100).isEmpty())throw problem("cdp_input_request_invalid");
        double navigation=number(request.opt("navigation"));if(navigation<1||navigation>9007199254740991d||navigation!=Math.floor(navigation))throw problem("cdp_input_request_invalid");
        String document=string(request.opt("input_document"),38),ref=string(request.opt("input_ref"),14);
        if(!document.matches("d-"+MdoBrowserCdpFrameChain.UUID)||!ref.matches("e[1-9][0-9]{0,12}"))throw problem("cdp_input_request_invalid");
        if(keyboard&&(!document.equals(request.opt("focus_document"))||!ref.equals(request.opt("focus_ref"))))throw problem("cdp_input_focus_mismatch");
        double x=0,y=0,dx=0,dy=0;
        if(point){x=number(request.opt("x"));y=number(request.opt("y"));
            if(x<0||y<0||x>32768||y>32768||x*layout.scale>=layout.width||y*layout.scale>=layout.height)throw problem("cdp_input_coordinate_invalid");
            if(request.has("dx"))dx=number(request.opt("dx"));if(request.has("dy"))dy=number(request.opt("dy"));
            if(Math.abs(dx)>32768||Math.abs(dy)>32768)throw problem("cdp_input_scroll_invalid");
            if(request.has("button")&&!"left".equals(request.opt("button"))||request.has("click_count")&&number(request.opt("click_count"))!=1)throw problem("cdp_input_kind_unsupported");
        }
        String text=null,key=null;int modifiers=0;
        if(kind.equals("text")){text=string(request.opt("text"),8192);if(text.getBytes(StandardCharsets.UTF_8).length>16384)throw problem("cdp_input_request_invalid");}
        if(kind.equals("key")){key=string(request.opt("key"),32);
            if(key.startsWith("Control+")){modifiers=2;key=key.substring(8);if(!key.matches("A|Home|End|ArrowUp|ArrowDown|Space"))throw problem("cdp_input_key_invalid");}
            else if(key.equals("Shift+Tab")){key="Tab";modifiers=1;}
            if(!key.matches("[A-Z0-9]|Enter|Tab|Backspace|Delete|Escape|Space|Arrow(Left|Right|Up|Down)|Home|End|Page(Up|Down)"))throw problem("cdp_input_key_invalid");
        }
        return new MdoBrowserCdpInput(kind,document,ref,text,key,modifiers,x,y,dx,dy,layout,MdoBrowserCdpFrameChain.parse(request));
    }
    private JSONObject evaluate(MdoBrowserCdpFrames graph,MdoBrowserCdpFrameChain.Pin pin,String expression,boolean effect,MdoBrowserCdpWorker.Deadline deadline)throws IOException,JSONException {
        JSONObject reply=graph.evaluate(pin.frame,pin.document,expression,effect,deadline),value=reply.optJSONObject("value");
        if(value==null||!Boolean.TRUE.equals(value.opt("ok"))){String code=value==null?null:value.optString("code",null);
            throw problem(code!=null&&code.matches("[a-z_]{1,50}")?"cdp_"+code:"cdp_input_guard_failed");}return value;
    }
    private static JSONObject position(Object value)throws IOException,JSONException {
        if(!(value instanceof JSONObject))throw problem("cdp_input_geometry_invalid");JSONObject point=(JSONObject)value;
        double x=number(point.opt("x")),y=number(point.opt("y"));if(x<0||y<0||x>32768||y>32768)throw problem("cdp_input_geometry_invalid");return new JSONObject().put("x",x).put("y",y);
    }
    private JSONObject owner(MdoBrowserCdpFrames graph,int index,String action,JSONObject point,MdoBrowserCdpWorker.Deadline deadline)throws IOException,JSONException {
        MdoBrowserCdpFrameChain.Pin child=frames.chain.get(index),parent=frames.chain.get(index+1);
        JSONObject reply=graph.owner(action,child.frame,child.document,point,deadline),value=reply.optJSONObject("value");
        if(!parent.frame.equals(reply.opt("parent"))||!parent.document.equals(reply.opt("parent_document"))||value==null||!Boolean.TRUE.equals(value.opt("ok")))throw problem("cdp_input_owner_changed");return value;
    }
    byte[] fence(MdoBrowserCdpBinding binding,MdoBrowserCdpWorker.Deadline deadline)throws IOException {
        try{
            MdoBrowserCdpFrames graph=binding.frames();frames.validate(graph,deadline);
            MdoBrowserCdpFrameChain.Pin root=frames.chain.get(frames.chain.size()-1),leaf=frames.chain.get(0);
            JSONObject nativeLayout=new JSONObject().put("width",layout.width).put("height",layout.height).put("scrollX",layout.scrollX).put("scrollY",layout.scrollY).put("scale",layout.scale);
            String rootExpression=ROOT+"("+nativeLayout.toString()+")";evaluate(graph,root,rootExpression,false,deadline);
            JSONObject witness=new JSONObject().put("document",document).put("ref",ref).put("kind",kind);
            if(kind.equals("wheel")){
                JSONObject point=new JSONObject().put("x",x).put("y",y);
                for(int i=frames.chain.size()-2;i>=0;i--){JSONObject content=owner(graph,i,"capture_owner",null,deadline).optJSONObject("content");
                    if(content==null)throw problem("cdp_input_geometry_invalid");double left=number(content.opt("x")),top=number(content.opt("y")),width=number(content.opt("width")),height=number(content.opt("height"));
                    double px=point.getDouble("x")-left,py=point.getDouble("y")-top;if(width<=0||height<=0||px<0||py<0||px>=width||py>=height)throw problem("cdp_input_owner_changed");point=new JSONObject().put("x",px).put("y",py);}
                witness.put("point",point);
            }
            JSONObject local=evaluate(graph,leaf,TARGET+"("+witness.toString()+")",kind.equals("focus"),deadline),point=position(local.opt("point"));
            for(int i=0;i<frames.chain.size()-1;i++){
                JSONObject parent=owner(graph,i,"owner",point,deadline);point=position(parent.opt("point"));
                if(kind.matches("focus|key|text")&&!Boolean.TRUE.equals(parent.opt("focused")))throw problem("cdp_input_focus_lost");
            }
            if(kind.matches("click|move|wheel")&&(Math.abs(point.getDouble("x")-x)>.5||Math.abs(point.getDouble("y")-y)>.5))throw problem("cdp_input_target_changed");
            if(point.getDouble("x")*layout.scale>=layout.width||point.getDouble("y")*layout.scale>=layout.height)throw problem("cdp_input_viewport_changed");
            evaluate(graph,root,rootExpression,false,deadline);frames.validate(graph,deadline);
            return "{\"ok\":true}".getBytes(StandardCharsets.UTF_8);
        }catch(JSONException error){throw problem("cdp_input_guard_failed");}
    }
    private static String code(String value){
        if(value==null)return null;if(value.contains("timeout"))return "input_timed_out";
        if(value.contains("cancelled")||value.contains("owner_changed")&&value.startsWith("cdp_binding"))return "input_cancelled";
        if(value.contains("frame_changed")||value.contains("frame_stale")||value.contains("snapshot_changed")||value.contains("world_context_changed"))return "frame_changed";
        return value.matches("[a-z_]{1,63}")?value:"input_guard_failed";
    }
    void run(MdoBrowserInput input,MdoBrowserCdpWorker worker,MdoBrowserCdpMarker marker,Current current,MdoBrowserInput.Completion complete)throws MdoBrowserInput.Failure {
        run(input,worker,marker,current,complete,null,null);
    }
    void runAttached(MdoBrowserInput input,MdoBrowserCdpWorker worker,MdoBrowserCdpMarker marker,Current current,MdoBrowserInput.Completion complete,
                     MdoBrowserCdpPopupBroker.Handoff handoff,MdoBrowserCdpPopupNative.Document documentOwner)throws MdoBrowserInput.Failure {
        if(handoff==null||documentOwner==null||marker==null||marker.lease()!=documentOwner.lease||!handoff.current()||!handoff.owns(documentOwner)||!documentOwner.current())throw new MdoBrowserInput.Failure("frame_changed");
        Current scope=()->{String code=current.rejection();return !marker.lease().current()||!handoff.current()||!handoff.owns(documentOwner)||!documentOwner.current()?"frame_changed":code;};
        run(input,worker,marker,scope,complete,handoff,documentOwner);
    }
    private void run(MdoBrowserInput input,MdoBrowserCdpWorker worker,MdoBrowserCdpMarker marker,Current current,MdoBrowserInput.Completion complete,
                     MdoBrowserCdpPopupBroker.Handoff handoff,MdoBrowserCdpPopupNative.Document documentOwner)throws MdoBrowserInput.Failure {
        MdoBrowserInput.Guard guard=new MdoBrowserInput.Guard(){
          public String rejection(){return current.rejection();}
          public void check(long remaining,MdoBrowserInput.Checked checked){
            String stale=current.rejection();if(stale!=null){checked.complete(stale);return;}
            if(refusal!=null){checked.complete(refusal);return;}if(remaining<100){checked.complete("input_timed_out");return;}
            try{pending=true;MdoBrowserCdpWorker.Completion done=result->{
                pending=false;String rejection=current.rejection();if(rejection==null)rejection=code(result.code);
                if(rejection==null&&!Arrays.equals(result.bytes,"{\"ok\":true}".getBytes(StandardCharsets.UTF_8)))rejection="input_guard_failed";
                if(rejection!=null)refusal=rejection;checked.complete(rejection);
            };
                if(handoff==null)worker.submit(marker,(int)Math.min(15000,remaining),kind.equals("focus"),MdoBrowserCdpInput.this::fence,done);
                else worker.submitDocument(marker,handoff,documentOwner,(int)Math.min(15000,remaining),kind.equals("focus"),MdoBrowserCdpInput.this::fence,done);
            }catch(IOException error){pending=false;refusal=code(error.getMessage());checked.complete(refusal);}
          }
        };
        MdoBrowserInput.Completion settled=result->{
            // A timed-out input must not leave a queued focus effect running.
            // Close its exact lease; never cancel a replacement view/job.
            if(pending){pending=false;worker.cancel(marker.lease());}complete.complete(result);
        };
        switch(kind){
            case "focus":input.focus(guard,settled);break;
            case "click":input.tap((float)(x*layout.scale),(float)(y*layout.scale),guard,settled);break;
            case "move":input.hover((float)(x*layout.scale),(float)(y*layout.scale),guard,settled);break;
            case "wheel":input.wheel((float)(x*layout.scale),(float)(y*layout.scale),(float)(dx*layout.scale),(float)(dy*layout.scale),guard,settled);break;
            case "text":input.text(text,guard,settled);break;
            case "key":input.key(key,modifiers,guard,settled);break;
            default:throw new MdoBrowserInput.Failure("input_kind_unsupported");
        }
    }
}
