import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {test} from 'node:test';
import {executeDownloadSave,downloadSaveState,cancelDownloadSave} from '../../app/web/js/features/workbench/download-save.js';

function fixture(options={}){
  const bytes=options.bytes??Buffer.from('original response bytes'),sha=createHash('sha256').update(bytes).digest('hex');
  const context={extension:'save-owner',device:'local',project:'default',session:'original',namespace:'private',member:'worker',workspace:'D:\\task'};
  const metadata={id:'d-owned',state:'completed',size:bytes.length,sha256:sha};
  const calls=[],chunks=[];let dispatched=false,receive,publish,closed=false,cancelled=false,published=false,writes=0,blocked=options.backpressure||0;
  const state={get published(){return published;},get receive(){return receive;},get publish(){return publish;},get writes(){return writes;},get closed(){return closed;},get cancelled(){return cancelled;}};
  const fail=(code,message)=>({handled:true,result:{ok:false,error:{code,message,effect_may_have_occurred:dispatched}}});
  const api={async call(method,p={}){
    calls.push({...p,method,target_method:p.method});
    if(method==='sdk.context')return {...context,...options.context};
    if(method==='stream.open')return{handle:'str-'+'1'.repeat(32)};
    if(method==='operation.start'){
      if(p.method==='files.receive.start')receive=p.id;
      else if(p.method==='files.receive.publish'){publish=p.id;if(options.publishAckLost){published=true;throw Object.assign(Error('private/cache/secret'),{code:'network_error'});}if(!options.exists)published=true;}
      else throw Error('Unexpected operation');
      return{id:p.id};
    }
    if(method==='stream.write'){
      if(options.beforeWrite)await options.beforeWrite();
      writes++;
      if(blocked-->0)throw Object.assign(Error('No bytes committed'),{code:'sdk_stream_would_block'});
      chunks.push(Buffer.from(p.base64,'base64'));
      if(options.writeAckLost)throw Object.assign(Error('private/cache/secret'),{code:'network_error'});
      return{written:chunks.at(-1).length};
    }
    if(method==='stream.close'){closed=true;if(options.eofAckLost)throw Object.assign(Error('lost EOF'),{code:'network_error'});return{};}
    if(method==='operation.get'||method==='operation.wait'){
      if(p.id===receive)return{terminal:closed||cancelled,state:cancelled?'cancelled':closed?'succeeded':'running'};
      if(p.id===publish)return options.exists?{terminal:true,state:'failed',error:{code:'sdk_file_exists',may_have_completed:false}}:{terminal:true,state:'succeeded'};
      throw Object.assign(Error('not admitted'),{code:'sdk_operation_missing'});
    }
    if(method==='operation.cancel'){if(p.id===receive)cancelled=true;return{};}
    if(method==='operation.result'){
      if(p.id===receive)return{id:receive,state:'ready',source:'.mdo-receive-'+receive.slice(4)+'.part',source_revision:'file-revision',workspace:context.workspace,root_snapshot:'root',destination:'saved.bin',size:bytes.length,sha256:sha};
      if(p.id===publish)return{published:true,path:'saved.bin',source:'private staging name',size:bytes.length,sha256:sha,revision:'published-revision'};
    }
    if(method==='files.receive.release'&&options.cleanupFails)throw Object.assign(Error('private/cache/secret'),{code:'sdk_file_receive_cleanup'});
    if(['stream.release','files.receive.release','operation.release'].includes(method))return{};
    throw Error('Unexpected '+method);
  }};
  const view={async execute(command,p){
    assert.equal(command,'downloads');calls.push({native:p.action,...p});
    if(p.action==='get')return{...metadata,...options.metadata};
    if(options.sourceClosed)throw Object.assign(Error('Native source was released'),{code:'download_closed'});
    assert.equal(p.action,'read');const value=bytes.subarray(p.offset,p.offset+p.limit);
    return{id:metadata.id,offset:p.offset,next:p.offset+value.length,bytes:value.length,eof:p.offset+value.length===bytes.length,sha256:sha,base64:value.toString('base64'),...options.range};
  }};
  const guard=async()=>{
    if(options.cancelBeforePublish&&closed||options.cancelAfterPublish&&published)return fail('cancelled','Stopped by original task');
    return{state:{download_capability:true}};
  };
  return {calls,chunks,state,context,metadata,run:(overrides={})=>executeDownloadSave({params:{download:metadata.id,path:'saved.bin',root_snapshot:'root'},
    api,view,action:{id:'uia-'+'a'.repeat(32),context:options.noWorkspace?{}:context},guard,deadline:performance.now()+3000,setDispatched:value=>{dispatched=value;},fail,...overrides})};
}

