package org.xserver.android;

import java.util.HashSet;
import java.util.Locale;
import java.util.Set;
import org.json.JSONArray;
import org.json.JSONObject;

/** Bounded, immutable metadata from an original paused event. This is not a
 * request selector, stream claim or permission. It retains no URL, request,
 * headers, credentials or caller-owned JSON. Only a sanitized advisory basename
 * and normalized media type survive. No URL fallback, content sniff or refetch.
 * Only explicit attachments/known binary Document responses are candidates;
 * native download-intent/provider qualification remains a separate gate. */
final class MdoBrowserCdpResponse {
    final String kind,reason,name,mime,frame;final int status;final long declaredLength;final boolean exactTextLength;
    private MdoBrowserCdpResponse(String kind,String reason,int status,long length,boolean exactTextLength){
        this(kind,reason,status,length,exactTextLength,"download.bin","",null);
    }
    private MdoBrowserCdpResponse(String kind,String reason,int status,long length,boolean exactTextLength,String name,String mime,String frame){
        this.kind=kind;this.reason=reason;this.status=status;declaredLength=length;this.exactTextLength=exactTextLength;this.name=name;this.mime=mime;this.frame=frame;
    }
    private static MdoBrowserCdpResponse pass(String reason,int status){return new MdoBrowserCdpResponse("pass",reason,status,-1,false);}
    private static MdoBrowserCdpResponse invalid(){return new MdoBrowserCdpResponse("invalid","response_metadata_invalid",0,-1,false);}
    private static boolean token(String value){
        if(value.isEmpty())return false;
        for(int i=0;i<value.length();i++){char c=value.charAt(i);
            if(!(c>='a'&&c<='z'||c>='A'&&c<='Z'||c>='0'&&c<='9'||"!#$%&'*+-.^_`|~".indexOf(c)>=0))return false;
        }return true;
    }
    private static int whitespace(String value,int i){while(i<value.length()&&(value.charAt(i)==' '||value.charAt(i)=='\t'))i++;return i;}
    private static String trim(String value){int start=whitespace(value,0),end=value.length();while(end>start&&(value.charAt(end-1)==' '||value.charAt(end-1)=='\t'))end--;return value.substring(start,end);}
    private static int bytes(String value){
        int count=0;for(int i=0;i<value.length();i++){char c=value.charAt(i);
            if(c<32&&c!='\t'||c==127)return -1;
            if(Character.isHighSurrogate(c)){if(i+1==value.length()||!Character.isLowSurrogate(value.charAt(++i)))return -1;count+=4;}
            else if(Character.isLowSurrogate(c))return -1;else count+=c<128?1:c<2048?2:3;
        }return count;
    }
    /** Validate parameter syntax without decoding or retaining any value. */
    private static String parameterized(String value,boolean media){
        int semicolon=value.indexOf(';');String head=trim(semicolon<0?value:value.substring(0,semicolon));
        if(media){int slash=head.indexOf('/');if(slash<=0||!token(head.substring(0,slash))||!token(head.substring(slash+1)))return null;}
        else if(!token(head))return null;
        Set<String> names=new HashSet<>();int i=semicolon<0?value.length():semicolon;
        while(i<value.length()){
            if(value.charAt(i++)!=';')return null;i=whitespace(value,i);int start=i;
            while(i<value.length()&&value.charAt(i)!='='&&value.charAt(i)!=' '&&value.charAt(i)!='\t'&&value.charAt(i)!=';')i++;
            String name=value.substring(start,i);if(!token(name)||names.size()>=32||!names.add(name.toLowerCase(Locale.ROOT)))return null;
            i=whitespace(value,i);if(i==value.length()||value.charAt(i++)!='=')return null;i=whitespace(value,i);
            if(i==value.length())return null;
            if(value.charAt(i)=='"'){
                i++;boolean closed=false;while(i<value.length()){
                    char c=value.charAt(i++);if(c=='"'){closed=true;break;}
                    if(c=='\\'){if(i==value.length())return null;i++;}
                }if(!closed)return null;
            }else{
                start=i;while(i<value.length()&&value.charAt(i)!=';'&&value.charAt(i)!=' '&&value.charAt(i)!='\t')i++;
                if(!token(value.substring(start,i)))return null;
            }
            i=whitespace(value,i);if(i<value.length()&&value.charAt(i)!=';')return null;
        }
        return head.toLowerCase(Locale.ROOT);
    }
    private static long length(String value){
        value=trim(value);if(value.isEmpty())return -1;long result=0;
        for(int i=0;i<value.length();i++){int digit=value.charAt(i)-'0';
            if(digit<0||digit>9||result>(Long.MAX_VALUE-digit)/10)return -1;result=result*10+digit;
        }return result;
    }
    /** RFC 6266/8187 display hints, never filesystem authority. Invalid or
     * unsupported extended encodings fall back to the ordinary parameter. */
    private static String extendedName(String value){
        int first=value.indexOf('\''),second=first<0?-1:value.indexOf('\'',first+1);
        if(first<=0||second<0||second-first>65)return null;
        String encoding=value.substring(0,first),language=value.substring(first+1,second);
        if(!language.matches("[A-Za-z0-9-]*"))return null;
        java.nio.charset.Charset charset;
        if(encoding.equalsIgnoreCase("UTF-8"))charset=java.nio.charset.StandardCharsets.UTF_8;
        else if(encoding.equalsIgnoreCase("ISO-8859-1"))charset=java.nio.charset.StandardCharsets.ISO_8859_1;
        else return null;
        byte[] bytes=new byte[value.length()-second-1];int used=0;
        try{
            for(int i=second+1;i<value.length();i++){
                char c=value.charAt(i);
                if(c=='%'){
                    if(i+2>=value.length())return null;int high=Character.digit(value.charAt(++i),16),low=Character.digit(value.charAt(++i),16);
                    if(high<0||low<0)return null;bytes[used++]=(byte)(high<<4|low);
                }else{
                    if(!(c>='a'&&c<='z'||c>='A'&&c<='Z'||c>='0'&&c<='9'||"!#$&+-.^_`|~".indexOf(c)>=0))return null;
                    bytes[used++]=(byte)c;
                }
            }
            return charset.newDecoder().onMalformedInput(java.nio.charset.CodingErrorAction.REPORT)
                .onUnmappableCharacter(java.nio.charset.CodingErrorAction.REPORT).decode(java.nio.ByteBuffer.wrap(bytes,0,used)).toString();
        }catch(java.nio.charset.CharacterCodingException failure){return null;}
        finally{java.util.Arrays.fill(bytes,(byte)0);}
    }
    private static String basename(String value){
        if(value==null)return "download.bin";
        value=trim(value.substring(Math.max(value.lastIndexOf('/'),value.lastIndexOf('\\'))+1));
        if(value.isEmpty()||value.length()>200||bytes(value)>240||value.endsWith(".")||value.endsWith(" "))return "download.bin";
        for(int i=0;i<value.length();){int c=value.codePointAt(i);i+=Character.charCount(c);
            if(Character.isISOControl(c)||Character.getType(c)==Character.FORMAT||"<>:\"/\\|?*".indexOf(c)>=0)return "download.bin";}
        String stem=value.split("\\.",2)[0].toUpperCase(Locale.ROOT);
        if(stem.endsWith(" ")||stem.matches("CON|PRN|AUX|NUL|CONIN\\$|CONOUT\\$|CLOCK\\$|(?:COM|LPT)[0-9\u00b9\u00b2\u00b3]"))return "download.bin";
        return value;
    }
    private static String filename(String validated){
        if(validated==null)return "download.bin";
        String regular=null,extended=null;int at=validated.indexOf(';');
        // The same bounded field already passed parameterized(). Extract only
        // these two values, honoring quoted escapes and semicolons in strings.
        while(at>=0&&at<validated.length()){
            at=whitespace(validated,at+1);int start=at;while(validated.charAt(at)!='='&&validated.charAt(at)!=' '&&validated.charAt(at)!='\t')at++;
            String name=validated.substring(start,at);at=whitespace(validated,at);at=whitespace(validated,at+1);
            boolean quoted=validated.charAt(at)=='"';String value;
            if(quoted){StringBuilder text=new StringBuilder();at++;while(validated.charAt(at)!='"'){
                    char c=validated.charAt(at++);if(c=='\\')c=validated.charAt(at++);text.append(c);
                }at++;value=text.toString();
            }else{start=at;while(at<validated.length()&&validated.charAt(at)!=';'&&validated.charAt(at)!=' '&&validated.charAt(at)!='\t')at++;value=validated.substring(start,at);}
            if(name.equalsIgnoreCase("filename"))regular=value;
            else if(name.equalsIgnoreCase("filename*")&&!quoted)extended=extendedName(value);
            at=whitespace(validated,at);
        }
        return basename(extended==null?regular:extended);
    }
    static MdoBrowserCdpResponse inspect(JSONObject event){
        MdoBrowserCdpResponse value=inspectMetadata(event);
        String frame=event!=null&&MdoBrowserCdpNetworkFrames.identity(event.opt("frameId"))?(String)event.opt("frameId"):null;
        return frame==null||value.frame!=null?value:new MdoBrowserCdpResponse(value.kind,value.reason,value.status,value.declaredLength,value.exactTextLength,value.name,value.mime,frame);
    }
    private static MdoBrowserCdpResponse inspectMetadata(JSONObject event){
        if(event==null)return invalid();
        boolean hasStatus=event.has("responseStatusCode"),hasError=event.has("responseErrorReason");
        if(!hasStatus&&!hasError)return pass("request_stage",0);
        if(hasError){Object error=event.opt("responseErrorReason");
            return !hasStatus&&error instanceof String&&token((String)error)&&((String)error).length()<=64?pass("response_failed",0):invalid();
        }
        Object number=event.opt("responseStatusCode");if(!(number instanceof Integer)&&!(number instanceof Long))return invalid();
        long raw=((Number)number).longValue();if(raw<100||raw>599)return invalid();int status=(int)raw;
        if(status<200||status>=300)return pass("response_status",status);
        if(status==204||status==205||status==206)return pass("response_no_full_body",status);
        Object resource=event.opt("resourceType");if(!(resource instanceof String))return invalid();
        if(!"Document".equals(resource))return pass("subresource",status);
        Object request=event.opt("request");if(!(request instanceof JSONObject))return invalid();
        Object method=((JSONObject)request).opt("method");if(!(method instanceof String)||((String)method).length()>32||!token((String)method))return invalid();
        if("HEAD".equalsIgnoreCase((String)method)||"CONNECT".equalsIgnoreCase((String)method))return pass("response_no_full_body",status);
        Object headers=event.opt("responseHeaders");if(!(headers instanceof JSONArray)||((JSONArray)headers).length()>128)return invalid();
        JSONArray entries=(JSONArray)headers;String disposition=null,dispositionValue=null,type=null;long declared=-1;
        boolean hasLength=false,hasEncoding=false,identityEncoding=true,transferEncoded=false;int total=0;
        for(int i=0;i<entries.length();i++){
            Object row=entries.opt(i);if(!(row instanceof JSONObject))return invalid();
            Object name=((JSONObject)row).opt("name"),value=((JSONObject)row).opt("value");
            if(!(name instanceof String)||!(value instanceof String))return invalid();
            String n=(String)name,v=(String)value;if(n.length()>256||!token(n)||v.length()>8192)return invalid();
            int size=bytes(v);if(size<0||size>65536-total-n.length())return invalid();total+=size+n.length();
            if(n.equalsIgnoreCase("Content-Disposition")){if(disposition!=null)return invalid();disposition=parameterized(v,false);if(disposition==null)return invalid();dispositionValue=v;}
            else if(n.equalsIgnoreCase("Content-Type")){if(type!=null)return invalid();type=parameterized(v,true);if(type==null)return invalid();}
            else if(n.equalsIgnoreCase("Content-Length")){if(hasLength)return invalid();hasLength=true;declared=length(v);if(declared<0)return invalid();}
            else if(n.equalsIgnoreCase("Content-Encoding")){if(hasEncoding)return invalid();hasEncoding=true;String coding=trim(v);identityEncoding=token(coding)&&coding.equalsIgnoreCase("identity");}
            else if(n.equalsIgnoreCase("Transfer-Encoding"))transferEncoded=true;
        }
        boolean attachment="attachment".equals(disposition);
        boolean binary="application/octet-stream".equals(type)||"application/x-download".equals(type)||"application/force-download".equals(type);
        if(attachment||binary)return new MdoBrowserCdpResponse("candidate",attachment?"attachment":"binary_document",status,declared,hasLength&&identityEncoding&&!transferEncoded,
            filename(dispositionValue),type!=null&&type.length()<=128?type:"",MdoBrowserCdpNetworkFrames.identity(event.opt("frameId"))?(String)event.opt("frameId"):null);
        return pass("document",status);
    }
}
