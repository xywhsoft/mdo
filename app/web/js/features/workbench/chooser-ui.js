// This trusted UI owns picker controls; webpage labels never become markup.
export function createBrowserChoosers({execute,onControl,onChange,onError}){
  const make=(tag,className,text)=>{const node=document.createElement(tag);node.className=className;if(text)node.textContent=text;return node;};
  const root=make('section','wb-browser-dialog'),title=make('strong','wb-dialog-title','网页正在选择文件'),
    message=make('p','wb-dialog-message','Agent 可为当前选择器提供任务文件。接管后如需使用系统选择器，请重新点击网页的选择文件按钮。'),
    controls=make('div','wb-dialog-actions'),cancel=make('button','wb-button','取消选择'),takeover=make('button','wb-button','接管浏览器');
  root.setAttribute('role','group');root.setAttribute('aria-label','等待处理的文件选择器');root.hidden=true;
  cancel.type=takeover.type='button';controls.append(cancel,takeover);root.append(title,message,controls);
  let pending=null,busy=false,disposed=false,generation=0;
  const hide=()=>{root.hidden=true;pending=null;onChange?.();};
  async function act(manual){
    if(disposed||busy||!pending)return;busy=true;generation++;cancel.disabled=takeover.disabled=true;const current=pending;
    try{
      if(manual){await execute('takeover');onControl?.();}
      else await execute('chooser',{action:'cancel',chooser:current.id});
      if(!disposed)hide();
    }catch(error){if(!disposed)onError?.(error);}
    finally{busy=false;cancel.disabled=takeover.disabled=false;}
  }
  cancel.onclick=()=>{void act(false);};takeover.onclick=()=>{void act(true);};
  return Object.freeze({element:root,async refresh(state){
    if(disposed)return;const observed=++generation;
    if(!state.chooser_open||state.dialog_open||state.popup_open){if(pending)hide();return;}if(busy)return;
    const response=await execute('chooser',{action:'list'});if(disposed||observed!==generation)return;
    pending=response.choosers?.[0]||null;if(!pending){hide();return;}
    title.textContent=pending.multiple?'网页正在选择多个文件':'网页正在选择文件';root.hidden=false;onChange?.();
  },dispose(){disposed=true;generation++;root.hidden=true;pending=null;cancel.onclick=takeover.onclick=null;}});
}
