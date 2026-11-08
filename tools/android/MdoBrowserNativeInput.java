package org.xserver.android;

import android.webkit.WebView;
import java.io.IOException;
import java.util.function.BooleanSupplier;
import java.util.function.DoubleSupplier;
import org.json.JSONObject;

/** Exact UI geometry plus the private native frame-chain input core. */
final class MdoBrowserNativeInput {
    private final WebView view;private final BooleanSupplier owned;private final DoubleSupplier scale;
    private final MdoBrowserCdpWorker worker;private final MdoBrowserCdpMarker marker;private final MdoBrowserCdpInput operation;
    private final MdoBrowserCdpPopupBroker.Handoff handoff;private final MdoBrowserCdpPopupNative.Document documentOwner;
    MdoBrowserNativeInput(WebView view,MdoBrowserCdpWorker worker,MdoBrowserCdpMarker marker,BooleanSupplier owned,DoubleSupplier scale,JSONObject request)throws IOException {
        this(view,worker,marker,owned,scale,request,null,null);
    }
    MdoBrowserNativeInput(WebView view,MdoBrowserCdpWorker worker,MdoBrowserCdpMarker marker,BooleanSupplier owned,DoubleSupplier scale,JSONObject request,
                          MdoBrowserCdpPopupBroker.Handoff handoff,MdoBrowserCdpPopupNative.Document documentOwner)throws IOException {
        if(view==null||worker==null||marker==null||owned==null||scale==null)throw new IllegalArgumentException("Missing native input owner");
        if((handoff==null)!=(documentOwner==null)||handoff!=null&&(marker.lease()!=documentOwner.lease||!handoff.current()||!handoff.owns(documentOwner)||!documentOwner.current()))throw new IOException("cdp_input_scope_unavailable");
        this.view=view;this.worker=worker;this.marker=marker;this.owned=owned;this.scale=scale;
        this.handoff=handoff;this.documentOwner=documentOwner;
        operation=MdoBrowserCdpInput.parse(request,layout());
    }
    private MdoBrowserCdpInput.Layout layout()throws IOException {
        if(view.getPaddingLeft()!=0||view.getPaddingTop()!=0||view.getPaddingRight()!=0||view.getPaddingBottom()!=0)throw new IOException("cdp_input_layout_invalid");
        return new MdoBrowserCdpInput.Layout(view.getWidth(),view.getHeight(),view.getScrollX(),view.getScrollY(),scale.getAsDouble());
    }
    private String current(){
        if(!owned.getAsBoolean()||!marker.lease().current()||handoff!=null&&(!handoff.current()||!handoff.owns(documentOwner)||!documentOwner.current()))return "frame_changed";
        try{return operation.layout.matches(layout())?null:"input_viewport_changed";}
        catch(IOException|RuntimeException error){return "input_viewport_changed";}
    }
    boolean focusing(){return operation.kind.equals("focus");}
    void run(MdoBrowserInput input,MdoBrowserInput.Completion complete)throws MdoBrowserInput.Failure {
        String stale=current();if(stale!=null)throw new MdoBrowserInput.Failure(stale);
        if(handoff==null)operation.run(input,worker,marker,this::current,complete);
        else operation.runAttached(input,worker,marker,this::current,complete,handoff,documentOwner);
    }
}
