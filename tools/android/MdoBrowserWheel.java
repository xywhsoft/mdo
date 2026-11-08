package org.xserver.android;

/** Pixel deltas use the same right/down convention as the shared CDP backend.
 * The installed WebView receives native wheel ticks; no touch/DOM fallback.
 * Reference mappings and source hashes: scroll-protocol.json. */
final class MdoBrowserWheel {
    static final float MAX_DELTA=1048576;
    static boolean delta(float value){return Float.isFinite(value)&&Math.abs(value)<=MAX_DELTA;}
    static float[] ticks(float dx,float dy,float physicalPixelsPerTick){
        if(!delta(dx)||!delta(dy)||!Float.isFinite(physicalPixelsPerTick)||physicalPixelsPerTick<=0)
            throw new IllegalArgumentException("Invalid native wheel geometry");
        float x=(float)(-(double)dx/physicalPixelsPerTick),y=(float)(-(double)dy/physicalPixelsPerTick);
        if(!Float.isFinite(x)||!Float.isFinite(y))throw new IllegalArgumentException("Native wheel conversion overflow");
        return new float[]{x,y};
    }
}
