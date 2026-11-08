package org.xserver.android;

import java.io.IOException;
import org.json.JSONObject;

/** Immutable native file-input pins. Inspection grants neither files nor an
 * assignment capability; raw DOM/CDP identities never leave the private worker. */
final class MdoBrowserCdpFileInput {
    interface BeforeClear {void check()throws IOException;}
    static final String NODE="(p=>{const r=globalThis.__mdo_browser_use_local_frame_v1;return r&&r.documentNode===document&&r.document===p.document?r.uploadNode({document:p.document,target:{ref:p.ref}}):null;})";
    static final String INFO="function(element,p){const r=globalThis.__mdo_browser_use_local_frame_v1;"+
        "if(!r||r.documentNode!==document||r.document!==p.document||!element||element.ownerDocument!==document||!element.isConnected)return {ok:false,code:'target_changed'};"+
        "const o={document:p.document,target:{ref:p.ref}},v=r.uploadInfo(o);"+
        "if(!v.ok)return {ok:false,code:v.error?.code||'target_changed'};"+
        "if(v.document!==p.document||v.element?.ref!==p.ref||r.uploadNode(o)!==element)return {ok:false,code:'target_changed'};"+
        "return {ok:true,document:p.document,ref:p.ref,multiple:element.multiple,directory:Boolean(element.webkitdirectory),hidden:v.element.hidden};}";
    static final String CLEAR_STATUS="function(element,p){const info=("+INFO+")(element,p);if(!info.ok)return info;"+
        "const get=Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'files')?.get;"+
        "if(!get)return {ok:false,code:'clear_unconfirmed'};const files=get.call(element);"+
        "return {ok:true,document:p.document,ref:p.ref,empty:files!==null&&files.length===0};}";
    final String document,ref;final MdoBrowserCdpFrameChain pins;
    static final String PIN="function(element,p){const r=globalThis.__mdo_browser_use_local_frame_v1;"+
        "if(!r||r.documentNode!==document||r.document!==p.document||!element||element.ownerDocument!==document||!element.isConnected)return {ok:false,code:'target_changed'};"+
        "const v=r.pinUploadNode(element);if(!v.ok)return {ok:false,code:v.error?.code||'target_changed'};"+
        "if(v.document!==p.document||r.uploadNode({document:p.document,target:{ref:v.element?.ref}})!==element)return {ok:false,code:'target_changed'};"+
        "return {ok:true,document:p.document,ref:v.element.ref,multiple:element.multiple,directory:Boolean(element.webkitdirectory),hidden:v.element.hidden};}";
    private MdoBrowserCdpFileInput(String document,String ref,MdoBrowserCdpFrameChain pins){this.document=document;this.ref=ref;this.pins=pins;}
    static MdoBrowserCdpFileInput parse(JSONObject request)throws IOException {
        Object document=request.opt("local_document"),ref=request.opt("ref");
        if(!(document instanceof String)||!((String)document).matches("d-"+MdoBrowserCdpFrameChain.UUID)||
            !(ref instanceof String)||!((String)ref).matches("e[1-9][0-9]{0,12}"))throw new IOException("cdp_file_input_request_invalid");
        MdoBrowserCdpFrameChain pins=MdoBrowserCdpFrameChain.parse(request);MdoBrowserCdpFrameChain.Pin leaf=pins.chain.get(0);
        if(!leaf.frame.equals(request.opt("frame"))||!leaf.document.equals(request.opt("document")))throw new IOException("cdp_file_input_request_invalid");
        return new MdoBrowserCdpFileInput((String)document,(String)ref,pins);
    }
    static void parents(MdoBrowserCdpFrameChain pins,MdoBrowserCdpFrames frames,MdoBrowserCdpWorker.Deadline deadline)throws IOException {
        for(int i=0;i+1<pins.chain.size();i++){
            MdoBrowserCdpFrameChain.Pin child=pins.chain.get(i),parent=pins.chain.get(i+1);
            JSONObject response=frames.owner("upload_policy",child.frame,child.document,null,deadline),value=response.optJSONObject("value");
            if(!parent.frame.equals(response.opt("parent"))||!parent.document.equals(response.opt("parent_document"))||value==null||!Boolean.TRUE.equals(value.opt("ok")))
                throw new IOException("cdp_file_input_ancestor_blocked");
        }
    }
    JSONObject inspect(MdoBrowserCdpFrames frames,MdoBrowserCdpWorker.Deadline deadline)throws IOException {
        pins.validate(frames,deadline);parents(pins,frames,deadline);
        MdoBrowserCdpFrameChain.Pin leaf=pins.chain.get(0);JSONObject result=frames.fileInput(leaf.frame,leaf.document,document,ref,deadline);
        pins.validate(frames,deadline);parents(pins,frames,deadline);pins.validate(frames,deadline);return result;
    }
    JSONObject clear(MdoBrowserCdpFrames frames,MdoBrowserCdpWorker.Deadline deadline)throws IOException {
        BeforeClear original=()->{pins.validate(frames,deadline);parents(pins,frames,deadline);pins.validate(frames,deadline);};
        original.check();MdoBrowserCdpFrameChain.Pin leaf=pins.chain.get(0);
        JSONObject result=frames.clearFileInput(leaf.frame,leaf.document,document,ref,original,deadline);
        original.check();return result;
    }
}
