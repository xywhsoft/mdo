import assert from 'node:assert/strict';
import {test} from 'node:test';
import {directoryManifest,safeUploadName} from '../../app/web/js/features/workbench/upload-tree.js';

const snapshot=()=>({format:'mdo.file-tree.v1',revision:'t1-'+'a'.repeat(64),root:{path:'.',revision:'f-root'},
  directories:[{path:'nested'},{path:'nested/empty'}],directory_count:2,
  files:[{path:'nested/a.txt',size:3,sha256:'b'.repeat(64),revision:'f-one'},{path:'empty.txt',size:0,sha256:'c'.repeat(64),revision:'f-two'}],
  file_count:2,total_bytes:3});
test('directory wire manifests preserve relative files/empty directories and omit task metadata',()=>{
  const tree=directoryManifest(snapshot(),'selected-中文');
  assert.deepEqual(tree,{name:'selected-中文',directories:['nested','nested/empty'],
    files:[{path:'nested/a.txt',size:3,sha256:'b'.repeat(64)},{path:'empty.txt',size:0,sha256:'c'.repeat(64)}]});
  assert(!JSON.stringify(tree).includes('revision'));assert(!JSON.stringify(tree).includes('f-root'));
});
test('ambiguous basenames and incomplete/coerced directory metadata are rejected',()=>{
  for(const name of ['.','..','CON','COM¹.txt','x/y','x\\y','tail.','x\0y','x'.repeat(241),'\ud800'])assert(!safeUploadName(name),name);
  assert(safeUploadName('文'.repeat(78)+'.txt'));assert(safeUploadName('n'.repeat(240)));assert(safeUploadName('\ufeffname'));
  const changes=[
    t=>t.file_count--,t=>t.directory_count++,t=>t.total_bytes++,t=>t.revision='f-not-tree',
    t=>t.root.path='other',t=>t.root.revision='',t=>t.files[0].size='3',t=>t.files[0].sha256='b'.repeat(63),
    t=>t.files[0].revision='',t=>t.files[0].path='../escape',t=>t.files[0].path='nested\\a.txt',
    t=>t.files[0].path='missing/a',t=>t.files[0].path='nested',t=>t.files[1].path=t.files[0].path,
    t=>t.directories[1].path='nested/absent/empty',t=>t.directories[1].path='nested',
    t=>t.files[0].path='nested/'+('a'.repeat(241)),t=>t.files[0].size=33554433,
  ];
  for(const change of changes){const tree=snapshot();change(tree);assert.throws(()=>directoryManifest(tree,'selected'),{code:'upload_snapshot_invalid'});}
});
test('directory count/depth/byte budgets are complete bounds rather than truncation',()=>{
  const tree=snapshot();tree.files=Array.from({length:128},(_,i)=>({path:'f'+i,size:0,sha256:'a'.repeat(64),revision:'f-'+i}));
  tree.file_count=128;tree.total_bytes=0;assert.equal(directoryManifest(tree,'selected').files.length,128);
  tree.files.push({...tree.files[0],path:'overflow'});tree.file_count++;assert.throws(()=>directoryManifest(tree,'selected'));
  const deep=snapshot();let path='';deep.directories=[];
  for(let i=0;i<16;i++){path+=(path?'/':'')+'d';deep.directories.push({path});}
  deep.directory_count=16;deep.files[0].path=path+'/a';assert(directoryManifest(deep,'selected'));
  deep.directories.push({path:path+'/deeper'});deep.directory_count++;assert.throws(()=>directoryManifest(deep,'selected'));
});
