package org.xserver.android;

import android.app.Activity;
import android.content.ClipData;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.ProviderInfo;
import android.net.Uri;
import android.os.Process;
import java.util.LinkedHashSet;

/** Shared ingress policy for human picker results in both host and content
 * WebViews. A native callback runs with our UID; a picker cannot nominate any
 * same-UID provider merely by returning a content URI. No file/URL is opened. */
final class MdoBrowserPickerUris {
    private static boolean permitted(Activity activity,Uri uri){
        if(uri==null||uri.toString().length()>8192||!"content".equals(uri.getScheme()))return false;
        String authority=uri.getAuthority();
        if(authority==null||authority.length()>256||!authority.matches("[A-Za-z0-9_.-]+"))return false;
        ProviderInfo provider=activity.getPackageManager().resolveContentProvider(authority,0);
        if(provider==null||provider.applicationInfo==null||provider.applicationInfo.uid==Process.myUid())return false;
        return activity.checkUriPermission(uri,Process.myPid(),Process.myUid(),Intent.FLAG_GRANT_READ_URI_PERMISSION)==PackageManager.PERMISSION_GRANTED;
    }
    static Uri[] read(Activity activity,int result,Intent data,boolean multiple){
        if(activity==null||result!=Activity.RESULT_OK||data==null)return null;
        try{
            LinkedHashSet<Uri> selected=new LinkedHashSet<>();ClipData clip=data.getClipData();
            if(clip!=null){int size=clip.getItemCount();if(size>16||size<1||!multiple&&size>1)return null;
                for(int at=0;at<size;at++){Uri uri=clip.getItemAt(at).getUri();if(!permitted(activity,uri))return null;selected.add(uri);}}
            else{Uri uri=data.getData();if(!permitted(activity,uri))return null;selected.add(uri);}
            return selected.toArray(new Uri[0]);
        }catch(RuntimeException|OutOfMemoryError invalid){return null;}
    }
}