test('save streams exact existing response chunks and publishes once without refetch or release',async()=>{
  const f=fixture({bytes:Buffer.alloc(131075,251)}),out=(await f.run()).result;
  assert.equal(out.ok,true);assert.equal(out.saved,true);assert.equal(out.source_retained,true);assert.equal(out.path,'saved.bin');
  assert.deepEqual(Buffer.concat(f.chunks),Buffer.alloc(131075,251));assert.equal(f.state.writes,3);
  assert.equal(f.calls.filter(c=>c.method==='operation.start').length,2);
  assert.equal(f.calls.filter(c=>c.target_method==='files.receive.publish').length,1);
  assert.equal(f.calls.filter(c=>c.native==='read').length,3);assert(f.calls.filter(c=>c.native==='read').every(c=>c.limit<=65536));
  assert.equal(f.calls.filter(c=>c.native==='get').length,2);assert(!f.calls.some(c=>['accept','release'].includes(c.native)));
  assert.equal(out.transfer.cleanup_pending,false);assert(!JSON.stringify(out).includes('base64'));assert(!JSON.stringify(out).includes('private staging'));
});
test('an empty response still needs explicit EOF and a verified publication',async()=>{
  const f=fixture({bytes:Buffer.alloc(0)}),out=(await f.run()).result;
  assert.equal(out.saved,true);assert.equal(f.state.closed,true);assert.equal(f.state.writes,0);assert(!f.calls.some(c=>c.native==='read'));
});
test('missing original workspace and a changed task fail before creating any byte sink',async()=>{
  for(const options of [{noWorkspace:true},{context:{workspace:'D:\\other'}},{context:{namespace:'other'}},{metadata:{state:'pending'}}]){
    const f=fixture(options),out=(await f.run()).result;assert.equal(out.ok,false);assert.equal(f.state.published,false);
    assert(!f.calls.some(c=>c.method==='stream.open'));
  }
});
test('a corrupt native range is rejected before any write and only its receiver is cancelled',async()=>{
  for(const range of [{bytes:1},{sha256:'0'.repeat(64)},{next:999},{base64:'not base64'},{base64:'!'.repeat(32)},{eof:false}]){
    const f=fixture({range}),out=(await f.run()).result;assert.equal(out.ok,false);assert.equal(out.error.code,'download_snapshot_changed');assert.equal(f.state.writes,0);assert.equal(f.state.published,false);assert.equal(f.state.cancelled,true);
  }
});
test('only explicit zero-write backpressure is retried',async()=>{
  const f=fixture({backpressure:2}),out=(await f.run()).result;
  assert.equal(out.saved,true);assert.equal(f.state.writes,3);assert.equal(f.chunks.length,1);
});
test('a lost byte acknowledgement aborts without sending the same bytes again',async()=>{
  const f=fixture({writeAckLost:true}),out=(await f.run()).result;
  assert.equal(out.ok,false);assert.equal(out.error.code,'network_error');assert.equal(f.state.writes,1);assert.equal(f.state.published,false);
  assert.equal(f.state.cancelled,true);assert(!JSON.stringify(out).includes('private/cache/secret'));
});
test('a lost EOF acknowledgement never starts publication',async()=>{
  const f=fixture({eofAckLost:true}),out=(await f.run()).result;
  assert.equal(out.ok,false);assert.equal(f.state.closed,true);assert.equal(f.state.published,false);assert(!f.state.publish);
});
test('a source closed during reception does not claim that its download is still retained',async()=>{
  const f=fixture({sourceClosed:true}),out=(await f.run()).result;
  assert.equal(out.ok,false);assert.equal(out.error.code,'download_closed');assert.equal(f.state.published,false);
  assert(!out.error.message.includes('remains available'));assert.equal(out.source_retained,undefined);
});
test('cancellation before publication clears only owned staging and retains the source response',async()=>{
  const f=fixture({cancelBeforePublish:true}),out=(await f.run()).result;
  assert.equal(out.error.code,'cancelled');assert.equal(f.state.published,false);assert.equal(out.transfer.cleanup_pending,false);
  assert(f.calls.some(c=>c.method==='files.receive.release'));assert(!f.calls.some(c=>c.native==='release'));
});
test('an uncertain publication preserves staging and reports its exact recovery identity',async()=>{
  const f=fixture({publishAckLost:true}),out=(await f.run()).result;
  assert.equal(out.ok,false);assert.equal(out.transfer.publication,f.state.publish);assert.equal(out.transfer.cleanup_pending,true);
  assert.equal(f.state.published,true);assert(f.calls.some(c=>c.method==='operation.cancel'&&c.id===f.state.publish));
  assert(!f.calls.some(c=>c.method==='files.receive.release'));assert(!f.calls.some(c=>c.native==='release'));
});
test('a confirmed destination conflict cleans staging but never replaces the target',async()=>{
  const f=fixture({exists:true}),out=(await f.run()).result;
  assert.equal(out.error.code,'sdk_file_exists');assert.equal(f.state.published,false);assert.equal(out.transfer.cleanup_pending,false);
  assert(f.calls.some(c=>c.method==='files.receive.release'));
});
test('cleanup failure cannot turn a verified saved file into a failed save',async()=>{
  const f=fixture({cleanupFails:true}),out=(await f.run()).result;
  assert.equal(out.ok,true);assert.equal(out.saved,true);assert.equal(out.transfer.cleanup_pending,true);
  assert(!JSON.stringify(out).includes('private/cache/secret'));
});
test('cancellation after verified publication still reports the existing saved file',async()=>{
  const f=fixture({cancelAfterPublish:true}),out=(await f.run()).result;
  assert.equal(out.ok,false);assert.equal(out.error.code,'cancelled');assert.equal(out.saved,true);assert.equal(out.file.sha256,f.metadata.sha256);
});

