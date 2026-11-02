"""Current product C: Home-relative persistence and temporary pre-1.0 recovery.

Uses an existing QA host; no model calls, copied runtimes or user Home writes.
"""
import argparse
import json
from pathlib import Path
import uuid
from native_product_runner import product

ROOT = Path(__file__).resolve().parents[1]
CONTRACT = r'''
static void QaPathResult(xvalue* R,cstr K,bool V){xrtValueObjectSetNew(R,xrtStrView(K),xrtValueBool(V));}
static bool QaPathEqual(char* A,cstr B){bool Ok=A&&B&&!strcmp(A,B);xrtFree(A);return Ok;}
static xvalue* QaPathRead(cstr Path){xfile F=MdoHomeOpenRead(Path);char* Text=NULL;size_t N=0;bool Ok=F&&MdoHomeReadFileBounded(F,1048576,&Text,&N);xrtClose(F);xjsonreadconfig C;xrtJsonReadConfigInit(&C);C.MaxInputBytes=1048576;C.MaxValues=50000;C.MaxContainerItems=4096;xvalue* V=Ok?xrtJsonRead(xrtStrViewN(Text,N),&C):NULL;xrtFree(Text);return V;}
static bool QaPathWrite(cstr Path,xvalue* V){char* Text=xrtJsonStringify(V,false,NULL);bool Ok=Text&&MdoHomeAtomicWrite(Path,Text,strlen(Text),false);xrtFree(Text);return Ok;}
xvalue* QaProductContract(const xvalue* P,xvfs V){
 xvalue* R=xrtValueObject();cstr Root=SdkField(P,"root"),App=SdkField(P,"app");
 if(!QaProductHome(Root,App,V)){QaPathResult(R,"home",false);return R;}
 char* File=MdoHomeExternalPath("1.txt"),*Workspace=MdoHomeExternalPath("workspace");
 char* Parent=xrtPathParent(Root);char* Old=xrtPathJoin(Parent,"missing/mdo-home/workspace"),*Other=xrtPathJoin(Root,"original/mdo-home/keep.txt");xrtFree(Parent);
 QaPathResult(R,"relative_paths_are_home_based",QaPathEqual(MdoPathResolve("1.txt"),File));
 QaPathResult(R,"store_paths_are_relative",QaPathEqual(MdoPathStore(File),"1.txt"));
 QaPathResult(R,"home_root_roundtrips",QaPathEqual(MdoPathStore(Root),".")&&QaPathEqual(MdoPathResolve("."),Root));
 QaPathResult(R,"missing_old_home_rebases",QaPathEqual(MdoPathResolve(Old),Workspace));
 QaPathResult(R,"legacy_store_converges_to_relative",QaPathEqual(MdoPathStore(Old),"workspace"));
 QaPathResult(R,"existing_absolute_path_wins",QaPathEqual(MdoPathResolve(Other),Other));
 char* Similar=xrtPathJoin(Root,"missing/mdo-home-backup/workspace");char* Expected=xrtPathAbs(Similar);
 QaPathResult(R,"similar_directory_is_not_rebased",QaPathEqual(MdoPathResolve(Similar),Expected));xrtFree(Expected);xrtFree(Similar);
 char* Bad=xrtFormat("%s/missing/mdo-home/../escape.txt",Root);char* Invalid=MdoPathResolve(Bad);
 QaPathResult(R,"legacy_traversal_is_rejected",!Invalid);xrtFree(Invalid);xrtFree(Bad);xrtClearError();
 Invalid=MdoPathResolve("../escape.txt");QaPathResult(R,"relative_traversal_is_rejected",!Invalid);xrtFree(Invalid);xrtClearError();
 cstr External=SdkField(P,"external");QaPathResult(R,"external_project_binding_is_preserved",QaPathEqual(MdoPathStore(External),External));
 MdoSessionInfo S={0};S.Size=sizeof(S);S.Revision=7;S.CreatedAt=S.UpdatedAt=S.LastActivatedAt=MdoUtcUs();
 S.Status=S.PreviousStatus=MDO_SESSION_ACTIVE;S.Protocol=MDO_MODEL_PROTOCOL_OPENAI_RESPONSES;S.MaxOutputTokens=1024;
 strcpy(S.Id,"0123456789abcdef0123456789abcdef");strcpy(S.ProjectId,"default");strcpy(S.AgentId,"mdo.default");strcpy(S.ModelId,"fixture");strcpy(S.PermissionProfile,"full-access");
 snprintf(S.WorkspaceRoot,sizeof(S.WorkspaceRoot),"%s",Workspace);
 size_t N=0;char* Text=MdoSessionsInternalMetaJson(&S,&N);xvalue* Meta=Text?xrtJsonParse(xrtStrViewN(Text,N)):NULL;xrtFree(Text);
 QaPathResult(R,"session_serializer_uses_relative_workspace",Meta&&SdkField(Meta,"workspace_root")&&!strcmp(SdkField(Meta,"workspace_root"),"workspace"));
 MdoSessionInfo Restored={0};Text=Meta?xrtJsonStringify(Meta,false,NULL):NULL;
 QaPathResult(R,"relative_session_recovers_native_workspace",Text&&MdoSessionsInternalMetaParse(S.ProjectId,S.Id,xrtStrView(Text),&Restored)&&!strcmp(Restored.WorkspaceRoot,Workspace)&&Restored.Revision==7);xrtFree(Text);
 SdkPut(Meta,"workspace_root",Old);
 bool Written=QaPathWrite("sessions/default/0123456789abcdef0123456789abcdef/meta.json",Meta)&&QaPathWrite("sessions/default/0123456789abcdef0123456789abcdef/meta.json.bak",Meta);
 cstr Ledger="{\"user_text\":\"preserve original history\"}\n";
 Written=Written&&MdoHomeAtomicWrite("sessions/default/0123456789abcdef0123456789abcdef/snapshot.json",Ledger,strlen(Ledger),false);
 xvalue* Project=xrtValueObject();SdkPut(Project,"workspace_root",Old);SdkPutUInt(Project,"revision",9);Written=Written&&QaPathWrite("projects/owned.json",Project);
 SdkPut(Project,"workspace_root",External);Written=Written&&QaPathWrite("projects/external.json",Project);xrtValueRelease(Project);
 bool Migrated=Written&&MdoPathMigrate();xvalue* Read=QaPathRead("sessions/default/0123456789abcdef0123456789abcdef/meta.json");
 QaPathResult(R,"startup_migrates_existing_metadata",Migrated&&Read&&!strcmp(SdkField(Read,"workspace_root"),"workspace"));
 uint64 Rev=0;SdkUInt(SdkGet(Read,"revision"),&Rev);QaPathResult(R,"migration_preserves_metadata_revision",Rev==7);xrtValueRelease(Read);
 Read=QaPathRead("sessions/default/0123456789abcdef0123456789abcdef/meta.json.bak");QaPathResult(R,"startup_migrates_backups",Read&&!strcmp(SdkField(Read,"workspace_root"),"workspace"));xrtValueRelease(Read);
 Read=QaPathRead("projects/external.json");QaPathResult(R,"migration_keeps_external_projects",Read&&!strcmp(SdkField(Read,"workspace_root"),External));xrtValueRelease(Read);
 QaPathResult(R,"startup_is_idempotent",MdoPathMigrate());
 xfile F=MdoHomeOpenRead("sessions/default/0123456789abcdef0123456789abcdef/snapshot.json");Text=NULL;N=0;bool Unchanged=F&&MdoHomeReadFileBounded(F,1024,&Text,&N)&&N==strlen(Ledger)&&!memcmp(Text,Ledger,N);xrtClose(F);xrtFree(Text);
 QaPathResult(R,"migration_does_not_rewrite_model_history",Unchanged);
 xrtValueRelease(Meta);xrtFree(File);xrtFree(Workspace);xrtFree(Old);xrtFree(Other);MdoHomeUnit();return R;
}
'''


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--url', required=True)
    p.add_argument('--home', type=Path, required=True)
    p.add_argument('--project', default='default')
    args = p.parse_args()
    base = ROOT / '.build/portable-paths-native' / uuid.uuid4().hex[:10]
    fixture = base / 'mdo-home'
    (fixture / 'workspace').mkdir(parents=True)
    (fixture / '1.txt').write_text('Home file', encoding='utf-8')
    original = fixture / 'original/mdo-home'
    original.mkdir(parents=True)
    (original / 'keep.txt').write_text('Existing original wins', encoding='utf-8')
    external = base / 'external-project'
    external.mkdir()
    # Inject only the owned external project fixture, never a real project.
    contract = CONTRACT.replace('cstr External=SdkField(P,"external");',
                                'cstr External=' + json.dumps(str(external)) + ';')
    result = product(args, fixture, contract, base / 'result.json')
    assert result and all(v is True for v in result.values()), result
    print('PASS', len(result), 'native portable path contracts; evidence:', base / 'result.json')


if __name__ == '__main__':
    main()
