import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';

const source=readFileSync(new URL('../../../app/web/js/features/workbench/document-driver.js',import.meta.url),'utf8')
  .replace("from './automation-runtime.js'",`from ${JSON.stringify(new URL('../../../app/web/js/features/workbench/automation-runtime.js',import.meta.url).href)}`);
export const {createDocumentDriver}=await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);
export const doc='d-12345678-1234-4123-8123-123456789abc',fd='fd-12345678-1234-4123-8123-123456789abc';
export const ref=`r1|f0|${fd}|${doc}|e7`;
export function fixture(){
  const state={fd,navigation:7,available:true,phase:'ready',ready:true,scale:true,clean:true,inputBusy:false,hook:null,calls:[]};
  const element={ref:'e7',frame:'f0',role:'textbox',name:'Root',editable:true};
  const meta=()=>({ok:true,document:doc,url:'https://name:password@fixture.test/',title:'Root',ready:'complete',viewport:{width:320,height:240}});
  const execute=async(command,params)=>{
    state.calls.push({command,params});
    if(command==='state')return{platform:'android-webview',loading:false,navigation:state.navigation,
      browser_use:{available:state.available},native_scale_ready:state.scale,input_busy:state.inputBusy,input_cleanup_confirmed:state.clean,
      isolated_world:{adapter:'android-main-document-v1',installed:true,ready:state.ready,phase:state.phase,native_document:state.fd}};
    assert.equal(command,'automation');assert.equal(params.agent,true);assert(params.timeout_ms>0&&params.timeout_ms<=10000);
    assert.equal(params.native_document,state.fd);assert.equal(params.navigation,state.navigation);
    const tail=params.script.split('({local_frame:true}).').at(-1),method=tail.slice(0,tail.indexOf('('));
    const options=JSON.parse(tail.slice(tail.indexOf('(')+1,-1));state.calls.at(-1).method=method;state.calls.at(-1).options=options;
    let value=meta();
    if(method==='observe')value={...meta(),elements:[element],frames:[{id:'f0',dom:true}],text:'Root fixture',visited:1,truncated:false};
    if(method==='locate')value={...meta(),element,visited:1};
    if(method==='prepare')value={...meta(),element,point:{x:20,y:30},geometry:[10,20,20,20,20,30]};
    if(method==='verify')value={...meta(),focused:true,verified:true};
    if(method==='optionList')value={...meta(),ref:'e7',frame:'f0',options:[{index:0,label:'One'}]};
    if(method==='selectOptions')value={...meta(),dispatched:true,verified:true};
    if(method==='scrollPoint')value={...meta(),point:options.point,hit_ref:'e7'};
    if(method==='captureArea')value={...meta(),region:options.mode==='element'?{x:10,y:20,width:20,height:20}:{x:0,y:0,width:320,height:240},
      scroll:{x:0,y:0},device_pixel_ratio:1,visited:4,redaction:'form-controls-and-opaque-content',
      redactions:options.mode==='element'?[{x:10,y:20,width:20,height:20}]:[{x:10,y:20,width:20,height:20},{x:100,y:20,width:100,height:100,frame_ref:'e8'}]};
    const result={ok:true,native_document:params.native_document,navigation:params.navigation,value};
    return await state.hook?.({method,options,result})||result;
  };
  return{state,execute,driver:createDocumentDriver({execute})};
}
