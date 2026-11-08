package org.xserver.android;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.Context;
import android.content.pm.ProviderInfo;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.Binder;
import android.os.CancellationSignal;
import android.os.Handler;
import android.os.OperationCanceledException;
import android.os.ParcelFileDescriptor;
import android.os.storage.StorageManager;
import android.provider.OpenableColumns;
import android.webkit.MimeTypeMap;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Locale;

/** Same-UID read-only provider. Model/JSON callers cannot mint its native
 * object grants. All payload I/O runs on the existing upload worker. */
public final class MdoBrowserUploadProvider extends ContentProvider {
    @Override public void attachInfo(Context context,ProviderInfo info){
        if(info==null||info.exported||info.grantUriPermissions||!MdoBrowserUploadUriRuntime.authority(context).equals(info.authority))throw new SecurityException("Upload provider must remain private");
        super.attachInfo(context,info);
    }
    @Override public boolean onCreate(){return true;}
    private void caller(){if(getContext()==null||Binder.getCallingUid()!=android.os.Process.myUid())throw new SecurityException("Private upload resource");}
    private static FileNotFoundException unavailable(){return new FileNotFoundException("Upload resource unavailable");}
    private MdoBrowserUploadUris state()throws IOException {MdoBrowserUploadUris value=MdoBrowserUploadUriRuntime.existing(getContext());if(value==null)throw unavailable();return value;}
    private MdoBrowserUploadUris.FileInfo info(Uri uri)throws IOException {return state().info(MdoBrowserUploadUriRuntime.token(getContext(),uri));}
    @Override public Cursor query(Uri uri,String[] projection,String selection,String[] selectionArgs,String sortOrder){
        caller();try{
            MdoBrowserUploadUris.FileInfo file=info(uri);String[] columns=projection==null?new String[]{OpenableColumns.DISPLAY_NAME,OpenableColumns.SIZE}:projection;
            if(columns.length>32)throw new IllegalArgumentException("Upload projection too large");
            ArrayList<String> names=new ArrayList<>();ArrayList<Object> values=new ArrayList<>();
            for(String column:columns){if(OpenableColumns.DISPLAY_NAME.equals(column)){names.add(column);values.add(file.name);}else if(OpenableColumns.SIZE.equals(column)){names.add(column);values.add(file.size);}}
            MatrixCursor result=new MatrixCursor(names.toArray(new String[0]),1);result.addRow(values);return result;
        }catch(IOException unavailable){return null;}
    }
    @Override public String getType(Uri uri){
        caller();try{
            String name=info(uri).name;int dot=name.lastIndexOf('.');String type=dot<0?null:MimeTypeMap.getSingleton().getMimeTypeFromExtension(name.substring(dot+1).toLowerCase(Locale.ROOT));
            return type==null?"application/octet-stream":type;
        }catch(IOException unavailable){return null;}
    }
    @Override public ParcelFileDescriptor openFile(Uri uri,String mode)throws FileNotFoundException {return openFile(uri,mode,null);}
    @Override public ParcelFileDescriptor openFile(Uri uri,String mode,CancellationSignal signal)throws FileNotFoundException {
        caller();if(!"r".equals(mode))throw unavailable();if(signal!=null)signal.throwIfCanceled();
        MdoBrowserUploadUris.Session session;
        StorageManager storage;Handler callbackHandler;
        try{
            MdoBrowserUploadUris owner=state();String token=MdoBrowserUploadUriRuntime.token(getContext(),uri);
            storage=getContext().getSystemService(StorageManager.class);if(storage==null)throw unavailable();
            callbackHandler=MdoBrowserUploadUriRuntime.callbacks();session=owner.reserve(token);
        }catch(IOException|RuntimeException|OutOfMemoryError failure){throw unavailable();}
        ParcelFileDescriptor fd=null;boolean entered=false,delivered=false,listener=false;
        try{
            MdoBrowserUploadProxy callback=new MdoBrowserUploadProxy(session);
            if(signal!=null){listener=true;signal.setOnCancelListener(session::cancel);signal.throwIfCanceled();}
            if(!session.enter())throw unavailable();entered=true;
            fd=storage.openProxyFileDescriptor(ParcelFileDescriptor.MODE_READ_ONLY,callback,callbackHandler);
            if(fd==null)throw unavailable();
            if(signal!=null){signal.setOnCancelListener(null);listener=false;signal.throwIfCanceled();}
            if(!session.returned())throw unavailable();
            ParcelFileDescriptor result=fd;delivered=true;fd=null;return result;
        }catch(IOException|RuntimeException|OutOfMemoryError failure){
            if(failure instanceof OperationCanceledException)throw (OperationCanceledException)failure;throw unavailable();
        }finally{
            try{if(listener)try{signal.setOnCancelListener(null);}catch(RuntimeException|OutOfMemoryError detachFailure){session.cancel();}}
            finally{
                if(entered&&!delivered&&fd==null)session.openFailure();
                if(fd!=null){session.cancel();boolean confirmed=false;try{fd.close();confirmed=true;}catch(IOException|RuntimeException|OutOfMemoryError uncertain){}finally{if(!confirmed)session.closeFailure();}}
                if(!entered)session.beforeOpenFailure();
            }
        }
    }
    @Override public Uri insert(Uri uri,ContentValues values){caller();throw new SecurityException("Read-only upload provider");}
    @Override public int update(Uri uri,ContentValues values,String selection,String[] selectionArgs){caller();throw new SecurityException("Read-only upload provider");}
    @Override public int delete(Uri uri,String selection,String[] selectionArgs){caller();throw new SecurityException("Read-only upload provider");}
}
