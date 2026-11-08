import assert from 'node:assert/strict';
import {test} from 'node:test';
import {createDownloadUserActions} from '../../app/web/js/features/workbench/download-user.js';

function fixture(options={}){
  const calls=[],context={extension:'owner',device:'local',project:'original',session:'original-session',workspace:'D:\\original',namespace:'subtask',member:'worker',...options.baseContext};
  const hash='e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855';
  let bound,receive,publish,destination,serverContext={...context,...options.serverContext};const controller=new AbortController();
  const revision=options.rootSnapshot||'root',candidate={id:'manual-ui-qa',revision:1,session_revision:4,workspace:context.workspace,root_snapshot:revision};
  const api={async call(method,p={}){
    calls.push({...p,method,target_method:p.method});
    if(method==='files.stat')return{revision};
    if(method==='sdk.context')return bound;
    if(method==='browser.get')return{id:'manual-ui-qa',owner:context.extension,context:serverContext};
    if(method==='browser.workspace.lookup'){
      if(p.prepare_default){
        if(options.reviewError)throw Object.assign(Error('Unconfirmed directory review'),{code:options.reviewError});
        if(options.customDefault)throw Object.assign(Error('Custom original root'),{code:'browser_workspace_not_default'});
        const result={...candidate,prepare_default:true,root_exists:!options.missingDefault};
        if(options.missingDefault)delete result.root_snapshot;return result;
      }
      return candidate;
    }
    if(method==='browser.workspace.adopt'||method==='browser.workspace.prepare'){
      serverContext={...serverContext,workspace:candidate.workspace,workspace_snapshot:revision};
      if(options.lostAdopt||method==='browser.workspace.prepare'&&options.lostPrepare)throw Object.assign(Error('Lost binding reply'),{code:'connection_lost'});
      return{id:'manual-ui-qa',owner:context.extension,context:serverContext};
    }
    if(method==='stream.open')return{handle:'str-'+'1'.repeat(32)};
    if(method==='operation.start'){
      if(p.method==='files.receive.start'){receive=p.id;destination=p.params.destination;}
      else publish=p.id;
      if(options.lostStart)throw Object.assign(Error('private path should not be shown'),{code:'connection_lost'});
      return{id:p.id};
    }
    if(method==='operation.get')return{terminal:true,state:'succeeded'};
    if(method==='operation.result')return p.id===receive?{id:receive,state:'ready',source:'.mdo-receive-'+receive.slice(4)+'.part',
      workspace:bound.workspace,root_snapshot:revision,destination,source_revision:'source',size:0,sha256:hash}:
      {published:true,path:destination,size:0,sha256:hash,revision:'file',verdict:'verified'};
    if(method==='files.receive.list')return[];
    return{};
  }};
  const sdk={bind(scope){bound={...scope};return api;}};
  const execute=async(command,p={})=>{
    calls.push({native:command,...p});
    if(command==='state')return{manual:true,download_capability:true};
    return{id:'download',state:'completed',size:0,sha256:hash};
  };
  const browser={id:'manual-ui-qa',owner:context.extension,context:{...context,...options.context}};
  return{calls,context,controller,get bound(){return bound;},actions:createDownloadUserActions({browser,sdk,execute,signal:controller.signal,alive:options.alive})};
}

test('trusted manual save stays in its original owner/task/private workspace and never takes over',async()=>{
  const f=fixture();try{
    assert.deepEqual(f.bound,f.context);const result=await f.actions.save('download','empty.bin');assert.equal(result.saved,true);
    assert(f.calls.filter(c=>c.native==='downloads').every(c=>c.agent===false));assert(!f.calls.some(c=>c.native==='takeover'));
    assert.equal(f.calls.find(c=>c.target_method==='files.receive.start').params.destination,'empty.bin');
  }finally{f.actions.dispose();}
});

test('missing legacy roots and changed displaying devices fail before a file lookup',async()=>{
  for(const options of [{context:{workspace:''}},{alive:()=>false}]){
    const f=fixture(options);try{await assert.rejects(f.actions.save('download','empty.bin'));assert(!f.calls.some(c=>c.method==='files.stat'));}
    finally{f.actions.dispose();}
  }
});

