package org.xserver.android;

import android.os.ProxyFileDescriptorCallback;
import android.system.ErrnoException;
import android.system.OsConstants;
import java.io.IOException;

/** One exact original platform callback. Releasing an FD never revokes its
 * shared file permission; failed native opens remain charged until onRelease. */
final class MdoBrowserUploadProxy extends ProxyFileDescriptorCallback {
    final MdoBrowserUploadUris.Session session;
    MdoBrowserUploadProxy(MdoBrowserUploadUris.Session session){if(session==null)throw new IllegalArgumentException("Missing proxy session");this.session=session;}
    private static ErrnoException errno(IOException failure){
        String code=failure instanceof MdoBrowserUploadFiles.Failure?((MdoBrowserUploadFiles.Failure)failure).code:null;
        int error="upload_range_invalid".equals(code)?OsConstants.EINVAL:"upload_read_cancelled".equals(code)?OsConstants.EINTR:OsConstants.EIO;
        return new ErrnoException("mdo_upload_read",error);
    }
    @Override public long onGetSize()throws ErrnoException {try{return session.size();}catch(IOException failure){throw errno(failure);}catch(RuntimeException|OutOfMemoryError failure){throw new ErrnoException("mdo_upload_size",OsConstants.EIO);}}
    @Override public int onRead(long offset,int size,byte[] data)throws ErrnoException {try{return session.read(offset,size,data,5000);}catch(IOException failure){throw errno(failure);}catch(RuntimeException|OutOfMemoryError failure){throw new ErrnoException("mdo_upload_read",OsConstants.EIO);}}
    @Override public int onWrite(long offset,int size,byte[] data)throws ErrnoException {throw new ErrnoException("mdo_upload_write",OsConstants.EBADF);}
    @Override public void onFsync()throws ErrnoException {throw new ErrnoException("mdo_upload_write",OsConstants.EBADF);}
    @Override public void onRelease(){session.released();}
}
