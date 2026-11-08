package org.xserver.android;

import android.content.Context;
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

/** One application-owned service. Context is native authority, never a caller
 * path. Resolve directories, UID/mode checks and recovery only on its file
 * worker; do not retain an Activity, WebView or task in the shared service. */
final class MdoBrowserDownloadRuntime {
    private static MdoBrowserDownloadRuntime instance;
    private final Context application;
    final MdoBrowserDownloadQueue files;
    final MdoBrowserCdpDownloads.Budget budget=new MdoBrowserCdpDownloads.Budget();
    private MdoBrowserDownloadRuntime(Context application){
        this.application=application;files=new MdoBrowserDownloadQueue(()->open(application),SystemClock::elapsedRealtime);
    }
    static synchronized MdoBrowserDownloadRuntime get(Context context){
        if(context==null)throw new IllegalArgumentException("download_cache_authority_invalid");
        Context app=context.getApplicationContext();if(app==null)throw new IllegalStateException("download_cache_authority_invalid");
        if(instance!=null){if(instance.application!=app)throw new IllegalStateException("download_cache_authority_changed");return instance;}
        instance=new MdoBrowserDownloadRuntime(app);return instance;
    }
    private static MdoBrowserDownloadFiles open(Context app)throws IOException {
        if(Looper.myLooper()==Looper.getMainLooper())throw new MdoBrowserDownloadFiles.Failure("download_cache_wrong_thread");
        try{
            File data=app.getDataDir().getCanonicalFile(),cache=app.getCacheDir().getCanonicalFile();
            if(!cache.getPath().equals(new File(data,"cache").getPath()))throw new MdoBrowserDownloadFiles.Failure("download_cache_authority_invalid");
            StructStat parent=Os.lstat(cache.getPath());
            if(!OsConstants.S_ISDIR(parent.st_mode)||parent.st_uid!=Os.getuid())throw new MdoBrowserDownloadFiles.Failure("download_cache_authority_invalid");
            Path root=cache.toPath().resolve("mdo-browser-use-download-v1");
            if(!Files.exists(root,LinkOption.NOFOLLOW_LINKS))Files.createDirectory(root,PosixFilePermissions.asFileAttribute(PosixFilePermissions.fromString("rwx------")));
            StructStat value=Os.lstat(root.toString());
            if(!OsConstants.S_ISDIR(value.st_mode)||value.st_uid!=Os.getuid()||(value.st_mode&0077)!=0)throw new MdoBrowserDownloadFiles.Failure("download_cache_authority_invalid");
            return new MdoBrowserDownloadFiles(root);
        }catch(android.system.ErrnoException failure){throw new MdoBrowserDownloadFiles.Failure("download_cache_authority_unavailable");}
        catch(MdoBrowserDownloadFiles.Failure failure){throw failure;}
        catch(IOException|RuntimeException failure){throw new MdoBrowserDownloadFiles.Failure("download_cache_authority_unavailable");}
    }
}
