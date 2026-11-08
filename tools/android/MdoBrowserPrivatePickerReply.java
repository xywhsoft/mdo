package org.xserver.android;

import android.content.Context;
import android.net.Uri;
import android.webkit.ValueCallback;
import java.util.function.BooleanSupplier;

/** Original SDK callback only. Native ReadLease objects, never caller URI/path
 * strings, mint permissions. Preparing a grant can fail before onReceiveValue;
 * still cancel the original callback once, and never replay an uncertain send. */
final class MdoBrowserPrivatePickerReply implements MdoBrowserPrivatePicker.Response {
    private final Context context;private ValueCallback<Uri[]> callback;private boolean sent;
    MdoBrowserPrivatePickerReply(Context context,ValueCallback<Uri[]> callback){
        if(context==null||callback==null)throw new IllegalArgumentException("Missing original native picker reply");this.context=context.getApplicationContext();this.callback=callback;
    }
    public void send(MdoBrowserUploadStore.ReadLease[] files,BooleanSupplier current){
        if(sent)throw new IllegalStateException("upload_picker_response_retired");sent=true;ValueCallback<Uri[]> original=callback;callback=null;
        Uri[] grant=null;boolean prepared=files==null,answered=false,ownerChanged=false;
        try{
            if(files!=null){
                if(files.length<1||files.length>16||current==null)throw new IllegalStateException("upload_picker_grant_invalid");
                grant=new Uri[files.length];for(int i=0;i<files.length;i++){
                    if(!current.getAsBoolean()){ownerChanged=true;throw new IllegalStateException("upload_picker_owner_changed");}grant[i]=MdoBrowserUploadUriRuntime.publish(context,files[i]);
                }
                if(!current.getAsBoolean()){ownerChanged=true;throw new IllegalStateException("upload_picker_owner_changed");}prepared=true;
            }
        }catch(Exception|OutOfMemoryError error){grant=null;}
        try{original.onReceiveValue(grant);answered=true;}
        finally{
            if(!prepared||!answered){if(files!=null)for(MdoBrowserUploadStore.ReadLease file:files)if(file!=null)file.revoke();}
        }
        if(!prepared)throw MdoBrowserPrivatePicker.cancelledGrant(ownerChanged);
    }
}
