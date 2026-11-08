package org.xserver.android;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Objects;
import java.util.Set;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;
import org.json.JSONTokener;

/** Immutable capture context. Native graph/world proofs surround an owned-view
 * draw; composed DOM privacy masks remain in the trusted shared renderer. */
final class MdoBrowserCdpCapture {
    interface Current {String rejection();}
    static final String FENCE="(p=>{try{"+
        "const bad=code=>({ok:false,code}),r=globalThis.__mdo_browser_use_local_frame_v1;"+
        "if(!r||r.documentNode!==document||r.document!==p.document||(p.root?self!==top:self===top))return bad('capture_changed');"+
        "if(![innerWidth,innerHeight,scrollX,scrollY,devicePixelRatio].every(Number.isFinite))return bad('capture_viewport_unsupported');"+
        "if(p.root){const v=globalThis.visualViewport;if(!v||![v.width,v.height,v.scale,v.offsetLeft,v.offsetTop].every(Number.isFinite)||v.scale<=0||"+
        "Math.abs(v.width-innerWidth)>1||Math.abs(v.height-innerHeight)>1||Math.abs(v.offsetLeft)>.01||Math.abs(v.offsetTop)>.01)return bad('capture_viewport_unsupported');}"+
        "if(Math.abs(innerWidth-p.viewport.width)>.05||Math.abs(innerHeight-p.viewport.height)>.05||Math.abs(scrollX-p.scroll.x)>.05||"+
        "Math.abs(scrollY-p.scroll.y)>.05||Math.abs(devicePixelRatio-p.device_pixel_ratio)>.001)return bad('capture_changed');"+
        "return {ok:true};}catch{return {ok:false,code:'capture_guard_failed'}}})";
    private static final byte[] ACK="{\"ok\":true}".getBytes(StandardCharsets.UTF_8);
    private static final Set<String> STATUS=new HashSet<>(Arrays.asList("readable","not_painted","outside_capture","opaque_geometry","opaque_ancestor","opaque_not_ready","opaque_budget","opaque_unreadable","opaque_scale"));
    private static final class Box {
        final double x,y,width,height;
        Box(JSONObject value,boolean rectangle)throws IOException {
            if(value==null||value.length()!=(rectangle?4:2))throw invalid();
            x=number(value.opt("x"));y=number(value.opt("y"));
            width=rectangle?number(value.opt("width")):0;height=rectangle?number(value.opt("height")):0;
            if(rectangle&&(width<0||height<0||width>32768||height>32768))throw invalid();
        }
        boolean matches(JSONObject value)throws IOException {if(value==null)return false;return near(x,number(value.opt("x")))&&near(y,number(value.opt("y")))&&
            near(width,number(value.opt("width")))&&near(height,number(value.opt("height")));}
    }
    private static final class Viewport {
        final double width,height;
        Viewport(JSONObject value)throws IOException {
            if(value==null||value.length()!=2)throw invalid();width=number(value.opt("width"));height=number(value.opt("height"));
            if(width<=0||height<=0||width>32768||height>32768)throw invalid();
        }
        boolean matches(JSONObject value)throws IOException {return value!=null&&near(width,number(value.opt("width")))&&near(height,number(value.opt("height")));}
    }
    private static final class Edge {
        final String document,ref;final Box content,box;
        Edge(JSONObject edge,boolean opaqueGeometry)throws IOException {
            if(edge==null||edge.length()<1||edge.length()>3)throw invalid();
            for(Iterator<String> keys=edge.keys();keys.hasNext();)if(!Arrays.asList("owner","content","box").contains(keys.next()))throw invalid();
            JSONObject owner=edge.optJSONObject("owner");if(owner==null||!local(owner.opt("document"))||!element(owner.opt("ref")))throw invalid();
            for(Iterator<String> keys=owner.keys();keys.hasNext();)if(!Arrays.asList("ok","document","ref").contains(keys.next()))throw invalid();
            if(owner.has("ok")&&!Boolean.TRUE.equals(owner.opt("ok")))throw invalid();document=(String)owner.opt("document");ref=(String)owner.opt("ref");
            content=opaqueGeometry?null:new Box(edge.optJSONObject("content"),true);box=opaqueGeometry?null:new Box(edge.optJSONObject("box"),true);
            if(opaqueGeometry&&(edge.has("content")||edge.has("box")))throw invalid();
        }
    }
    private static final class Row {
        final String frame,nativeDocument,parent,status,document;final boolean loading,isolated;final Box scroll;final Viewport viewport;final Edge edge;
        Row(JSONArray row)throws IOException,JSONException {
            if(row==null||row.length()!=10||!MdoBrowserCdpFrameChain.frame(row.opt(0))||!MdoBrowserCdpFrameChain.document(row.opt(1))||
                !(row.opt(3) instanceof Boolean)||!(row.opt(4) instanceof Boolean)||!(row.opt(5) instanceof String)||!STATUS.contains(row.opt(5)))throw invalid();
            frame=row.getString(0);nativeDocument=row.getString(1);parent=row.opt(2)==JSONObject.NULL?null:row.opt(2) instanceof String?(String)row.opt(2):null;
            if(frame.equals("f0")?row.opt(2)!=JSONObject.NULL:!MdoBrowserCdpFrameChain.frame(parent)||frame.equals(parent))throw invalid();
            loading=row.getBoolean(3);isolated=row.getBoolean(4);status=row.getString(5);
            if(status.equals("readable")){
                if(loading||!isolated||!local(row.opt(6)))throw invalid();document=row.getString(6);scroll=new Box(row.optJSONObject(7),false);viewport=new Viewport(row.optJSONObject(8));
            }else{if(row.opt(6)!=JSONObject.NULL||row.opt(7)!=JSONObject.NULL||row.opt(8)!=JSONObject.NULL)throw invalid();document=null;scroll=null;viewport=null;}
            edge=row.opt(9)==JSONObject.NULL?null:new Edge(row.optJSONObject(9),status.equals("opaque_geometry"));
            if(frame.equals("f0")&&(!status.equals("readable")||edge!=null))throw invalid();
            if(status.equals("opaque_not_ready")&&(!loading&&isolated||edge!=null))throw invalid();
            if(status.equals("opaque_ancestor")&&edge!=null)throw invalid();
            if(!frame.equals("f0")&&Arrays.asList("readable","opaque_geometry","opaque_scale","not_painted","outside_capture","opaque_unreadable").contains(status)&&edge==null)throw invalid();
            if(edge!=null&&(loading||!isolated))throw invalid();
        }
    }
    final String captureKey,nativeDocument,document;final long navigation,timeout;final double x,y,width,height,ratio,dpr;
    private final List<Row> rows;private final Row root;
    private MdoBrowserCdpCapture(String captureKey,String nativeDocument,String document,long navigation,long timeout,double x,double y,double width,double height,double ratio,double dpr,List<Row> rows){
        this.captureKey=captureKey;this.nativeDocument=nativeDocument;this.document=document;this.navigation=navigation;this.timeout=timeout;
        this.x=x;this.y=y;this.width=width;this.height=height;this.ratio=ratio;this.dpr=dpr;this.rows=Collections.unmodifiableList(rows);root=rows.get(0);
    }
    private static IOException invalid(){return new IOException("cdp_capture_request_invalid");}
    private static IOException changed(){return new IOException("cdp_capture_changed");}
    private static boolean local(Object value){return value instanceof String&&((String)value).matches("d-"+MdoBrowserCdpFrameChain.UUID);}
    private static boolean element(Object value){return value instanceof String&&((String)value).matches("e[1-9][0-9]{0,12}");}
    private static boolean near(double a,double b){return Math.abs(a-b)<.05;}
    private static double number(Object value)throws IOException {
        if(!(value instanceof Number)||!Double.isFinite(((Number)value).doubleValue())||Math.abs(((Number)value).doubleValue())>1e8)throw invalid();return((Number)value).doubleValue();
    }
    private static String text(Object value,int max)throws IOException {if(!(value instanceof String)||((String)value).isEmpty()||((String)value).length()>max)throw invalid();return(String)value;}
    private static long navigation(Object value)throws IOException {
        if(!(value instanceof Number))throw invalid();double number=((Number)value).doubleValue();
        if(!Double.isFinite(number)||number<1||number>9007199254740991d||number!=Math.rint(number))throw invalid();return(long)number;
    }
    private static JSONTokener stampReader(Object value)throws IOException {
        String source=text(value,65536);int depth=0,containers=0;boolean quoted=false,escaped=false;
        // Bound the embedded JSON BEFORE the recursive platform reader. Outer
        // command JSON sees this stamp as a string and cannot bound its depth.
        for(int i=0;i<source.length();i++){
            char c=source.charAt(i);
            if(quoted){if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c=='"')quoted=false;}
            else if(c=='"')quoted=true;
            else if(c=='['||c=='{'){if(++depth>8||++containers>512)throw invalid();}
            else if(c==']'||c=='}'){if(--depth<0)throw invalid();}
            else if(c=='\''||c=='/')throw invalid();
        }
        if(depth!=0||quoted)throw invalid();return new JSONTokener(source);
    }
    private static void keys(JSONObject value,String... allowed)throws IOException {
        if(value==null)throw invalid();Set<String> names=new HashSet<>(Arrays.asList(allowed));for(Iterator<String> keys=value.keys();keys.hasNext();)if(!names.contains(keys.next()))throw invalid();
    }
    static MdoBrowserCdpCapture parse(JSONObject request)throws IOException {
        try{
            keys(request,"channel","id","browser","command","agent","navigation","capture_key","clip","capture_context","format","max_bytes","timeout_ms");
            if(!"capture".equals(request.opt("command"))||!Boolean.TRUE.equals(request.opt("agent")))throw invalid();
            if(request.has("channel")&&!"mdo-browser".equals(request.opt("channel")))throw invalid();
            if(request.has("id")&&!text(request.opt("id"),36).matches(MdoBrowserCdpFrameChain.UUID))throw invalid();
            if(request.has("browser"))text(request.opt("browser"),100);
            String key=text(request.opt("capture_key"),128);if(!key.matches("[a-zA-Z0-9._:-]{1,128}"))throw invalid();
            JSONObject context=request.optJSONObject("capture_context"),clip=request.optJSONObject("clip");
            keys(context,"adapter","native_document","document","navigation","viewport","scroll","device_pixel_ratio","frame_stamp");keys(clip,"x","y","width","height","scale");
            if(!"android-native-frames-v1".equals(context.opt("adapter"))||!MdoBrowserCdpFrameChain.document(context.opt("native_document"))||!local(context.opt("document")))throw invalid();
            long nav=navigation(request.opt("navigation"));if(nav!=navigation(context.opt("navigation")))throw invalid();
            String format=request.has("format")?text(request.opt("format"),8):"png";if(!format.equals("png"))throw new IOException("cdp_capture_lossless_source_required");
            double max=request.has("max_bytes")?number(request.opt("max_bytes")):262144,time=request.has("timeout_ms")?number(request.opt("timeout_ms")):10000;
            if(max<1||max>262144||max!=Math.floor(max)||time<1||time>60000||time!=Math.floor(time))throw invalid();
            double x=number(clip.opt("x")),y=number(clip.opt("y")),width=number(clip.opt("width")),height=number(clip.opt("height")),ratio=number(clip.opt("scale")),dpr=number(context.opt("device_pixel_ratio"));
            if(width<=0||height<=0||width>32768||height>32768||width*height>16777216||dpr<=0||dpr>16||width*height*dpr*dpr>33554432||ratio<=0||ratio>1)throw invalid();
            JSONTokener reader=stampReader(context.opt("frame_stamp"));Object parsed=reader.nextValue();
            if(!(parsed instanceof JSONArray)||reader.nextClean()!=0)throw invalid();JSONArray stamp=(JSONArray)parsed;
            if(stamp.length()<1||stamp.length()>32)throw invalid();
            List<Row> rows=new ArrayList<>();Set<String> seen=new HashSet<>();
            for(int i=0;i<stamp.length();i++){Row row=new Row(stamp.optJSONArray(i));
                if(!seen.add(row.frame)||row.frame.equals("f0")!=(i==0))throw invalid();
                if(i>0){Row parent=null;for(Row prior:rows)if(prior.frame.equals(row.parent))parent=prior;
                    if(parent==null||row.edge!=null&&!Objects.equals(row.edge.document,parent.document)||row.status.equals("opaque_ancestor")&&parent.document!=null)throw invalid();}
                rows.add(row);
            }
            Row root=rows.get(0);Viewport viewport=new Viewport(context.optJSONObject("viewport"));Box scroll=new Box(context.optJSONObject("scroll"),false);
            if(!root.nativeDocument.equals(context.opt("native_document"))||!root.document.equals(context.opt("document"))||
                !near(root.viewport.width,viewport.width)||!near(root.viewport.height,viewport.height)||!near(root.scroll.x,scroll.x)||!near(root.scroll.y,scroll.y))throw invalid();
            return new MdoBrowserCdpCapture(key,root.nativeDocument,root.document,(long)nav,(long)Math.min(10000,time),x,y,width,height,ratio,dpr,rows);
        }catch(JSONException error){throw invalid();}
    }
    MdoBrowserCapture.Plan plan(MdoBrowserCapture.Snapshot snapshot)throws IOException {
        if(snapshot==null||!snapshot.document.equals(nativeDocument)||snapshot.navigation!=navigation)throw changed();
        if(x<root.scroll.x-1||y<root.scroll.y-1||x+width>root.scroll.x+root.viewport.width+1||y+height>root.scroll.y+root.viewport.height+1)
            throw new IOException("cdp_capture_region_outside_viewport");
        double factor=snapshot.scale;
        if(Math.abs(root.viewport.width*factor-snapshot.width)>2||Math.abs(root.viewport.height*factor-snapshot.height)>2||
            Math.abs(root.scroll.x*factor-snapshot.scrollX)>1||Math.abs(root.scroll.y*factor-snapshot.scrollY)>1)throw changed();
        try{return new MdoBrowserCapture.Plan(snapshot,(x-root.scroll.x)*factor,(y-root.scroll.y)*factor,width*factor,height*factor,
            Math.min(1,ratio*dpr/factor),"png",262144,timeout);}catch(IllegalArgumentException error){throw invalid();}
    }
    double reportedScale(MdoBrowserCapture.Plan plan){return Math.min(plan.outputWidth/width,plan.outputHeight/height)/dpr;}
    private Row row(String id){for(Row row:rows)if(row.frame.equals(id))return row;return null;}
    private void graph(MdoBrowserCdpFrames frames,MdoBrowserCdpWorker.Deadline deadline)throws IOException,JSONException {
        JSONObject tree=frames.refresh(deadline);JSONArray actual=tree.optJSONArray("frames");if(!Boolean.TRUE.equals(tree.opt("complete"))||actual==null||actual.length()!=rows.size())throw changed();
        Set<String> seen=new HashSet<>();for(int i=0;i<actual.length();i++){
            JSONObject value=actual.getJSONObject(i);Row row=row(value.optString("id",null));
            if(row==null||!seen.add(row.frame)||!row.nativeDocument.equals(value.opt("document"))||
                !Objects.equals(row.parent==null?JSONObject.NULL:row.parent,value.opt("parent"))||!Boolean.valueOf(row.loading).equals(value.opt("loading"))||!Boolean.valueOf(row.isolated).equals(value.opt("isolated")))throw changed();
        }
    }
    private void world(MdoBrowserCdpFrames frames,Row row,MdoBrowserCdpWorker.Deadline deadline)throws IOException,JSONException {
        JSONObject witness=new JSONObject().put("root",row.frame.equals("f0")).put("document",row.document).put("viewport",new JSONObject().put("width",row.viewport.width).put("height",row.viewport.height))
            .put("scroll",new JSONObject().put("x",row.scroll.x).put("y",row.scroll.y)).put("device_pixel_ratio",dpr);
        JSONObject value=frames.evaluate(row.frame,row.nativeDocument,FENCE+"("+witness.toString()+")",false,deadline).optJSONObject("value");
        if(value==null||!Boolean.TRUE.equals(value.opt("ok"))){String code=value==null?null:value.optString("code",null);
            throw new IOException(code!=null&&code.matches("capture_[a-z_]{1,45}")?"cdp_"+code:"cdp_capture_guard_failed");}
    }
    byte[] fence(MdoBrowserCdpBinding binding,MdoBrowserCdpWorker.Deadline deadline)throws IOException {
        try{
            MdoBrowserCdpFrames frames=binding.frames();graph(frames,deadline);
            for(Row row:rows){
                if(row.document!=null)world(frames,row,deadline);
                if(row.edge==null)continue;Row parent=row(row.parent);
                JSONObject result=frames.owner("capture_owner",row.frame,row.nativeDocument,null,deadline),value=result.optJSONObject("value"),owner=value==null?null:value.optJSONObject("owner");
                if(!parent.frame.equals(result.opt("parent"))||!parent.nativeDocument.equals(result.opt("parent_document"))||owner==null||
                    !row.edge.document.equals(owner.opt("document"))||!row.edge.ref.equals(owner.opt("ref")))throw changed();
                if(row.status.equals("opaque_geometry")){
                    JSONObject error=value.optJSONObject("error");if(!Boolean.FALSE.equals(value.opt("ok"))||error==null||!Arrays.asList("unsupported_geometry","frame_ancestry_limit").contains(error.opt("code")))throw changed();continue;
                }
                if(!Boolean.TRUE.equals(value.opt("ok"))||!Boolean.valueOf(!row.status.equals("not_painted")).equals(value.opt("painted"))||
                    !row.edge.box.matches(value.optJSONObject("box"))||!row.edge.content.matches(value.optJSONObject("content"))||
                    !parent.viewport.matches(value.optJSONObject("viewport")))throw changed();
                JSONObject scroll=value.optJSONObject("scroll");if(scroll==null||!near(parent.scroll.x,number(scroll.opt("x")))||!near(parent.scroll.y,number(scroll.opt("y"))))throw changed();
            }
            world(frames,root,deadline);graph(frames,deadline);return ACK.clone();
        }catch(JSONException error){throw new IOException("cdp_capture_guard_failed");}
    }
    final class Session implements AutoCloseable {
        private final MdoBrowserCdpWorker worker;private final MdoBrowserCdpMarker marker;private final Current current;private boolean pending,closed;
        private final MdoBrowserCdpPopupBroker.Handoff handoff;private final MdoBrowserCdpPopupNative.Document documentOwner;
        Session(MdoBrowserCdpWorker worker,MdoBrowserCdpMarker marker,Current current){this(worker,marker,current,null,null);}
        Session(MdoBrowserCdpWorker worker,MdoBrowserCdpMarker marker,Current current,MdoBrowserCdpPopupBroker.Handoff handoff,MdoBrowserCdpPopupNative.Document documentOwner){
            this.worker=worker;this.marker=marker;this.current=current;this.handoff=handoff;this.documentOwner=documentOwner;
        }
        private String rejection(){
            if(closed)return "capture_cancelled";String code=current.rejection();if(closed)return "capture_cancelled";if(code!=null)return code;
            if(!marker.lease().current()||handoff!=null&&(!handoff.current()||!handoff.owns(documentOwner)||!documentOwner.current()))return "capture_changed";
            return null;
        }
        void check(long remaining,MdoBrowserCapture.Checked complete){
            String failure=rejection();if(failure!=null){complete.complete(failure);return;}
            if(pending){complete.complete("capture_guard_busy");return;}
            if(remaining<100){complete.complete("capture_timed_out");return;}
            try{pending=true;MdoBrowserCdpWorker.Completion done=result->{
                pending=false;String code=rejection();if(code==null&&result.code!=null)code=code(result.code);
                if(code==null&&!Arrays.equals(ACK,result.bytes))code="capture_guard_failed";complete.complete(code);
            };
                if(handoff==null)worker.submit(marker,(int)Math.min(15000,remaining),false,MdoBrowserCdpCapture.this::fence,done);
                else worker.submitDocument(marker,handoff,documentOwner,(int)Math.min(15000,remaining),false,MdoBrowserCdpCapture.this::fence,done);
            }catch(IOException error){pending=false;complete.complete(code(error.getMessage()));}
        }
        @Override public void close(){if(closed)return;closed=true;if(pending){pending=false;worker.cancel(marker.lease());}}
    }
    Session session(MdoBrowserCdpWorker worker,MdoBrowserCdpMarker marker,Current current){return new Session(worker,marker,current);}
    Session attachedSession(MdoBrowserCdpWorker worker,MdoBrowserCdpMarker marker,Current current,MdoBrowserCdpPopupBroker.Handoff handoff,MdoBrowserCdpPopupNative.Document documentOwner)throws IOException {
        if(worker==null||marker==null||current==null||handoff==null||documentOwner==null||marker.lease()!=documentOwner.lease||
            !marker.lease().current()||!handoff.current()||!handoff.owns(documentOwner)||!documentOwner.current())throw new IOException("cdp_capture_scope_unavailable");
        return new Session(worker,marker,current,handoff,documentOwner);
    }
    private static String code(String value){if(value==null)return "capture_guard_failed";
        if(value.contains("timeout"))return "capture_timed_out";if(value.contains("cancelled"))return "capture_cancelled";
        return value.matches("[a-z_]{1,63}")?value:"capture_guard_failed";}
}
