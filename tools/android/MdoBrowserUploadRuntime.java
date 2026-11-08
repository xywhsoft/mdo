package org.xserver.android;

import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.system.Os;
import android.system.OsConstants;
import android.system.StructStat;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.nio.file.attribute.PosixFilePermissions;

/** App-private authority comes only from the native Context and UID. No View,
 * Activity, document, SDK workspace or caller path is retained by the worker. */
final class MdoBrowserUploadRuntime {
    private static MdoBrowserUploadQueue queue;
    private static File cache,data;
    static synchronized MdoBrowserUploadQueue get(Context context){
        Context app=context.getApplicationContext();File nextCache=app.getCacheDir(),nextData=app.getDataDir();
        if(queue!=null){if(!cache.equals(nextCache)||!data.equals(nextData))throw new IllegalStateException("upload_cache_authority_changed");return queue;}
        cache=nextCache;data=nextData;final File ownedCache=nextCache,ownedData=nextData;
        final Handler ui=new Handler(Looper.getMainLooper());
        queue=new MdoBrowserUploadQueue(()->open(ownedCache,ownedData),task->ui.post(task),SystemClock::elapsedRealtime,
            ()->Looper.myLooper()!=Looper.getMainLooper());return queue;
    }
    private static MdoBrowserUploadFiles open(File cache,File data)throws IOException {
        try{
            File nativeData=data.getCanonicalFile(),nativeCache=cache.getCanonicalFile();
            if(!nativeCache.getPath().equals(new File(nativeData,"cache").getPath()))throw new MdoBrowserUploadFiles.Failure("upload_cache_authority_invalid");
            StructStat parent=Os.lstat(nativeCache.getPath());
            if(!OsConstants.S_ISDIR(parent.st_mode)||parent.st_uid!=Os.getuid())throw new MdoBrowserUploadFiles.Failure("upload_cache_authority_invalid");
            Path root=nativeCache.toPath().resolve("mdo-browser-use-upload-v1");
            if(!Files.exists(root,LinkOption.NOFOLLOW_LINKS))Files.createDirectory(root,PosixFilePermissions.asFileAttribute(PosixFilePermissions.fromString("rwx------")));
            StructStat value=Os.lstat(root.toString());
            if(!OsConstants.S_ISDIR(value.st_mode)||value.st_uid!=Os.getuid()||(value.st_mode&0077)!=0)throw new MdoBrowserUploadFiles.Failure("upload_cache_authority_invalid");
            MdoBrowserUploadFiles files=new MdoBrowserUploadFiles(root);
            try{files.acquireCacheLease();return files;}
            catch(IOException|RuntimeException|Error failure){try{files.close();}catch(IOException ignored){}throw failure;}
        }catch(android.system.ErrnoException failure){throw new MdoBrowserUploadFiles.Failure("upload_cache_authority_unavailable");}
    }
}
