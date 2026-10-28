package org.xserver.android;

import android.content.Context;
import android.system.Os;
import org.json.JSONObject;
import java.io.*;

/** Identifies the signed APK. The host installs ordinary directory ZIPs. */
final class MdoRuntime {
    static void initialize(Context context) throws Exception {
        Os.setenv("MDO_ANDROID_SHARE_ROOT",new File(context.getFilesDir(),"mdo-shares").getAbsolutePath(),true);
        JSONObject metadata;
        try(InputStream input=context.getAssets().open("mdo-runtime.json")) {
            ByteArrayOutputStream bytes=new ByteArrayOutputStream();byte[] buffer=new byte[8192];int n;
            while((n=input.read(buffer))!=-1) {
                if(bytes.size()+n>1048576)throw new IOException("APK tool index exceeds limit");
                bytes.write(buffer,0,n);
            }
            metadata=new JSONObject(bytes.toString("UTF-8"));
        }
        Os.setenv("MDO_EDITION",metadata.getString("edition"),true);
        Os.setenv("MDO_BUILD_ID",metadata.getString("build_id"),true);
        Os.setenv("MDO_NATIVE_DIR",context.getApplicationInfo().nativeLibraryDir,true);
        Os.setenv("MDO_APK_PATH",context.getApplicationInfo().sourceDir,true);
        Os.setenv("LD_LIBRARY_PATH",context.getApplicationInfo().nativeLibraryDir,true);
    }
}
