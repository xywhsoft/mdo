const encoder=new TextEncoder();
export function extensionTags(value){
  if(!Array.isArray(value))return [];
  return [...new Set(value.filter(tag=>typeof tag==="string").map(tag=>tag.trim()).filter(tag=>tag&&!/[\x00-\x1f\x7f]/.test(tag)&&encoder.encode(tag).length<=64))].slice(0,16);
}
export function extensionPresentation(item={}){
  const receipt=item.installation||{},rank=item.sort_order??receipt.sort_order??item.manifest?.sort_order??0;
  return {sort_order:Number.isInteger(rank)&&rank>=-2147483648&&rank<=2147483647?rank:0,
    sort_id:item.serial??receipt.serial??item.id,
    tags:extensionTags(item.tags??receipt.tags??item.manifest?.tags)};
}
export function compareExtensions(a,b){
  const delta=(b.sort_order||0)-(a.sort_order||0);if(delta)return delta;
  const left=a.sort_id,right=b.sort_id;if(left==null||right==null)return 0;
  if(typeof left==="number"&&typeof right==="number")return right-left;
  return String(left)===String(right)?0:String(left)>String(right)?-1:1;
}
