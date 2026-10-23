package org.xserver.android;

import android.app.Activity;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.net.Uri;
import android.webkit.*;
import android.view.View;
import android.widget.FrameLayout;
import android.util.Base64;
import org.json.*;
import java.util.*;
import java.io.ByteArrayOutputStream;

/** Main-document-only message port. No JavascriptInterface is exposed to any
 * content frame, including iframe previews in the trusted host WebView. */
final class MdoBrowser {
    private final Activity activity;
    private final FrameLayout root;
    private final WebView host;
    private final Map<String,WebView> views = new HashMap<>();
    private final Set<String> manual = new HashSet<>();
    private final Map<String,Boolean> loading = new HashMap<>();
    private final Map<String,String> errors = new HashMap<>();
    private final Map<String,Integer> errorCodes = new HashMap<>();
    private final Set<String> crashed = new HashSet<>();
    private final Map<String,String> locations = new HashMap<>();
    private WebMessagePort port;
    private String document;
    private boolean stopped;
    MdoBrowser(Activity activity, FrameLayout root, WebView host) {
        this.activity=activity; this.root=root; this.host=host;
    }
    void attach(String url) {
        if (stopped || url == null) return;
        document=url.split("#",2)[0];
        if (port != null) port.close();
        WebMessagePort[] ports=host.createWebMessageChannel(); port=ports[0];
        port.setWebMessageCallback(new WebMessagePort.WebMessageCallback() {
            @Override public void onMessage(WebMessagePort source, WebMessage message) {
                if (stopped || host.getUrl()==null || !host.getUrl().split("#",2)[0].equals(document)) return;
                String text=message.getData(); if(text==null || text.length()>262144) return;
                try { request(new JSONObject(text)); } catch(Exception error) { /* Invalid input has no effects. */ }
            }
        });
        String script="(()=>{const f=e=>{if(e.data!=='mdo-browser-init'||e.ports.length!==1||e.source!==null)return;window.removeEventListener('message',f);const p=e.ports[0];"
            + "window.XsBrowser={request:s=>p.postMessage(s)};p.onmessage=e=>window.dispatchEvent(new CustomEvent('mdo-browser-result',{detail:e.data}));"
            + "window.dispatchEvent(new Event('mdo-browser-ready'));};window.addEventListener('message',f)})()";
        final Uri origin=Uri.parse(Uri.parse(url).getScheme()+"://"+Uri.parse(url).getAuthority());
        host.evaluateJavascript(script, ignored -> {
            if (!stopped) host.postWebMessage(new WebMessage("mdo-browser-init",new WebMessagePort[]{ports[1]}),origin);
        });
    }
    private void reply(String id,Object result,String error) {
        if(stopped || port==null)return;
        try { JSONObject value=new JSONObject().put("channel","mdo-browser").put("id",id);
            if(error!=null)value.put("error",error);else value.put("result",result);
            String json=value.toString();
            if(json.length()>1048576)json=new JSONObject().put("channel","mdo-browser").put("id",id).put("error","Browser result exceeds 1 MiB").toString();
            port.postMessage(new WebMessage(json));
        } catch(Exception ignored) { }
    }
    private static String url(String value) {
        Uri uri=Uri.parse(value);String scheme=uri.getScheme();
        if(value.length()>8192 || (!"http".equals(scheme)&&!"https".equals(scheme)) || uri.getHost()==null || uri.getUserInfo()!=null)
            throw new IllegalArgumentException("Only HTTP(S) navigation is supported");
        return value;
    }
    private void bounds(WebView web,JSONObject request) {
        JSONObject box=request.optJSONObject("bounds");if(box==null)return;
        double scale=Math.max(.5,Math.min(8,box.optDouble("scale",1)));
        int width=(int)Math.max(1,Math.min(16384,box.optDouble("width",640)*scale));
        int height=(int)Math.max(1,Math.min(16384,box.optDouble("height",480)*scale));
        FrameLayout.LayoutParams params=new FrameLayout.LayoutParams(width,height);
        params.leftMargin=host.getLeft()+(int)Math.max(0,Math.min(32768,box.optDouble("x",0)*scale));
        params.topMargin=host.getTop()+(int)Math.max(0,Math.min(32768,box.optDouble("y",0)*scale));
        web.setLayoutParams(params);web.setVisibility(request.optBoolean("visible",true)?View.VISIBLE:View.INVISIBLE);
    }
    private void request(JSONObject request) throws Exception {
        String id=request.optString("id"),command=request.optString("command"),key=request.optString("browser");
        if(!"mdo-browser".equals(request.optString("channel"))||id.length()!=36)return;
        try {
            if("capabilities".equals(command)) {reply(id,new JSONObject().put("native",true).put("snapshot",true).put("screenshot",true).put("input",true).put("profile_isolation",false),null);return;}
            if(key.isEmpty()||key.length()>100)throw new IllegalArgumentException("Invalid browser identity");
            WebView web=views.get(key);
            if("create".equals(command)) {
                if(web!=null){bounds(web,request);reply(id,new JSONObject().put("attached",true),null);return;}
                if(views.size()>=8)throw new IllegalStateException("Native browser view limit reached (8)");
                String target=request.optString("url","about:blank");if(!"about:blank".equals(target))target=url(target);
                web=new WebView(activity);web.getSettings().setJavaScriptEnabled(true);web.getSettings().setDomStorageEnabled(true);
                web.getSettings().setAllowFileAccess(false);web.getSettings().setAllowContentAccess(false);
                web.getSettings().setMixedContentMode(WebSettings.MIXED_CONTENT_NEVER_ALLOW);
                web.setWebViewClient(new WebViewClient(){
                    @Override public void onPageStarted(WebView view,String address,Bitmap icon){loading.put(key,true);errors.remove(key);errorCodes.remove(key);locations.put(key,address);}
                    @Override public void onPageFinished(WebView view,String address){loading.put(key,false);}
                    @Override public void onReceivedError(WebView view,WebResourceRequest navigation,WebResourceError error){
                        if(navigation.isForMainFrame()){loading.put(key,false);errors.put(key,error.getDescription().toString());errorCodes.put(key,error.getErrorCode());}
                    }
                    @Override public void onReceivedSslError(WebView view,SslErrorHandler handler,android.net.http.SslError error){handler.cancel();loading.put(key,false);errors.put(key,"TLS certificate validation failed");errorCodes.put(key,error.getPrimaryError());}
                    @Override public void onReceivedHttpError(WebView view,WebResourceRequest navigation,WebResourceResponse response){
                        if(navigation.isForMainFrame()){errorCodes.put(key,response.getStatusCode());}
                    }
                    @Override public boolean shouldOverrideUrlLoading(WebView view,WebResourceRequest navigation){
                        String scheme=navigation.getUrl().getScheme();return !"http".equals(scheme)&&!"https".equals(scheme);
                    }
                    @Override public boolean onRenderProcessGone(WebView view,RenderProcessGoneDetail detail){
                        views.remove(key);manual.remove(key);loading.put(key,false);errors.put(key,"Browser process stopped; reload to restore the page");crashed.add(key);root.removeView(view);view.destroy();return true;
                    }
                });
                crashed.remove(key);locations.put(key,target);views.put(key,web);root.addView(web);bounds(web,request);web.loadUrl(target);reply(id,new JSONObject().put("created",true),null);return;
            }
            if("close".equals(command)) {if(web!=null){views.remove(key);root.removeView(web);web.destroy();}manual.remove(key);loading.remove(key);errors.remove(key);errorCodes.remove(key);crashed.remove(key);locations.remove(key);reply(id,new JSONObject(),null);return;}
            if("state".equals(command)&&crashed.contains(key)){reply(id,new JSONObject().put("url",locations.getOrDefault(key,"about:blank")).put("loading",false).put("platform","android-webview").put("process_failed",true).put("error",errors.get(key)),null);return;}
            if(web==null)throw new IllegalStateException("Browser is not ready");
            if("bounds".equals(command)){bounds(web,request);reply(id,new JSONObject(),null);return;}
            if("state".equals(command)){reply(id,new JSONObject().put("url",web.getUrl()).put("title",web.getTitle()).put("loading",Boolean.TRUE.equals(loading.get(key))).put("can_go_back",web.canGoBack()).put("can_go_forward",web.canGoForward()).put("manual",manual.contains(key)).put("visible",web.getVisibility()==View.VISIBLE).put("platform","android-webview").put("error_code",errorCodes.getOrDefault(key,0)).put("error",errors.getOrDefault(key,"")),null);return;}
            if("takeover".equals(command)||"release".equals(command)){
                if("release".equals(command)&&manual.contains(key)&&request.optBoolean("agent"))throw new IllegalStateException("Only the user can return manual control");
                if("takeover".equals(command))manual.add(key);else manual.remove(key);reply(id,new JSONObject().put("manual",manual.contains(key)),null);return;
            }
            if(manual.contains(key)&&request.optBoolean("agent")&&!"snapshot".equals(command)&&!"screenshot".equals(command))throw new IllegalStateException("The user has taken control");
            if("navigate".equals(command))web.loadUrl(url(request.getString("url")));
            else if("back".equals(command))web.goBack();
            else if("forward".equals(command))web.goForward();
            else if("reload".equals(command))web.reload();
            else if("stop".equals(command))web.stopLoading();
            else if("screenshot".equals(command)){
                int width=web.getWidth(),height=web.getHeight();if(width<=0||height<=0||((long)width)*height>16777216)throw new IllegalStateException("Browser viewport is too large");
                Bitmap bitmap=Bitmap.createBitmap(width,height,Bitmap.Config.ARGB_8888);web.draw(new Canvas(bitmap));
                ByteArrayOutputStream bytes=new ByteArrayOutputStream();bitmap.compress(Bitmap.CompressFormat.JPEG,65,bytes);bitmap.recycle();
                reply(id,new JSONObject().put("data",Base64.encodeToString(bytes.toByteArray(),Base64.NO_WRAP)),null);return;
            }else{
                String script,selector=JSONObject.quote(request.optString("selector"));
                if("snapshot".equals(command))script="({url:location.href,title:document.title,text:(document.body?.innerText||'').slice(0,200000),links:[...document.querySelectorAll('a[href]')].slice(0,100).map(a=>({text:a.innerText,url:a.href}))})";
                else if("click".equals(command))script="(()=>{let e="+(request.optString("selector").isEmpty()?"document.elementFromPoint("+request.optInt("x")+","+request.optInt("y")+")":"document.querySelector("+selector+")")+";if(!e)throw Error('Element not found');e.click();return true})()";
                else if("type".equals(command))script="(()=>{let e=document.querySelector("+selector+");if(!e)throw Error('Element not found');e.focus();e.value="+JSONObject.quote(request.optString("text"))+";e.dispatchEvent(new Event('input',{bubbles:true}));e.dispatchEvent(new Event('change',{bubbles:true}));return true})()";
                else if("scroll".equals(command))script="(()=>{scrollBy("+request.optInt("x")+","+request.optInt("y")+");return true})()";
                else if("evaluate".equals(command))script=request.getString("script");
                else throw new IllegalArgumentException("Unsupported browser command");
                web.evaluateJavascript("(()=>{try{return {ok:true,value:("+script+")}}catch(e){return {ok:false,error:String(e.message||e)}}})()", output -> {
                    try{JSONObject result=new JSONObject(output);reply(id,result,result.optBoolean("ok")?null:result.optString("error","Browser execution failed"));}
                    catch(Exception error){reply(id,null,"Browser returned an invalid result");}
                });return;
            }
            reply(id,new JSONObject().put("accepted",true),null);
        }catch(Exception error){reply(id,null,error.getMessage());}
    }
    void destroy(){stopped=true;if(port!=null)port.close();for(WebView web:views.values()){root.removeView(web);web.destroy();}views.clear();manual.clear();loading.clear();errors.clear();errorCodes.clear();crashed.clear();locations.clear();}
}
