package org.xserver.android;

import android.content.Context;
import android.net.Uri;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.Looper;
import android.os.SystemClock;
import java.io.IOException;

/** One application registry and callback thread, created lazily. Neither a
 * native URI nor a descriptor exposes the cache path/record/serialized lease. */
final class MdoBrowserUploadUriRuntime {
    private static MdoBrowserUploadUris registry;private static String packageName;
    private static HandlerThread thread;private static Handler handler;
    static String authority(Context context){return context.getApplicationContext().getPackageName()+".mdo.browseruse.upload";}
    private static void same(Context context){if(packageName!=null&&!packageName.equals(context.getApplicationContext().getPackageName()))throw new IllegalStateException("upload_uri_context_changed");}
    static synchronized Uri publish(Context context,MdoBrowserUploadStore.ReadLease lease)throws IOException {
        same(context);MdoBrowserUploadQueue queue=MdoBrowserUploadRuntime.get(context);
        if(!queue.owns(lease))throw new MdoBrowserUploadFiles.Failure("upload_read_lease_invalid");
        if(registry==null){MdoBrowserUploadUris created=new MdoBrowserUploadUris(queue,SystemClock::elapsedRealtime);packageName=context.getApplicationContext().getPackageName();registry=created;}
        String token=registry.publish(lease);return Uri.parse("content://"+authority(context)+"/v1/"+token);
    }
    static synchronized MdoBrowserUploadUris existing(Context context){same(context);return registry;}
    static String token(Context context,Uri uri)throws IOException {
        if(uri==null)throw new MdoBrowserUploadFiles.Failure("upload_uri_invalid");
        String value=uri.toString(),prefix="content://"+authority(context)+"/v1/";
        if(value.length()!=prefix.length()+36||!value.startsWith(prefix))throw new MdoBrowserUploadFiles.Failure("upload_uri_invalid");
        String token=value.substring(prefix.length());if(!MdoBrowserUploadUris.token(token))throw new MdoBrowserUploadFiles.Failure("upload_uri_invalid");return token;
    }
    static synchronized Handler callbacks()throws IOException {
        if(thread==null){
            HandlerThread created=new HandlerThread("mdo-browser-upload-uri");thread=created;created.start();
        }
        if(!thread.isAlive())throw new MdoBrowserUploadFiles.Failure("upload_proxy_thread_unavailable");
        Looper looper=thread.getLooper();if(looper==null)throw new MdoBrowserUploadFiles.Failure("upload_proxy_thread_unavailable");
        if(handler==null)handler=new Handler(looper);return handler;
    }
}
