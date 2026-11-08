package org.xserver.android;

import java.io.IOException;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;

/** Worker-confined original-byte cache. Reuses the upload backend's pinned
 * directory/lease/recovery guards with a separate download purpose. Native
 * callers supply app-private authority; no path or digest comes from the page.
 * This layer proves disk bytes only, never original network completion. */
final class MdoBrowserDownloadFiles implements AutoCloseable {
    static final long FILE_BYTES=MdoBrowserUploadFiles.DOWNLOAD_FILE_BYTES,TOTAL_BYTES=MdoBrowserUploadFiles.TOTAL_BYTES;
    static final int CHUNK=MdoBrowserUploadFiles.CHUNK,FILES=MdoBrowserUploadFiles.DOWNLOAD_FILES;
    static final class Failure extends IOException {
        final String code;Failure(String code){super(code);this.code=code;}
    }
    private final Thread owner=Thread.currentThread();private final MdoBrowserUploadFiles files;private boolean closed;
    MdoBrowserDownloadFiles(Path appPrivateRoot)throws IOException {
        MdoBrowserUploadFiles opened=null;
        try{opened=new MdoBrowserUploadFiles(appPrivateRoot,true);opened.acquireCacheLease();files=opened;}
        catch(IOException failure){if(opened!=null)try{opened.close();}catch(IOException|RuntimeException ignored){}throw mapped(failure);}
        catch(RuntimeException|Error failure){if(opened!=null)try{opened.close();}catch(IOException|RuntimeException ignored){}throw failure;}
    }
    private static Failure mapped(IOException failure){
        String code=failure instanceof MdoBrowserUploadFiles.Failure?((MdoBrowserUploadFiles.Failure)failure).code:null;
        return new Failure(code!=null&&code.matches("upload_[a-z_]{1,80}")?"download_"+code.substring(7):"download_cache_io_failed");
    }
    private void thread(){if(Thread.currentThread()!=owner)throw new IllegalStateException("Downloads require their owning file worker");}
    private void active()throws Failure {thread();if(closed)throw new Failure("download_cache_closed");}
    Writer create(long maximum)throws IOException {
        active();if(maximum<1||maximum>FILE_BYTES)throw new Failure("download_metadata_invalid");
        MessageDigest digest;try{digest=MessageDigest.getInstance("SHA-256");}catch(NoSuchAlgorithmException unavailable){throw new Failure("download_hash_unavailable");}
        // Allocate the public native wrapper/digest before creating a leaf.
        // The shared backend keeps a failed creation's reservation until cleanup.
        Writer writer=new Writer(digest);
        try{writer.leaf=files.createStream(maximum);return writer;}catch(IOException failure){throw mapped(failure);}
    }
    int reservations(){thread();return files.reservations();}
    long reservedBytes(){thread();return files.reservedBytes();}
    int recoveredFiles(){thread();return files.recoveredFiles();}
    long recoveredBytes(){thread();return files.recoveredBytes();}
    void collectFailed()throws IOException {thread();try{files.collectFailed();}catch(IOException failure){throw mapped(failure);}}
    @Override public void close()throws IOException {thread();closed=true;try{files.close();}catch(IOException failure){throw mapped(failure);}}
    final class Writer implements AutoCloseable {
        private final MessageDigest sourceDigest;private MdoBrowserUploadFiles.Leaf leaf;
        private long written;private String sourceSha;private boolean finished,failed,released;
        private Writer(MessageDigest digest){sourceDigest=digest;}
        private void usable()throws Failure {active();if(released||failed||leaf==null)throw new Failure("download_file_unavailable");}
        void append(long offset,byte[] originalBytes)throws IOException {
            usable();if(finished)throw new Failure("download_already_sealed");
            if(offset!=written)throw new Failure("download_chunk_invalid");
            try{leaf.append(offset,originalBytes);sourceDigest.update(originalBytes);written+=originalBytes.length;}
            catch(IOException failure){throw mapped(failure);}
            catch(RuntimeException|Error failure){failed=true;throw failure;}
        }
        /** Independent source-byte digest and descriptor-relative reread must
         * agree. The caller separately requires the original BodyResult.ok. */
        void finish(long originalSize)throws IOException {
            usable();if(originalSize!=written)throw new Failure("download_incomplete");
            if(finished)return;
            try{
                if(sourceSha==null){
                    char[] text=new char[64];byte[] digest=null;String digits="0123456789abcdef";
                    try{digest=sourceDigest.digest();for(int i=0;i<digest.length;i++){int value=digest[i]&255;text[i*2]=digits.charAt(value>>>4);text[i*2+1]=digits.charAt(value&15);}sourceSha=new String(text);}
                    finally{if(digest!=null)java.util.Arrays.fill(digest,(byte)0);java.util.Arrays.fill(text,'\0');}
                }
                leaf.sealStream(originalSize,sourceSha);finished=true;
            }catch(IOException failure){failed=true;throw mapped(failure);}
            catch(RuntimeException|Error failure){failed=true;throw failure;}
        }
        boolean sealed(){thread();return !released&&!failed&&finished&&leaf.sealed();}
        long size(){thread();return written;}
        String sha256()throws IOException {usable();if(!finished)throw new Failure("download_not_sealed");return sourceSha;}
        void verify()throws IOException {usable();if(!finished)throw new Failure("download_not_sealed");try{leaf.verify();}catch(IOException failure){throw mapped(failure);}}
        byte[] read(long offset,int maximum)throws IOException {usable();if(!finished)throw new Failure("download_not_sealed");try{return leaf.read(offset,maximum);}catch(IOException failure){throw mapped(failure);}}
        @Override public void close()throws IOException {thread();if(released)return;try{leaf.close();released=true;}catch(IOException failure){throw mapped(failure);}}
    }
}
