package org.xserver.android;

import android.view.InputDevice;
import android.view.KeyCharacterMap;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.ViewConfiguration;
import android.os.SystemClock;
import android.util.TypedValue;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.text.InputType;
import android.webkit.WebView;
import java.util.function.BooleanSupplier;
import java.util.function.LongSupplier;

/** Dispatch only to the owned WebView. Coordinates are Android view pixels;
 * isolated-world CSS-to-view geometry/focus validation belongs to the caller. */
final class MdoBrowserInputTarget implements MdoBrowserInput.Target {
    private final WebView view;private final LongSupplier epoch;private final BooleanSupplier admitted;
    private long touchReleasedAt=-1;
    MdoBrowserInputTarget(WebView view,LongSupplier epoch,BooleanSupplier admitted){
        if(view==null||epoch==null||admitted==null)throw new IllegalArgumentException("Missing input binding");
        this.view=view;this.epoch=epoch;this.admitted=admitted;
    }
    public long document(){return epoch.getAsLong();}
    public int width(){return view.getWidth();}public int height(){return view.getHeight();}
    public boolean interactive(){return admitted.getAsBoolean()&&view.isAttachedToWindow()&&view.isShown()&&view.hasWindowFocus();}
    public boolean focused(){return view.hasFocus();}
    public boolean acquireFocus(){return interactive()&&view.requestFocus();}
    public boolean touch(int action,long down,long time,float x,float y){
        MotionEvent event=MotionEvent.obtain(down,time,action,x,y,0);
        try{event.setSource(InputDevice.SOURCE_TOUCHSCREEN);boolean accepted=view.dispatchTouchEvent(event);
            if(accepted&&(action==MdoBrowserInput.UP||action==MdoBrowserInput.CANCEL))touchReleasedAt=SystemClock.uptimeMillis();return accepted;}
        finally{event.recycle();}
    }
    public long hoverDelay(long now){
        if(touchReleasedAt<0)return 0;
        // Chromium's deferred tap feedback can overwrite a later mouse hover.
        // Use the platform gesture window (at least its known 150ms feedback)
        // only after this exact target's touchscreen release. Never replay input.
        long window=Math.max(150,ViewConfiguration.getDoubleTapTimeout());
        return Math.max(0,window-Math.max(0,now-touchReleasedAt));
    }
    public boolean wheel(long time,float x,float y,float dx,float dy){
        // Chromium WindowAndroid uses this Activity theme attribute for BOTH
        // wheel axes; a generic ViewConfiguration horizontal factor can differ.
        TypedValue value=new TypedValue();
        if(!view.getContext().getTheme().resolveAttribute(android.R.attr.listPreferredItemHeight,value,true))return false;
        float factor=value.getDimension(view.getResources().getDisplayMetrics());
        float[] ticks=MdoBrowserWheel.ticks(dx,dy,factor);
        MotionEvent.PointerProperties pointer=new MotionEvent.PointerProperties();pointer.id=0;pointer.toolType=MotionEvent.TOOL_TYPE_MOUSE;
        MotionEvent.PointerCoords point=new MotionEvent.PointerCoords();point.x=x;point.y=y;
        point.setAxisValue(MotionEvent.AXIS_HSCROLL,ticks[0]);point.setAxisValue(MotionEvent.AXIS_VSCROLL,ticks[1]);
        MotionEvent event=MotionEvent.obtain(time,time,MotionEvent.ACTION_SCROLL,1,new MotionEvent.PointerProperties[]{pointer},
            new MotionEvent.PointerCoords[]{point},0,0,1,1,0,0,InputDevice.SOURCE_MOUSE,0);
        try{return view.dispatchGenericMotionEvent(event);}finally{event.recycle();}
    }
    public boolean hover(long time,float x,float y){
        MotionEvent.PointerProperties pointer=new MotionEvent.PointerProperties();pointer.id=0;pointer.toolType=MotionEvent.TOOL_TYPE_MOUSE;
        MotionEvent.PointerCoords point=new MotionEvent.PointerCoords();point.x=x;point.y=y;
        MotionEvent event=MotionEvent.obtain(time,time,MotionEvent.ACTION_HOVER_MOVE,1,new MotionEvent.PointerProperties[]{pointer},
            new MotionEvent.PointerCoords[]{point},0,0,1,1,0,0,InputDevice.SOURCE_MOUSE,0);
        // Use the framework hover route, with no touch, button or focus event.
        // The resulting pointer position persists normally until another move.
        try{return view.dispatchGenericMotionEvent(event);}finally{event.recycle();}
    }
    public boolean key(boolean down,long started,long time,String name,int modifiers){
        int code;
        if(name.length()==1)code=name.charAt(0)>='A'?KeyEvent.KEYCODE_A+name.charAt(0)-'A':KeyEvent.KEYCODE_0+name.charAt(0)-'0';
        else switch(name){
            case "Enter":code=KeyEvent.KEYCODE_ENTER;break;case "Tab":code=KeyEvent.KEYCODE_TAB;break;
            case "Backspace":code=KeyEvent.KEYCODE_DEL;break;case "Delete":code=KeyEvent.KEYCODE_FORWARD_DEL;break;
            case "Escape":code=KeyEvent.KEYCODE_ESCAPE;break;case "Space":code=KeyEvent.KEYCODE_SPACE;break;
            case "ArrowLeft":code=KeyEvent.KEYCODE_DPAD_LEFT;break;case "ArrowRight":code=KeyEvent.KEYCODE_DPAD_RIGHT;break;
            case "ArrowUp":code=KeyEvent.KEYCODE_DPAD_UP;break;case "ArrowDown":code=KeyEvent.KEYCODE_DPAD_DOWN;break;
            case "Home":code=KeyEvent.KEYCODE_MOVE_HOME;break;case "End":code=KeyEvent.KEYCODE_MOVE_END;break;
            case "PageUp":code=KeyEvent.KEYCODE_PAGE_UP;break;case "PageDown":code=KeyEvent.KEYCODE_PAGE_DOWN;break;
            default:throw new IllegalArgumentException("Unsupported content key");
        }
        int meta=((modifiers&1)!=0?KeyEvent.META_SHIFT_ON:0)|((modifiers&2)!=0?KeyEvent.META_CTRL_ON:0)|((modifiers&4)!=0?KeyEvent.META_ALT_ON:0);
        return view.dispatchKeyEvent(new KeyEvent(started,time,down?KeyEvent.ACTION_DOWN:KeyEvent.ACTION_UP,
            code,0,meta,KeyCharacterMap.VIRTUAL_KEYBOARD,0,0,InputDevice.SOURCE_KEYBOARD));
    }
    public boolean text(String value){
        EditorInfo info=new EditorInfo();InputConnection connection=view.onCreateInputConnection(info);
        // The WebView/IME owns this connection. Do not close it or retain it
        // across turns/documents, and never fall back to DOM value assignment.
        return connection!=null&&info.inputType!=InputType.TYPE_NULL&&connection.commitText(value,1);
    }
}
