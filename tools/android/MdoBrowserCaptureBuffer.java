package org.xserver.android;

import java.io.ByteArrayOutputStream;

/** Enforce the compressed byte limit during encoding, before growth/copy. */
final class MdoBrowserCaptureBuffer extends ByteArrayOutputStream {
    private final int maximum;
    MdoBrowserCaptureBuffer(int maximum){
        super(Math.min(4096,Math.max(1,maximum)));
        if(maximum<1||maximum>262144)throw new IllegalArgumentException("Invalid capture byte limit");this.maximum=maximum;
    }
    @Override public synchronized void write(int value){if(count==maximum)throw new MdoBrowserCapture.Limit();super.write(value);}
    @Override public synchronized void write(byte[] source,int offset,int length){
        if(source==null)throw new NullPointerException();
        if(offset<0||length<0||offset>source.length-length)throw new IndexOutOfBoundsException();
        if(length>maximum-count)throw new MdoBrowserCapture.Limit();super.write(source,offset,length);
    }
}
