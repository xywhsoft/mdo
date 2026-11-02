"""Native title state, anonymous request and metadata race contracts."""
import argparse
from pathlib import Path
import shutil
import uuid
from native_product_runner import product

ROOT=Path(__file__).resolve().parents[1]
CONTRACT=r'''
static void SdkPutBool(xvalue* R,cstr K,bool V){xrtValueObjectSetNew(R,xrtStrView(K),xrtValueBool(V));}
static MdoSession* QaNamingSession;
static int QaNamingMode,QaNamingCalls;
static bool QaAnonymous,QaBounded;
bool QaTitleFetch(const XS_FetchRequest* Q,XS_FetchResponse* R){
 QaNamingCalls++;QaAnonymous=Q->HeaderCount==2;for(size_t I=0;I<Q->HeaderCount;I++)if(!strcmp(Q->Headers[I].Name,"Authorization"))QaAnonymous=false;
 QaBounded=Q->Timeout==8000000&&Q->MaxBodyBytes==4096&&Q->BodySize<=16384;
 if(QaNamingMode==2){xwork_error E;MdoSessionRename(QaNamingSession,"Manual title",&E);}
 if(QaNamingMode==3){xrtCancelRequest(MdoTitle.Cancel);return false;}
 const char* Json=QaNamingMode==1?"{\"code\":0,\"data\":{\"title\":\"Generated title\"}}":"{\"code\":0,\"data\":{\"title\":\"late result\"}}";
 if(QaNamingMode==4){R->Status=503;return true;}
 R->Status=200;R->Body=(unsigned char*)xrtStrDup(Json);R->BodySize=strlen(Json);return true;
}
static void QaSeedTitle(MdoSession* S,uint32 State){
 S->Info.TitleState=State;snprintf(S->Info.Title,sizeof(S->Info.Title),"First message");S->Info.Revision++;
 MdoSessionsMetaWrite(S->MetaPath,&S->Info);char Path[MDO_SESSION_PATH_CAPACITY];MdoTitlePath(Path,&S->Info);
 xvalue* J=xrtValueObject();MdoSessionsObjectString(J,"request_id","aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
 MdoSessionsObjectString(J,"content","First message");MdoSessionsObjectTake(J,"created_at_us",xrtValueUInt(MdoUtcUs()));
 size_t N=0;char* Json=xrtJsonStringify(J,false,&N);MdoHomeAtomicWrite(Path,Json,N,true);xrtFree(Json);xrtValueRelease(J);
}
xvalue* QaProductContract(const xvalue* P,xvfs V){
 xvalue* R=xrtValueObject();bool Home=QaProductHome(SdkField(P,"root"),SdkField(P,"app"),V);
 g_MdoSessions.Lock=xrtMutexCreate();g_MdoSessions.Initialized=true;MdoSessionDataInit();
 MdoSession S={0};S.Lock=xrtMutexCreate();S.Info.Size=sizeof(S.Info);S.Info.Revision=1;S.Info.CreatedAt=MdoUtcUs()-10000000;
 S.Info.UpdatedAt=S.Info.LastActivatedAt=S.Info.CreatedAt;S.Info.Status=S.Info.PreviousStatus=MDO_SESSION_ACTIVE;
 S.Info.Protocol=MDO_MODEL_PROTOCOL_OPENAI_RESPONSES;S.Info.MaxOutputTokens=1024;
 snprintf(S.Info.Id,sizeof(S.Info.Id),"0123456789abcdef0123456789abcdef");snprintf(S.Info.ProjectId,sizeof(S.Info.ProjectId),"default");
 snprintf(S.Info.AgentId,sizeof(S.Info.AgentId),"mdo.default");snprintf(S.Info.ModelId,sizeof(S.Info.ModelId),"fixture");snprintf(S.Info.WorkspaceRoot,sizeof(S.Info.WorkspaceRoot),".");
 snprintf(S.MetaPath,sizeof(S.MetaPath),"sessions/default/%s/meta.json",S.Info.Id);QaNamingSession=&S;MdoTitle.Cancel=xrtCancelCreate();
 QaSeedTitle(&S,2);S.Info.RuntimeOpen=true;int64 Activated=S.Info.LastActivatedAt;QaNamingMode=1;MdoTitleProcess(&S);
 SdkPutBool(R,"anonymous_bounded_request",Home&&QaAnonymous&&QaBounded);
 SdkPutBool(R,"model_title_and_activity",S.Info.TitleState==3&&!strcmp(S.Info.Title,"Generated title")&&S.Info.LastActivatedAt==Activated&&S.Info.RuntimeOpen);
 QaSeedTitle(&S,2);QaNamingMode=2;MdoTitleProcess(&S);
 SdkPutBool(R,"manual_rename_wins",S.Info.TitleState==0&&!strcmp(S.Info.Title,"Manual title"));
 QaSeedTitle(&S,2);QaNamingMode=4;MdoTitleProcess(&S);
 SdkPutBool(R,"http_failure_keeps_fallback",S.Info.TitleState==4&&!strcmp(S.Info.Title,"First message"));
 int Calls=QaNamingCalls;MdoTitleProcess(&S);SdkPutBool(R,"terminal_fallback_not_retried",QaNamingCalls==Calls);
 QaSeedTitle(&S,2);QaNamingMode=3;MdoTitleProcess(&S);
 SdkPutBool(R,"shutdown_retains_pending",S.Info.TitleState==2);
 xrtCancelDestroy(MdoTitle.Cancel);MdoTitle.Cancel=xrtCancelCreate();QaNamingMode=1;MdoTitleProcess(&S);
 SdkPutBool(R,"pending_can_resume",S.Info.TitleState==3);
 QaSeedTitle(&S,1);MdoSessionTitleStart(&S,"Visible request\n\n<!--mdo-context:v1-->\nprivate quote\n<!--/mdo-context-->",NULL);
 char Path[MDO_SESSION_PATH_CAPACITY],*Raw=NULL;size_t N=0;MdoTitlePath(Path,&S.Info);MdoSessionsReadBounded(Path,16384,&Raw,&N);
 SdkPutBool(R,"context_not_uploaded",Raw&&strstr(Raw,"Visible request")&&!strstr(Raw,"private quote"));xrtFree(Raw);
 MdoSessionInfo Current=S.Info,Stale=S.Info;Current.Revision++;Current.TitleState=3;snprintf(Current.Title,sizeof(Current.Title),"New auto title");MdoSessionsMetaWrite(S.MetaPath,&Current);
 xwork_error E;bool Rebased=MdoSessionsValidateCurrent(&S,&E);SdkPutBool(R,"live_handle_rebases_title_only",Rebased&&!strcmp(S.Info.Title,"New auto title"));
 Current=S.Info;Current.Revision++;snprintf(Current.ModelId,sizeof(Current.ModelId),"different");MdoSessionsMetaWrite(S.MetaPath,&Current);
 SdkPutBool(R,"other_profile_conflict_preserved",!MdoSessionsValidateCurrent(&S,&E));
 char Cut[9];MdoTitleClip(Cut,sizeof(Cut),"你好世界",80);SdkPutBool(R,"utf8_fallback_bounded",xrtUtf8Valid(xrtStrView(Cut),NULL)&&strlen(Cut)<=8);
 xrtCancelDestroy(MdoTitle.Cancel);memset(&MdoTitle,0,sizeof(MdoTitle));xrtMutexDestroy(S.Lock);xrtMutexDestroy(g_MdoSessions.Lock);g_MdoSessions.Lock=NULL;g_MdoSessions.Initialized=false;MdoSessionDataUnit();if(Home)MdoHomeUnit();return R;
}
'''

def main():
    p=argparse.ArgumentParser();p.add_argument('--url',required=True);p.add_argument('--home',type=Path,required=True);p.add_argument('--project',default='default');a=p.parse_args()
    out=ROOT/'.build/session-title-native';out.mkdir(exist_ok=True)
    fixture=out/('home-'+uuid.uuid4().hex[:10]);fixture.mkdir()
    try:
        result=product(a,fixture,CONTRACT,out/'result.json',compiler_setup='tcc_define_symbol(T,"xsFetch","QaTitleFetch");')
        assert result and all(v is True for v in result.values()),result
        print('PASS',len(result),'native title contracts; zero vendor calls')
    finally:shutil.rmtree(fixture)

if __name__=='__main__':main()
