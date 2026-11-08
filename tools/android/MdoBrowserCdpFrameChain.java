package org.xserver.android;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

/** Immutable caller pins. Only the native graph can prove their ancestry. */
final class MdoBrowserCdpFrameChain {
    static final String UUID="[a-fA-F0-9]{8}-[a-fA-F0-9]{4}-[a-fA-F0-9]{4}-[a-fA-F0-9]{4}-[a-fA-F0-9]{12}";
    static final class Pin {
        final String frame,document,parent;
        Pin(String frame,String document,String parent){this.frame=frame;this.document=document;this.parent=parent;}
    }
    final List<Pin> chain,snapshot;
    private MdoBrowserCdpFrameChain(List<Pin> chain,List<Pin> snapshot){
        this.chain=Collections.unmodifiableList(chain);this.snapshot=Collections.unmodifiableList(snapshot);
    }
    static boolean frame(Object value){return value instanceof String&&(((String)value).equals("f0")||((String)value).matches("f-"+UUID));}
    static boolean document(Object value){return value instanceof String&&((String)value).matches("fd-"+UUID);}
    private static IOException invalid(){return new IOException("cdp_input_frame_pins_invalid");}
    static MdoBrowserCdpFrameChain parse(JSONObject request)throws IOException {
        try{
            JSONArray chain=request.optJSONArray("frame_chain");if(chain==null||chain.length()<1||chain.length()>32)throw invalid();
            List<Pin> pins=new ArrayList<>(),snapshot=new ArrayList<>();Set<String> seen=new HashSet<>();
            for(int i=0;i<chain.length();i++){
                JSONObject row=chain.optJSONObject(i);if(row==null||row.length()!=2||!frame(row.opt("frame"))||!document(row.opt("document")))throw invalid();
                String frame=row.getString("frame"),document=row.getString("document");
                if(!seen.add(frame)||frame.equals("f0")!=(i==chain.length()-1))throw invalid();
                pins.add(new Pin(frame,document,null));
            }
            if(request.has("frame_snapshot")){
                JSONArray rows=request.optJSONArray("frame_snapshot");if(rows==null||rows.length()<1||rows.length()>32)throw invalid();seen.clear();
                for(int i=0;i<rows.length();i++){
                    JSONObject row=rows.optJSONObject(i);if(row==null||row.length()!=3||!frame(row.opt("frame"))||!document(row.opt("document"))||!row.has("parent"))throw invalid();
                    String frame=row.getString("frame"),parent=null;Object value=row.opt("parent");
                    // The shared visual dispatcher uses an empty root-parent
                    // string for the Windows wire contract; native inventories
                    // use JSON null. Both mean only this explicitly named root.
                    if(frame.equals("f0")){if(value!=JSONObject.NULL&&!"".equals(value))throw invalid();}
                    else{if(!frame(value)||frame.equals(value))throw invalid();parent=(String)value;}
                    if(!seen.add(frame))throw invalid();snapshot.add(new Pin(frame,row.getString("document"),parent));
                }
                if(!seen.contains("f0"))throw invalid();
                for(Pin pin:snapshot){Set<String> path=new HashSet<>();Pin at=pin;
                    while(at.parent!=null){if(!path.add(at.frame))throw invalid();Pin next=null;for(Pin row:snapshot)if(row.frame.equals(at.parent))next=row;
                        if(next==null)throw invalid();at=next;}}
                for(int i=0;i<pins.size();i++){Pin pin=pins.get(i),actual=null;for(Pin row:snapshot)if(row.frame.equals(pin.frame))actual=row;
                    if(actual==null||!actual.document.equals(pin.document)||!java.util.Objects.equals(actual.parent,i+1<pins.size()?pins.get(i+1).frame:null))throw invalid();}
            }
            return new MdoBrowserCdpFrameChain(pins,snapshot);
        }catch(JSONException error){throw invalid();}
    }
    void validate(MdoBrowserCdpFrames frames,MdoBrowserCdpWorker.Deadline deadline)throws IOException {
        JSONObject tree=frames.refresh(deadline);
        try{
            if(!Boolean.TRUE.equals(tree.opt("complete")))throw new IOException("cdp_input_frame_not_ready");
            JSONArray rows=tree.getJSONArray("frames");Set<String> seen=new HashSet<>();
            for(int i=0;i<chain.size();i++){
                Pin pin=chain.get(i);JSONObject nativeRow=null;for(int j=0;j<rows.length();j++)if(pin.frame.equals(rows.getJSONObject(j).opt("id")))nativeRow=rows.getJSONObject(j);
                if(nativeRow==null||!pin.document.equals(nativeRow.opt("document"))||!Boolean.FALSE.equals(nativeRow.opt("loading"))||!Boolean.TRUE.equals(nativeRow.opt("isolated"))||
                    !java.util.Objects.equals(i+1<chain.size()?chain.get(i+1).frame:JSONObject.NULL,nativeRow.opt("parent")))throw new IOException("cdp_input_frame_changed");
                frames.select(pin.frame,pin.document);
            }
            if(!snapshot.isEmpty()){
                if(rows.length()!=snapshot.size())throw new IOException("cdp_input_snapshot_changed");
                for(int i=0;i<rows.length();i++){
                    JSONObject row=rows.getJSONObject(i);Pin pin=null;for(Pin saved:snapshot)if(saved.frame.equals(row.opt("id")))pin=saved;
                    if(pin==null||!seen.add(pin.frame)||!pin.document.equals(row.opt("document"))||!Boolean.FALSE.equals(row.opt("loading"))||!Boolean.TRUE.equals(row.opt("isolated"))||
                        !java.util.Objects.equals(pin.parent==null?JSONObject.NULL:pin.parent,row.opt("parent")))throw new IOException("cdp_input_snapshot_changed");
                }
            }
        }catch(JSONException error){throw new IOException("cdp_input_frame_result_invalid");}
    }
}
