"""Navigation timestamps survive restart and concurrent metadata writes."""
import argparse
import json
from pathlib import Path
import shutil
import uuid
from native_product_runner import product

ROOT = Path(__file__).resolve().parents[1]
CONTRACT = r'''
xvalue* QaProductContract(const xvalue* P,xvfs V){
 xvalue* R=xrtValueObject();cstr Root=SdkField(P,"root"),App=SdkField(P,"app");
 bool Home=QaProductHome(Root,App,V);g_MdoSessions.Lock=xrtMutexCreate();
 MdoSession S={0};S.Lock=xrtMutexCreate();S.Info.Size=sizeof(S.Info);S.Info.Revision=3;
 S.Info.CreatedAt=MdoUtcUs()-10000000;S.Info.UpdatedAt=S.Info.CreatedAt+1;S.Info.LastActivatedAt=S.Info.CreatedAt;
 S.Info.Status=S.Info.PreviousStatus=MDO_SESSION_ACTIVE;S.Info.Protocol=MDO_MODEL_PROTOCOL_OPENAI_RESPONSES;S.Info.MaxOutputTokens=1024;
 snprintf(S.Info.Id,sizeof(S.Info.Id),"0123456789abcdef0123456789abcdef");snprintf(S.Info.ProjectId,sizeof(S.Info.ProjectId),"default");
 snprintf(S.Info.AgentId,sizeof(S.Info.AgentId),"mdo.default");snprintf(S.Info.ModelId,sizeof(S.Info.ModelId),"fixture");snprintf(S.Info.WorkspaceRoot,sizeof(S.Info.WorkspaceRoot),".");
 snprintf(S.MetaPath,sizeof(S.MetaPath),"sessions/default/%s/meta.json",S.Info.Id);
 xwork_error E;MdoSessionInfo Stale=S.Info,Read={0};
 bool Saved=Home&&g_MdoSessions.Lock&&S.Lock&&MdoSessionsMetaWrite(S.MetaPath,&S.Info);
 bool Activated=Saved&&MdoSessionActivate(&S,&E)&&S.Info.LastActivatedAt>Stale.LastActivatedAt&&S.Info.Revision==Stale.Revision&&S.Info.UpdatedAt==Stale.UpdatedAt;
 xrtValueObjectSetNew(R,XRT_STR_LITERAL("navigation_changes_only_activity"),xrtValueBool(Activated));
 bool Persisted=Activated&&MdoSessionsMetaRead(S.Info.ProjectId,S.Info.Id,&Read)&&Read.LastActivatedAt==S.Info.LastActivatedAt;
 xrtValueObjectSetNew(R,XRT_STR_LITERAL("activity_recovers_from_disk"),xrtValueBool(Persisted));
 Stale.Revision++;Stale.UpdatedAt=MdoUtcUs();
 bool Merged=Persisted&&MdoSessionsCommit(&S,&Stale,&E)&&MdoSessionsMetaRead(S.Info.ProjectId,S.Info.Id,&Stale)&&Stale.LastActivatedAt==Read.LastActivatedAt;
 xrtValueObjectSetNew(R,XRT_STR_LITERAL("stale_metadata_edit_preserves_navigation"),xrtValueBool(Merged));
 MdoSessionInfo Other=Read;Other.LastActivatedAt=Read.LastActivatedAt-1;Other.UpdatedAt=INT64_MAX;
 xrtValueObjectSetNew(R,XRT_STR_LITERAL("catalog_prefers_activation_over_edits"),xrtValueBool(MdoSessionsCatalogCompare(&Read,&Other)<0));
 size_t Bytes=0;char* Json=MdoSessionsInternalMetaJson(&Read,&Bytes);xvalue* Meta=Json?xrtJsonParse(xrtStrView(Json)):NULL;xrtFree(Json);
 xvalue* LegacyMeta=xrtValueObject();for(size_t I=0;I<xrtValueCount(Meta);I++){xstrview Key;const xvalue* Value=xrtValueObjectAt(Meta,I,&Key);if(!xrtStrEqual(Key,XRT_STR_LITERAL("last_activated_at_us")))xrtValueObjectSet(LegacyMeta,Key,(xvalue*)Value);}xrtValueRelease(Meta);Meta=LegacyMeta;Json=Meta?xrtJsonStringify(Meta,false,NULL):NULL;
 bool Legacy=Json&&MdoSessionsInternalMetaParse(Read.ProjectId,Read.Id,xrtStrView(Json),&Other)&&Other.LastActivatedAt==Read.UpdatedAt;xrtFree(Json);
 xrtValueObjectSetNew(R,XRT_STR_LITERAL("legacy_metadata_remains_readable"),xrtValueBool(Legacy));
 xrtValueObjectSetNew(Meta,XRT_STR_LITERAL("last_activated_at_us"),xrtValueInt(-1));Json=xrtJsonStringify(Meta,false,NULL);
 bool Invalid=Json&&!MdoSessionsInternalMetaParse(Read.ProjectId,Read.Id,xrtStrView(Json),&Other);xrtFree(Json);xrtValueRelease(Meta);
 xrtValueObjectSetNew(R,XRT_STR_LITERAL("invalid_activity_is_rejected"),xrtValueBool(Invalid));
 xrtMutexDestroy(S.Lock);xrtMutexDestroy(g_MdoSessions.Lock);g_MdoSessions.Lock=NULL;if(Home)MdoHomeUnit();return R;
}
'''

def main():
    p=argparse.ArgumentParser();p.add_argument('--url',required=True);p.add_argument('--home',type=Path,required=True);p.add_argument('--project',default='default');a=p.parse_args()
    out=ROOT/'.build/session-activation-native';out.mkdir(exist_ok=True)
    fixture=out/('home-'+uuid.uuid4().hex[:10]);fixture.mkdir()
    result=product(a,fixture,CONTRACT,out/'result.json')
    assert result and all(v is True for v in result.values()),result
    shutil.rmtree(fixture)
    print('PASS',len(result),'native session activation contracts')

if __name__=='__main__':main()
