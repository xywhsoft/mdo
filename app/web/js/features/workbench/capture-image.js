// Images stay in memory. Known form controls and opaque content are masked on
// the trusted application's canvas; website DOM is never changed for capture.
export async function prepareScreenshot(native, area, after, params, inspect = {}) {
  if (typeof native.data !== "string" || native.data.length > 349528 || !/^[A-Za-z0-9+/]*={0,2}$/.test(native.data)) throw Error("Native screenshot has an invalid byte envelope");
  const bytes=Uint8Array.from(atob(native.data),char=>char.charCodeAt(0));
  const format=params.format||"png",mime=`image/${format}`;
  const sourceFormat=native.format||format;
  if(!['png','jpeg'].includes(sourceFormat)||!['png','jpeg'].includes(format))throw Error('Unsupported screenshot codec');
  if(bytes.length<24||bytes.length>(native.format?262144:(params.max_bytes||131072)))throw Error("Native screenshot exceeded its byte budget");
  let width=0,height=0;
  if(sourceFormat==="png"){
    if(![137,80,78,71,13,10,26,10].every((byte,i)=>bytes[i]===byte))throw Error("Invalid native PNG signature");
    const data=new DataView(bytes.buffer);width=data.getUint32(16);height=data.getUint32(20);
  }else{
    if(bytes[0]!==255||bytes[1]!==216)throw Error("Invalid native JPEG signature");
    let at=2;
    while(at+3<bytes.length){
      if(bytes[at++]!==255)throw Error("Invalid native JPEG structure");while(bytes[at]===255)at++;
      const marker=bytes[at++];if([216,217].includes(marker))continue;
      const size=bytes[at]*256+bytes[at+1];if(size<2||at+size>bytes.length)break;
      if([192,193,194,195,197,198,199,201,202,203,205,206,207].includes(marker)&&size>=7){height=bytes[at+3]*256+bytes[at+4];width=bytes[at+5]*256+bytes[at+6];break;}at+=size;
    }
  }
  if(!width||!height||width*height>4194304||width>4096||height>4096)throw Error("Native screenshot dimensions exceed their raster budget");
  const bitmap=await createImageBitmap(new Blob([bytes],{type:`image/${sourceFormat}`}));
  try{
    const ratio=Math.min(1,(params.max_width||1280)/width,(params.max_height||960)/height);
    const canvas=document.createElement("canvas");canvas.width=Math.max(1,Math.floor(width*ratio));canvas.height=Math.max(1,Math.floor(height*ratio));
    const context=canvas.getContext("2d",{alpha:false});if(!context)throw Error("Screenshot canvas is unavailable");
    context.drawImage(bitmap,0,0,canvas.width,canvas.height);context.fillStyle="#202020";
    const masks=[...area.redactions,...after.redactions];
    for(const box of masks){
      const x=(box.x-area.region.x)*canvas.width/area.region.width,y=(box.y-area.region.y)*canvas.height/area.region.height;
      const w=box.width*canvas.width/area.region.width,h=box.height*canvas.height/area.region.height;
      context.fillRect(Math.floor(x)-2,Math.floor(y)-2,Math.ceil(w)+4,Math.ceil(h)+4);
    }
    for(let attempt=0;attempt<4;attempt++){
      const encoded=canvas.toDataURL(mime,0.8).split(",")[1];
      const size=encoded.length/4*3-(encoded.endsWith("==")?2:encoded.endsWith("=")?1:0);
      if(size<=(params.max_bytes||131072)){
        await inspect.raster?.(canvas);
        return {image:{media_type:mime,base64:encoded,width:canvas.width,height:canvas.height,bytes:size},
          scaled:native.scale*(area.device_pixel_ratio||1)<0.999||ratio<1||attempt>0,redacted_regions:Math.max(area.redactions.length,after.redactions.length)};
      }
      const next=document.createElement("canvas");next.width=Math.max(1,Math.floor(canvas.width*0.65));next.height=Math.max(1,Math.floor(canvas.height*0.65));
      const target=next.getContext("2d");if(!target)throw Error("Screenshot canvas is unavailable");target.drawImage(canvas,0,0,next.width,next.height);
      canvas.width=next.width;canvas.height=next.height;context.drawImage(next,0,0);
    }
    throw Error("Masked screenshot exceeds its byte budget; capture a smaller area");
  }finally{bitmap.close();}
}