test('manual trusted UI saving uses the same verified producer without impersonating an Agent',async()=>{
  const f=fixture(),out=(await f.run({nativeAgent:false})).result;
  assert.equal(out.saved,true);assert(f.calls.filter(c=>c.native).every(c=>c.agent===false));
  assert(!f.calls.some(c=>c.native==='takeover'));
});

test('one browser shares a save fence across manual and Agent producers',async()=>{
  let release,entered;const waiting=new Promise(resolve=>{entered=resolve;}),hold=new Promise(resolve=>{release=resolve;});
  const f=fixture({beforeWrite:async()=>{entered();await hold;}}),browser='save-fence-qa';
  const first=f.run({browser});await waiting;
  try{
    const progress=downloadSaveState(browser);assert.equal(progress.total,f.metadata.size);assert.equal(progress.stage,'stream_write');
    assert.equal(Object.hasOwn(progress,'api'),false);assert.equal(Object.hasOwn(progress,'receive'),false);
    assert.equal((await f.run({browser,nativeAgent:false})).result.error.code,'browser_busy');
  }finally{release();}
  assert.equal((await first).result.saved,true);assert.equal(downloadSaveState(browser),null);
});

test('explicit UI cancellation stops only its known child operations and leaves no stale lock',async()=>{
  let release,entered;const waiting=new Promise(resolve=>{entered=resolve;}),hold=new Promise(resolve=>{release=resolve;});
  const f=fixture({beforeWrite:async()=>{entered();await hold;}}),browser='cancel-save-qa';
  const pending=f.run({browser});await waiting;
  assert.equal(await cancelDownloadSave('unrelated-browser'),false);
  assert.equal(await cancelDownloadSave(browser),true);assert.equal(downloadSaveState(browser).cancelled,true);release();
  assert.equal((await pending).result.error.code,'cancelled');assert.equal(f.state.published,false);
  assert.equal(downloadSaveState(browser),null);assert(f.calls.filter(c=>c.method==='operation.cancel').every(c=>c.id===f.state.receive||c.id===f.state.publish));
});
