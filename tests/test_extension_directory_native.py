"""Native filesystem contract tests, using small project-local fixture trees."""
import argparse
import json
from pathlib import Path
import shutil
import uuid
import zipfile

from native_extension_runner import run
from test_extension_zip_native import headers

ROOT = Path(__file__).resolve().parents[1]
HOME_STUBS = r'''
static xroot QaRoot;
static bool QaParents(cstr Path){
 char* Copy=xrtStrDup(Path);if(!Copy)return false;bool Ok=true;
 for(char* P=Copy;Ok&&*P;P++)if(*P=='/'){*P=0;xfileinfo I;
  if(!xrtRootStat(QaRoot,Copy,false,&I)){if(xrtErrorKind(xrtGetError())!=XERR_NOT_FOUND){Ok=false;break;}xrtClearError();Ok=xrtRootDirCreate(QaRoot,Copy,0700);}
  else Ok=I.Type==XFILE_TYPE_DIRECTORY;*P='/';}
 xrtFree(Copy);return Ok;
}
xfile MdoHomeOpenRead(cstr Path){xfileoptions O;xrtFileOptionsInit(&O);O.Flags=XFILE_READ|XFILE_NOFOLLOW;return xrtRootFileOpen(QaRoot,Path,&O);}
xfile MdoHomeOpenWrite(cstr Path,uint32 Flags){if(!QaParents(Path))return NULL;xfileoptions O;xrtFileOptionsInit(&O);O.Flags=Flags|XFILE_NOFOLLOW;return xrtRootFileOpen(QaRoot,Path,&O);}
xdir MdoHomeOpenDirectory(cstr Path,uint32 Flags){return xrtRootDirOpen(QaRoot,Path,Flags);}
bool MdoHomeExternalStat(cstr Path,bool* Exists,xfileinfo* I){if(xrtRootStat(QaRoot,Path,false,I)){*Exists=true;return true;}if(xrtErrorKind(xrtGetError())==XERR_NOT_FOUND){*Exists=false;xrtClearError();return true;}return false;}
bool MdoHomeAtomicWrite(cstr Path,const void* Data,size_t Size,bool Backup){
 (void)Backup;if(!QaParents(Path)||!MdoExtensionRelativePath(Path))return false;char* Native=xrtPathJoin(xrtRootPath(QaRoot),Path);
 bool Ok=Native&&xrtFileWriteAtomic(Native,(xbytesview){Data,Size});xrtFree(Native);return Ok;
}
bool MdoHomeRemove(cstr Path,bool Backup){(void)Backup;return xrtRootRemove(QaRoot,Path);}
bool MdoHomeCreateDirectory(cstr Path){return QaParents(Path)&&xrtRootDirCreate(QaRoot,Path,0700);}
xroot MdoHomeOpenStorageDirectory(cstr Path){
 if(!QaParents(Path))return NULL;xfileinfo I;
 if(!xrtRootStat(QaRoot,Path,false,&I)){if(xrtErrorKind(xrtGetError())!=XERR_NOT_FOUND)return NULL;xrtClearError();if(!xrtRootDirCreate(QaRoot,Path,0700))return NULL;}
 else if(I.Type!=XFILE_TYPE_DIRECTORY)return NULL;return xrtRootOpenIn(QaRoot,Path);
}
bool MdoHomeRenameNoReplace(cstr From,cstr To){
 if(!MdoExtensionRelativePath(From)||!MdoExtensionRelativePath(To))return false;
 char* A=xrtPathJoin(xrtRootPath(QaRoot),From),*B=xrtPathJoin(xrtRootPath(QaRoot),To);bool Ok=A&&B&&xrtPathRename(A,B,false);xrtFree(A);xrtFree(B);return Ok;
}
bool MdoHomeSetMode(cstr Path,uint32 Mode){(void)Path;(void)Mode;return true;}
'''
TEST = r'''
static bool QaJournal(cstr Task,cstr Id,cstr Phase,cstr Operation,bool Old){
 char* Base=xrtFormat(MDO_EXT_TASKS "/%s",Task),*Path=xrtFormat("%s/transaction.json",Base);bool Ok=MdoExtEnsure(Base);xvalue* J=xrtValueObject();
 Ok=Ok&&MdoExtPut(J,"kind","mcp")&&MdoExtPut(J,"id",Id)&&MdoExtPut(J,"phase",Phase)&&MdoExtPut(J,"operation",Operation)&&xrtValueObjectSetNew(J,XRT_STR_LITERAL("old_exists"),xrtValueBool(Old));
 if(Old){xvalue* Before=xrtValueObject();MdoExtPut(Before,"source","manual");xrtValueObjectSetNew(Before,XRT_STR_LITERAL("enabled"),xrtValueBool(false));Ok=Ok&&xrtValueObjectSetNew(J,XRT_STR_LITERAL("before"),Before);}
 else Ok=Ok&&xrtValueObjectSetNew(J,XRT_STR_LITERAL("before"),xrtValueNull());
 Ok=Ok&&MdoExtSaveJson(Path,J,MDO_EXT_STATE_LIMIT);xrtValueRelease(J);xrtFree(Base);xrtFree(Path);return Ok;
}
MDO_EXPORT mdo_result mdoCommandRun(const mdo_command_context_v1* C,const xvalue* A,xvalue** R){
 (void)C;const xvalue* P=xrtValueArrayGet(A,0);xstrview Root={0};xrtValueGetString(xrtValueObjectGet(P,XRT_STR_LITERAL("root")),&Root);
 QaRoot=xrtRootOpen(Root.Data);if(!QaRoot||!MdoExtensionsInit())return MDO_RESULT_ERROR;
 *R=xrtValueObject();char Error[512]={0};xvalue* Rows=xrtValueArray();
 for(size_t K=0;K<9;K++){
  xvalue* Items=MdoExtensionsList(MdoExtensionKinds[K],Error),*Row=xrtValueObject();
  const xvalue* First=xrtValueArrayGet(Items,0);bool Valid=false;xrtValueGetBool(xrtValueObjectGet(First,XRT_STR_LITERAL("valid")),&Valid);
  MdoExtPut(Row,"kind",MdoExtensionKinds[K]);xrtValueObjectSetNew(Row,XRT_STR_LITERAL("listed"),xrtValueBool(Items&&xrtValueCount(Items)==1&&Valid));
  char* Entry=MdoExtensionsEntryPath(MdoExtensionKinds[K],"qa-local",Error);xrtValueObjectSetNew(Row,XRT_STR_LITERAL("entry"),xrtValueBool(Entry&&strstr(Entry,"/qa-local/")!=NULL));
  xrtFree(Entry);xrtValueRelease(Items);xrtValueArrayAppendNew(Rows,Row);
 }
 xrtValueObjectSetNew(*R,XRT_STR_LITERAL("types"),Rows);
 char Before[65],After[65],ArchiveHash[65];uint64 Size=0;
 bool Fingerprint=MdoExtensionsFingerprint("mcp","qa-local",Before,NULL,Error);
 bool Packed=MdoExtensionsPack("mcp","qa-local","data/extension-tasks/export/result.zip",NULL,ArchiveHash,&Size,Error);
 xrtValueObjectSetNew(*R,XRT_STR_LITERAL("packed"),xrtValueBool(Packed));MdoExtPut(*R,"pack_error",Error);
 bool Changed=MdoHomeAtomicWrite("mcp/qa-local/include/helper.h","changed",7,false)&&MdoExtensionsFingerprint("mcp","qa-local",After,NULL,Error)&&strcmp(Before,After);
 xrtValueObjectSetNew(*R,XRT_STR_LITERAL("auxiliary_changes_digest"),xrtValueBool(Fingerprint&&Changed));
 const char* State="{\"mcp:qa-local\":{\"source\":\"store\",\"enabled\":false}}";
 bool Saved=MdoHomeAtomicWrite(MDO_EXT_STATE,State,strlen(State),false);xvalue* M=Saved?MdoExtensionsRead("mcp","qa-local",Error):NULL;bool Enabled=true;
 xrtValueGetBool(xrtValueObjectGet(M,XRT_STR_LITERAL("enabled")),&Enabled);
 xrtValueObjectSetNew(*R,XRT_STR_LITERAL("disabled"),xrtValueBool(M&&!Enabled));xrtValueRelease(M);
 bool Blocked=!MdoExtensionsPack("mcp","qa-local","data/extension-tasks/export/rejected.zip",NULL,ArchiveHash,&Size,Error);
 xrtValueObjectSetNew(*R,XRT_STR_LITERAL("store_export_blocked"),xrtValueBool(Blocked));
 bool Unsafe=!MdoExtensionsRead("mcp","../outside",Error)&&!MdoExtensionsPack("skills","qa-local","outside.zip",NULL,ArchiveHash,&Size,Error);
 xrtValueObjectSetNew(*R,XRT_STR_LITERAL("unsafe_paths_blocked"),xrtValueBool(Unsafe));
 xvalue* Cycles=xrtValueArray();
 for(size_t K=0;K<9;K++){
  const char* Kind=MdoExtensionKinds[K];cstr Id="V--u87hExb-w2R9P--SuTetSA3Hd0dq7";
  char Archive[160];snprintf(Archive,sizeof(Archive),"data/extension-tasks/fixture/%s.zip",Kind);xvalue* Installed=NULL,*Removed=NULL;
  bool Ok=MdoExtensionsInstall(Kind,Id,Archive,NULL,true,NULL,&Installed,Error);xrtValueRelease(Installed);
  xvalue* Current=Ok?MdoExtensionsRead(Kind,Id,Error):NULL;
  Ok=Ok&&Current&&!strcmp(MdoExtensionText(Current,"alias",64),"qa-command-name");xrtValueRelease(Current);
  char OtherCase[97];snprintf(OtherCase,sizeof(OtherCase),"%s",Id);for(size_t I=0;OtherCase[I];I++)if(OtherCase[I]>='A'&&OtherCase[I]<='Z')OtherCase[I]+=32;
  Current=MdoExtensionsRead(Kind,OtherCase,Error);Ok=Ok&&!Current;xrtValueRelease(Current);
  char* Leftover=xrtFormat("%s/%s/leftover.txt",Kind,Id);Ok=Ok&&MdoHomeAtomicWrite(Leftover,"old",3,false)&&MdoExtensionsSetEnabled(Kind,Id,false,Error);
  Installed=NULL;if(Ok)Ok=MdoExtensionsInstall(Kind,Id,Archive,NULL,true,NULL,&Installed,Error);xrtValueRelease(Installed);
  bool Present=true;xfileinfo Info;Ok=Ok&&MdoHomeExternalStat(Leftover,&Present,&Info)&&!Present;xrtFree(Leftover);
  Current=Ok?MdoExtensionsRead(Kind,Id,Error):NULL;bool Enabled=true;xrtValueGetBool(xrtValueObjectGet(Current,XRT_STR_LITERAL("enabled")),&Enabled);Ok=Ok&&Current&&!Enabled;xrtValueRelease(Current);
  if(Ok)Ok=MdoExtensionsRemove(Kind,Id,&Removed,Error);xrtValueRelease(Removed);
  char* Dir=MdoExtensionsRoot(Kind,Id);Ok=Ok&&MdoHomeExternalStat(Dir,&Present,&Info)&&!Present;xrtFree(Dir);
  xvalue* Row=xrtValueObject();MdoExtPut(Row,"kind",Kind);MdoExtPut(Row,"error",Error);xrtValueObjectSetNew(Row,XRT_STR_LITERAL("cycle"),xrtValueBool(Ok));xrtValueArrayAppendNew(Cycles,Row);
 }
 xrtValueObjectSetNew(*R,XRT_STR_LITERAL("install_update_remove"),Cycles);
 bool Approval=true;{
  xvalue* Result=NULL;Approval=!MdoExtensionsInstall("mcp","unapproved","data/extension-tasks/fixture/mcp.zip",NULL,false,NULL,&Result,Error);xrtValueRelease(Result);
  bool Present=true;xfileinfo Info;Approval=Approval&&MdoHomeExternalStat("mcp/unapproved",&Present,&Info)&&!Present;
 }xrtValueObjectSetNew(*R,XRT_STR_LITERAL("approval_required"),xrtValueBool(Approval));
 {
  xfile Archive=MdoHomeOpenRead("data/extension-tasks/fixture/mcp.zip");char Hash[65];uint64 Size=0;bool Ok=Archive&&MdoExtFileDigest(Archive,NULL,Hash,&Size);xrtClose(Archive);
  xvalue* Credits=xrtValueObject(),*Installed=NULL,*Removed=NULL;MdoExtPut(Credits,"source","store");MdoExtPut(Credits,"sha256",Hash);MdoExtPut(Credits,"submitter","QA publisher");
  Ok=Ok&&MdoExtensionsInstall("mcp","stored-root","data/extension-tasks/fixture/mcp.zip",Credits,true,NULL,&Installed,Error);xrtValueRelease(Installed);xrtValueRelease(Credits);
  Ok=Ok&&MdoExtensionsVerify("mcp","stored-root",NULL,Error)&&MdoHomeAtomicWrite("mcp/stored-root/include/helper.h","tampered",8,false)&&!MdoExtensionsVerify("mcp","stored-root",NULL,Error)&&!MdoExtensionsSetEnabled("mcp","stored-root",true,Error)&&MdoExtensionsSetEnabled("mcp","stored-root",false,Error);
  Ok=Ok&&MdoExtensionsRemove("mcp","stored-root",&Removed,Error);xrtValueRelease(Removed);
  xrtValueObjectSetNew(*R,XRT_STR_LITERAL("changed_store_files_block_execution"),xrtValueBool(Ok));
 }
 {
  xvalue* Result=NULL;bool Ok=MdoExtensionsInstall("mcp","recovery-root","data/extension-tasks/fixture/mcp.zip",NULL,true,NULL,&Result,Error);xrtValueRelease(Result);
  Ok=Ok&&MdoExtensionsSetEnabled("mcp","recovery-root",false,Error)&&MdoExtEnsure(MDO_EXT_TASKS "/recovery-install/incoming")&&
      MdoHomeAtomicWrite(MDO_EXT_TASKS "/recovery-install/incoming/new-marker","new",3,false)&&QaJournal("recovery-install","recovery-root","prepared","install",true)&&
      MdoHomeRenameNoReplace("mcp/recovery-root",MDO_EXT_TASKS "/recovery-install/prior")&&MdoHomeRenameNoReplace(MDO_EXT_TASKS "/recovery-install/incoming","mcp/recovery-root")&&MdoExtensionsRecover(Error);
  xvalue* M=Ok?MdoExtensionsRead("mcp","recovery-root",Error):NULL;bool Enabled=true;xrtValueGetBool(xrtValueObjectGet(M,XRT_STR_LITERAL("enabled")),&Enabled);Ok=Ok&&M&&!Enabled;xrtValueRelease(M);
  bool Present=true;xfileinfo Info;Ok=Ok&&MdoHomeExternalStat("mcp/recovery-root/new-marker",&Present,&Info)&&!Present;
  xrtValueObjectSetNew(*R,XRT_STR_LITERAL("prepared_install_recovered"),xrtValueBool(Ok));
  bool Removed=Ok&&QaJournal("recovery-remove","recovery-root","prepared","remove",true)&&MdoHomeRenameNoReplace("mcp/recovery-root",MDO_EXT_TASKS "/recovery-remove/prior")&&MdoExtensionsRecover(Error);
  M=Removed?MdoExtensionsRead("mcp","recovery-root",Error):NULL;Removed=Removed&&M;xrtValueRelease(M);
  xrtValueObjectSetNew(*R,XRT_STR_LITERAL("prepared_remove_recovered"),xrtValueBool(Removed));
  bool Committed=Removed&&MdoExtEnsure(MDO_EXT_TASKS "/recovery-committed/prior")&&MdoHomeAtomicWrite(MDO_EXT_TASKS "/recovery-committed/prior/old-marker","old",3,false)&&
      MdoHomeAtomicWrite("mcp/recovery-root/committed-marker","new",3,false)&&QaJournal("recovery-committed","recovery-root","committed","install",true)&&MdoExtensionsRecover(Error);
  Committed=Committed&&MdoHomeExternalStat("mcp/recovery-root/committed-marker",&Present,&Info)&&Present&&MdoHomeExternalStat(MDO_EXT_TASKS "/recovery-committed",&Present,&Info)&&!Present;
  xrtValueObjectSetNew(*R,XRT_STR_LITERAL("committed_cleanup_only"),xrtValueBool(Committed));
  Result=NULL;bool Invalid=Committed&&!MdoExtensionsInstall("mcp","recovery-root","data/extension-tasks/fixture/corrupt-mcp.zip",NULL,true,NULL,&Result,Error);xrtValueRelease(Result);
  Invalid=Invalid&&MdoHomeExternalStat("mcp/recovery-root/committed-marker",&Present,&Info)&&Present;
  xrtValueObjectSetNew(*R,XRT_STR_LITERAL("bad_update_preserves_current"),xrtValueBool(Invalid));
  Result=NULL;bool Clean=MdoExtensionsRemove("mcp","recovery-root",&Result,Error);xrtValueRelease(Result);
  Result=NULL;bool Fresh=Clean&&MdoExtensionsInstall("mcp","recovery-fresh","data/extension-tasks/fixture/mcp.zip",NULL,true,NULL,&Result,Error);xrtValueRelease(Result);
  Fresh=Fresh&&QaJournal("recovery-fresh-task","recovery-fresh","prepared","install",false)&&MdoExtensionsRecover(Error)&&MdoHomeExternalStat("mcp/recovery-fresh",&Present,&Info)&&!Present;
  xrtValueObjectSetNew(*R,XRT_STR_LITERAL("fresh_install_rolled_back"),xrtValueBool(Fresh));
  bool Guard=MdoHomeAtomicWrite("outside-marker","keep",4,false)&&QaJournal("recovery-invalid","../outside-marker","prepared","install",false)&&!MdoExtensionsRecover(Error)&&MdoHomeExternalStat("outside-marker",&Present,&Info)&&Present;
  if(Guard)Guard=MdoExtRemoveTree(MDO_EXT_TASKS "/recovery-invalid");
  xrtValueObjectSetNew(*R,XRT_STR_LITERAL("invalid_journal_preserved_outside"),xrtValueBool(Guard));
 }
 xrtValueObjectSetNew(*R,XRT_STR_LITERAL("recovery_clean"),xrtValueBool(MdoExtensionsRecover(Error)));
 MdoExtensionsUnit();xrtRootClose(QaRoot);QaRoot=NULL;return MDO_RESULT_OK;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', required=True)
    parser.add_argument('--home', type=Path, required=True)
    parser.add_argument('--project', default='default')
    args = parser.parse_args()
    out = ROOT / '.build/extension-directory-native'; out.mkdir(parents=True, exist_ok=True)
    fixture = out / ('home-' + uuid.uuid4().hex[:12]); fixture.mkdir()
    entries = {'agents': 'agent.md', 'subagents': 'agent.c', 'tools': 'tool.c', 'mcp': 'mcp.json', 'skills': 'SKILL.md', 'commands': 'command.c', 'external-tools': 'bin/qa', 'knowledge': 'knowledge.json', 'plugins': 'plugin.json'}
    for kind, entry in entries.items():
        directory = fixture / kind / 'qa-local'; directory.mkdir(parents=True)
        manifest = {'kind': kind, 'name': 'QA directory', 'alias': 'qa-command-name', 'entry': entry, 'platforms': ['any']}
        (directory / 'extension.json').write_text(json.dumps(manifest), encoding='utf-8')
        (directory / entry).parent.mkdir(parents=True, exist_ok=True)
        (directory / entry).write_bytes(b'not executed')
        (directory / '.hidden').write_bytes(b'')
        (directory / 'empty').mkdir()
        (directory / 'include').mkdir()
        (directory / 'include/helper.h').write_bytes(b'initial')
    downloads = fixture / 'data/extension-tasks/fixture'; downloads.mkdir(parents=True)
    for kind in entries:
        directory = fixture / kind / 'qa-local'
        with zipfile.ZipFile(downloads / (kind + '.zip'), 'w', compression=zipfile.ZIP_DEFLATED) as z:
            for file in sorted(directory.rglob('*')):
                z.write(file, file.relative_to(directory).as_posix())
    with zipfile.ZipFile(downloads / 'corrupt-mcp.zip', 'w', compression=zipfile.ZIP_STORED) as z:
        z.writestr('extension.json', (fixture / 'mcp/qa-local/extension.json').read_bytes())
        z.writestr('mcp.json', b'corrupt-me')
    with zipfile.ZipFile(downloads / 'corrupt-mcp.zip') as z:
        at = z.getinfo('mcp.json').header_offset + 30 + len('mcp.json')
    corrupt = bytearray((downloads / 'corrupt-mcp.zip').read_bytes()); corrupt[at] ^= 1
    (downloads / 'corrupt-mcp.zip').write_bytes(corrupt)
    ident = 'directory-qa-' + uuid.uuid4().hex[:12]
    meta = {'id': ident, 'description': 'Extension directory storage regression', 'effects': ['read', 'workspace-write'], 'requires_session': False, 'timeout_ms': 60000}
    source = '/* mdo-command ' + json.dumps(meta) + ' */\n#include "mdo/command.h"\n#include <xsbase.h>\n'
    source += (ROOT / 'app/include/mdo/extensions.h').read_text(encoding='utf-8')
    source += (ROOT / 'app/include/mdo/home.h').read_text(encoding='utf-8')
    source += headers() + HOME_STUBS
    manager = (ROOT / 'app/src/extensions/manager.c').read_text(encoding='utf-8')
    manager = '\n'.join(line for line in manager.splitlines() if not line.startswith('#include '))
    source += manager + TEST
    try:
        result = run(args, ident, source, {'root': str(fixture)}, out / 'native-result.json')
        for row in result['types']:
            assert row['listed'] and row['entry'], row
        for field in ('packed', 'auxiliary_changes_digest', 'disabled', 'store_export_blocked', 'unsafe_paths_blocked'):
            assert result[field], (field, result)
        for row in result['install_update_remove']:
            assert row['cycle'], row
        assert result['approval_required'] and result['recovery_clean'] and result['changed_store_files_block_execution'], result
        for key in ('prepared_install_recovered', 'prepared_remove_recovered', 'committed_cleanup_only', 'bad_update_preserves_current', 'fresh_install_rolled_back', 'invalid_journal_preserved_outside'):
            assert result[key], (key, result)
        archive = fixture / 'data/extension-tasks/export/result.zip'
        with zipfile.ZipFile(archive) as z:
            assert z.testzip() is None
            assert z.read('include/helper.h') == b'initial'
            assert z.read('.hidden') == b''
            assert z.getinfo('empty/').is_dir()
            assert z.read('mcp.json') == b'not executed'
            assert z.namelist() == ['.hidden', 'empty/', 'extension.json', 'include/', 'include/helper.h', 'mcp.json']
        assert not (fixture / 'data/extension-tasks/export/rejected.zip').exists()
        shutil.copyfile(archive, out / 'native-export.zip')
        print(json.dumps({'types': 9, 'directory_pack_roundtrip': True, 'auxiliary_digest': True, 'native_command_bytes': len(source.encode())}))
    finally:
        if fixture.resolve().parent == out.resolve() and fixture.name.startswith('home-'):
            shutil.rmtree(fixture)


if __name__ == '__main__':
    main()