test('review uses a fresh explicit SDK identity and does not replay publication',async()=>{
  const f=fixture();try{
    await f.actions.reconcile('sdk-'+'a'.repeat(32));await f.actions.reconcile('sdk-'+'a'.repeat(32));
    const jobs=f.calls.filter(c=>c.method==='operation.start');assert.equal(jobs.length,2);assert.notEqual(jobs[0].id,jobs[1].id);
    assert(jobs.every(c=>c.target_method==='files.receive.reconcile'));assert(!f.calls.some(c=>c.target_method==='files.receive.publish'));
  }finally{f.actions.dispose();}
});

test('lost recovery admission is cancelled by exact identity without retry or leaked paths',async()=>{
  const f=fixture({lostStart:true});try{
    await assert.rejects(f.actions.publish('sdk-'+'a'.repeat(32),'new.bin'),cause=>cause.code==='connection_lost'&&!cause.message.includes('private path'));
    const start=f.calls.filter(c=>c.method==='operation.start');assert.equal(start.length,1);
    assert.equal(f.calls.find(c=>c.method==='operation.cancel').id,start[0].id);assert.equal(f.actions.busy,false);
  }finally{f.actions.dispose();}
});

test('disposed panels cannot inspect or publish retained task data',async()=>{
  const f=fixture();f.actions.dispose();await assert.rejects(f.actions.inspect(),{code:'context_changed'});
  await assert.rejects(f.actions.publish('sdk-'+'a'.repeat(32),'new.bin'),{code:'context_changed'});assert.equal(f.calls.length,0);
});

test('legacy review only binds the original server task after explicit adoption',async()=>{
  const f=fixture({context:{workspace:''},serverContext:{workspace:''},rootSnapshot:'f-3-4-0-0'});try{
    const candidate=await f.actions.workspaceCandidate();assert.equal(f.actions.context.workspace,'');
    assert(!f.calls.some(c=>c.method==='browser.workspace.adopt'));
    await f.actions.adoptWorkspace(candidate);assert.equal(f.actions.context.workspace,f.context.workspace);
    assert.equal(f.actions.context.namespace,f.context.namespace);assert.equal(f.actions.context.workspace_snapshot,'f-3-4-0-0');
    assert.equal((await f.actions.save('download','empty.bin')).saved,true);
  }finally{f.actions.dispose();}
});

test('a lost binding reply is recovered by reading the server without repeated adoption',async()=>{
  const f=fixture({context:{workspace:''},serverContext:{workspace:''},rootSnapshot:'f-3-4-0-0',lostAdopt:true});try{
    const candidate=await f.actions.workspaceCandidate();await assert.rejects(f.actions.adoptWorkspace(candidate),{code:'connection_lost'});
    assert.equal(f.actions.context.workspace,'');assert.equal((await f.actions.workspaceCandidate()).bound,true);
    assert.equal(f.actions.context.workspace,f.context.workspace);assert.equal(f.calls.filter(c=>c.method==='browser.workspace.adopt').length,1);
  }finally{f.actions.dispose();}
});

test('foreign or malformed browser context cannot replace the bound original task',async()=>{
  for(const serverContext of [{namespace:'foreign'},{member:'foreign'},{session:'foreign'},{workspace:123}]){
    const f=fixture({context:{workspace:''},serverContext});try{
      await assert.rejects(f.actions.workspaceCandidate());assert.equal(f.actions.context.workspace,'');
      assert(!f.calls.some(c=>c.method==='browser.workspace.lookup'||c.method==='browser.workspace.adopt'));
    }finally{f.actions.dispose();}
  }
});

test('manual saving checks an adopted physical pin before opening any byte stream',async()=>{
  const f=fixture({context:{workspace_snapshot:'f-3-4-0-0'},rootSnapshot:'f-3-9-0-0'});try{
    await assert.rejects(f.actions.save('download','empty.bin'),{code:'workspace_snapshot_changed'});
    assert(!f.calls.some(c=>c.method==='stream.open'||c.native==='downloads'));
  }finally{f.actions.dispose();}
});

test('ordinary directory metadata changes do not invalidate an adopted manual save',async()=>{
  const f=fixture({context:{workspace_snapshot:'f-3-4-0-0'},rootSnapshot:'f-3-4-80-999'});try{
    assert.equal((await f.actions.save('download','empty.bin')).saved,true);
  }finally{f.actions.dispose();}
});

