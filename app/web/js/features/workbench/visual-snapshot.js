// Trusted-UI metadata only. Pixels, DOM text and browser-profile paths are never
// retained here. A reference is scoped to one browser and one completed image.
const issue=(code,message)=>Object.assign(new Error(message),{code});
const size=64,padding=8;
const hex=buffer=>Array.from(new Uint8Array(buffer),byte=>byte.toString(16).padStart(2,'0')).join('');
const token=()=>{const bytes=crypto.getRandomValues(new Uint8Array(24));return'vs-'+hex(bytes);};
const freeze=value=>{if(value&&typeof value==='object'){for(const item of Object.values(value))freeze(item);Object.freeze(value);}return value;};
const validRaster=raster=>raster&&Number.isInteger(raster.width)&&Number.isInteger(raster.height)&&raster.width>0&&raster.height>0&&
  raster.width<=4096&&raster.height<=4096&&raster.width*raster.height<=4194304&&raster.size===size&&raster.padding===padding&&
  raster.columns===Math.ceil(raster.width/size)&&raster.rows===Math.ceil(raster.height/size)&&Array.isArray(raster.hashes)&&
  raster.hashes.length===raster.columns*raster.rows&&raster.hashes.length<=1024&&raster.hashes.every(hash=>/^[a-f0-9]{64}$/.test(hash));

export async function hashVisualRaster(canvas){
  const {width,height}=canvas;
  if(!Number.isInteger(width)||!Number.isInteger(height)||width<1||height<1||width>4096||height>4096||width*height>4194304)
    throw issue('visual_raster_invalid','The masked visual raster exceeds its bounds');
  const context=canvas.getContext('2d'),pixels=context?.getImageData(0,0,width,height)?.data;
  if(!pixels||pixels.length!==width*height*4)throw issue('visual_raster_invalid','The masked visual raster cannot be inspected');
  const columns=Math.ceil(width/size),rows=Math.ceil(height/size),hashes=[];
  for(let row=0;row<rows;row++)for(let column=0;column<columns;column++){
    const left=Math.max(0,column*size-padding),top=Math.max(0,row*size-padding),
      right=Math.min(width,(column+1)*size+padding),bottom=Math.min(height,(row+1)*size+padding);
    const tile=new Uint8Array((right-left)*(bottom-top)*4);
    for(let y=top;y<bottom;y++)tile.set(pixels.subarray((y*width+left)*4,(y*width+right)*4),(y-top)*(right-left)*4);
    hashes.push(hex(await crypto.subtle.digest('SHA-256',tile)));
  }
  return{width,height,size,padding,columns,rows,hashes};
}

export function visualTileMatches(before,after,x,y){
  if(!validRaster(before)||!validRaster(after)||!Number.isFinite(x)||!Number.isFinite(y)||x<0||y<0||x>=before.width||y>=before.height)return false;
  for(const key of ['width','height','size','padding','columns','rows'])if(before[key]!==after[key])return false;
  const index=Math.floor(y/before.size)*before.columns+Math.floor(x/before.size);
  return /^[a-f0-9]{64}$/.test(before.hashes?.[index]||'')&&before.hashes[index]===after.hashes?.[index];
}

export async function visualPointUnchanged(canvas,before,x,y){
  if(!validRaster(before)||canvas.width!==before.width||canvas.height!==before.height||
     !Number.isFinite(x)||!Number.isFinite(y)||x<0||y<0||x>=before.width||y>=before.height)return false;
  const column=Math.floor(x/size),row=Math.floor(y/size),left=Math.max(0,column*size-padding),top=Math.max(0,row*size-padding),
    right=Math.min(canvas.width,(column+1)*size+padding),bottom=Math.min(canvas.height,(row+1)*size+padding);
  const pixels=canvas.getContext('2d')?.getImageData(left,top,right-left,bottom-top)?.data;
  if(!pixels||pixels.length!==(right-left)*(bottom-top)*4)throw issue('visual_raster_invalid','The current masked tile cannot be inspected');
  return hex(await crypto.subtle.digest('SHA-256',pixels))===before.hashes[row*before.columns+column];
}

export function createVisualSnapshotStore({now=()=>performance.now(),random=token,ttl=60000,limit=32,byteLimit=2097152}={}){
  const records=new Map();let retained=0;
  const remove=key=>{const row=records.get(key);if(row){retained-=row.bytes;records.delete(key);}};
  const sweep=()=>{const time=now();for(const [key,row] of records)if(time>=row.expires)remove(key);};
  function get(browser,id){
    sweep();const row=records.get(browser);
    if(!row||row.id!==id)throw issue('visual_reference_expired','Request a fresh screenshot for this browser before visual input');
    return row.value;
  }
  return Object.freeze({
    issue(browser,context,raster){
      sweep();
      if(!browser||!context?.document||typeof context.frame_stamp!=='string'||context.frame_stamp.length>65536||
         !validRaster(raster))throw issue('visual_snapshot_invalid','The completed image has no bounded document and raster proof');
      const proof={document:context.document,navigation:context.navigation,frame_stamp:context.frame_stamp,
        region:context.region,options:context.options,redactions:context.redactions,capture:context.capture,
        device_pixel_ratio:context.device_pixel_ratio};
      const encoded=JSON.stringify({context:proof,raster}),bytes=new TextEncoder().encode(encoded).length;
      if(bytes>196608||bytes>byteLimit)throw issue('visual_snapshot_limit','The completed image exceeds its visual metadata budget');
      const id=random();if(!/^vs-[a-f0-9]{48}$/.test(id))throw issue('visual_identity_unavailable','A secure visual reference could not be created');
      remove(browser);while(records.size>=limit||retained+bytes>byteLimit)remove(records.keys().next().value);
      const value=freeze(JSON.parse(encoded));records.set(browser,{id,expires:now()+ttl,bytes,value});retained+=bytes;
      return{id,width:raster.width,height:raster.height,coordinates:'image_pixels',expires_in_ms:ttl};
    },
    get,
    consume(browser,id){const value=get(browser,id);remove(browser);return value;},
    release(browser){remove(browser);},
    usage(){sweep();return{entries:records.size,bytes:retained};}
  });
}

export const visualSnapshots=createVisualSnapshotStore();