test('missing canonical default root is only prepared after explicit review and confirmation',async()=>{
  const f=fixture({baseContext:{project:'default'},missingDefault:true,rootSnapshot:'f-3-4-0-0'});try{
    assert.equal(f.actions.workspaceReviewAvailable,true);
    const candidate=await f.actions.workspaceCandidate();assert.equal(candidate.prepare_default,true);assert.equal(candidate.root_exists,false);
    assert(!Object.hasOwn(candidate,'root_snapshot'));assert(!f.calls.some(c=>c.method==='browser.workspace.prepare'||c.native));
    await f.actions.adoptWorkspace(candidate);
    const prepare=f.calls.find(c=>c.method==='browser.workspace.prepare');assert.equal(prepare.confirmed,true);assert.equal(prepare.root_exists,false);
    assert(!Object.hasOwn(prepare,'root_snapshot'));assert.equal(prepare.workspace,f.context.workspace);assert.equal(prepare.session_revision,4);
    assert.equal(f.actions.context.workspace_snapshot,'f-3-4-0-0');assert.equal(f.actions.workspaceReviewAvailable,false);
    assert.equal(f.actions.context.namespace,f.context.namespace);assert.equal(f.actions.context.member,f.context.member);
    assert((await f.actions.save('download','empty.bin')).saved);assert(!f.calls.some(c=>c.method==='files.resolve'));
  }finally{f.actions.dispose();}
});

test('lost default preparation reply is reread without repeating preparation or a download',async()=>{
  const f=fixture({baseContext:{project:'default'},missingDefault:true,rootSnapshot:'f-3-4-0-0',lostPrepare:true});try{
    const candidate=await f.actions.workspaceCandidate();await assert.rejects(f.actions.adoptWorkspace(candidate),{code:'connection_lost'});
    assert(!f.actions.context.workspace_snapshot);assert.equal((await f.actions.workspaceCandidate()).bound,true);
    assert.equal(f.actions.context.workspace_snapshot,'f-3-4-0-0');assert.equal(f.actions.workspaceReviewAvailable,false);
    assert.equal(f.calls.filter(c=>c.method==='browser.workspace.prepare').length,1);assert(!f.calls.some(c=>c.native||c.method==='stream.open'));
  }finally{f.actions.dispose();}
});

test('custom roots in the default project keep legacy restoration and never use default creation',async()=>{
  for(const legacy of [false,true]){
    const f=fixture({baseContext:{project:'default'},customDefault:true,rootSnapshot:'f-3-4-0-0',
      ...(legacy?{context:{workspace:''},serverContext:{workspace:''}}:{})});try{
      const candidate=await f.actions.workspaceCandidate();
      if(legacy){assert(!candidate.prepare_default);await f.actions.adoptWorkspace(candidate);assert(f.calls.some(c=>c.method==='browser.workspace.adopt'));}
      else assert.equal(candidate.bound,true);
      assert.equal(f.actions.workspaceReviewAvailable,false);assert(!f.calls.some(c=>c.method==='browser.workspace.prepare'||c.method==='files.resolve'));
    }finally{f.actions.dispose();}
  }
});

test('a stored physical pin cannot be replaced by rereading another server binding',async()=>{
  const f=fixture({baseContext:{project:'default'},context:{workspace_snapshot:'f-3-4-0-0'},serverContext:{workspace_snapshot:'f-3-9-0-0'}});try{
    await assert.rejects(f.actions.workspaceCandidate(),{code:'workspace_snapshot_changed'});
    assert.equal(f.actions.context.workspace_snapshot,'f-3-4-0-0');assert(!f.calls.some(c=>c.method==='browser.workspace.prepare'||c.native));
  }finally{f.actions.dispose();}
});

test('unconfirmed default review errors do not fall back to another recovery or create path',async()=>{
  const f=fixture({baseContext:{project:'default'},reviewError:'browser_workspace_unavailable'});try{
    await assert.rejects(f.actions.workspaceCandidate(),{code:'browser_workspace_unavailable'});
    const lookups=f.calls.filter(c=>c.method==='browser.workspace.lookup');assert.equal(lookups.length,1);assert.equal(lookups[0].prepare_default,true);
    assert(!f.calls.some(c=>c.method==='browser.workspace.adopt'||c.method==='browser.workspace.prepare'||c.method==='files.resolve'));
  }finally{f.actions.dispose();}
});
