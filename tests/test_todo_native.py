"""Bundled todo uses the public SDK and preserves the native plan contracts."""
import argparse
import json
from pathlib import Path
import shutil
import uuid
from native_product_runner import product

ROOT = Path(__file__).resolve().parents[1]
CONTRACT = r'''
#define mdoModuleEntry QaTodoEntry
#include "/app/default-home/tools/todo/tool.c"
#undef mdoModuleEntry
typedef struct QaTodoOutput { char Text[13000];size_t Size;bool Success,Set; } QaTodoOutput;
static bool QaTodoWrite(void* P,cstr Text,size_t N){QaTodoOutput* O=P;if(N>=sizeof(O->Text)-O->Size)return false;memcpy(O->Text+O->Size,Text,N);O->Size+=N;O->Text[O->Size]=0;return true;}
static bool QaTodoSuccess(void* P,bool Success){QaTodoOutput* O=P;O->Success=Success;O->Set=true;return true;}
static bool QaTodoCall(cstr Json,bool Expected){
 QaTodoOutput Out={0};mdo_sdk_context_v1 App={0};App.Size=sizeof(App);App.Version=MDO_SDK_VERSION;App.Api=MdoApplicationApi();
 mdo_tool_context_v1 C={MDO_V1_HEADER(mdo_tool_context_v1)};C.Application=&App;
 mdo_result_writer_v1 W={MDO_V1_HEADER(mdo_result_writer_v1)};W.Context=&Out;W.Write=QaTodoWrite;W.SetSuccess=QaTodoSuccess;
 char Error[512]={0};mdo_result Code=TODO_TOOL.Execute(NULL,&C,Json,&W,Error,sizeof(Error));
 xvalue* Parsed=MdoSessionsInternalTodoParse(xrtStrViewN(Out.Text,Out.Size),false);
 bool Ok=Code==MDO_RESULT_OK&&Out.Set&&Out.Success==Expected&&(Expected?Parsed!=NULL:strstr(Out.Text,"Invalid todo arguments")!=NULL);
 xrtValueRelease(Parsed);return Ok;
}
xvalue* QaProductContract(const xvalue* P,xvfs V){
 (void)P;(void)V;xvalue* R=xrtValueObject();
 #define CHECK(K,X) xrtValueObjectSetNew(R,xrtStrView(K),xrtValueBool(X))
 CHECK("canonical_module",!strcmp(QaTodoEntry()->Id,"todo")&&!strcmp(TODO_TOOL.Id,"todo")&&!strcmp(TODO_TOOL.SerialGroup,"todo"));
 CHECK("valid_plan",QaTodoCall("{\"items\":[{\"text\":\"Inspect code\",\"done\":false},{\"text\":\"Verify\",\"done\":true}]}",true));
 CHECK("empty_plan_clears",QaTodoCall("{\"items\":[]}",true));
 cstr Invalid[]={NULL,"","null","[]","{","{}","{\"items\":false}","{\"items\":[],\"extra\":true}","{\"items\":[{\"text\":\"x\"}]}","{\"items\":[{\"text\":\"\",\"done\":false}]}","{\"items\":[{\"text\":\"x\",\"done\":0}]}","{\"items\":[{\"text\":\"x\",\"done\":false,\"extra\":0}]}","{\"items\":[{\"text\":\"a\\u0000b\",\"done\":false}]}"};
 bool Reject=true;for(size_t I=0;I<sizeof(Invalid)/sizeof(*Invalid);I++)Reject=QaTodoCall(Invalid[I],false)&&Reject;
 CHECK("invalid_arguments_are_correctable",Reject);
 char Text[965];for(size_t I=0;I<241;I++)memcpy(Text+I*4,"\xf0\x9f\x94\xa7",4);Text[960]=0;
 xvalue* Root=xrtValueObject(),*Items=xrtValueArray(),*Item=xrtValueObject();
 xrtValueObjectSetNew(Item,XRT_STR_LITERAL("text"),xrtValueString(xrtStrView(Text)));xrtValueObjectSetNew(Item,XRT_STR_LITERAL("done"),xrtValueBool(false));
 xrtValueArrayAppend(Items,Item);xrtValueObjectSet(Root,XRT_STR_LITERAL("items"),Items);char* Json=xrtJsonStringify(Root,false,NULL);
 CHECK("unicode_240_characters",QaTodoCall(Json,true));xrtFree(Json);Text[960]='x';Text[961]=0;
 xrtValueObjectSetNew(Item,XRT_STR_LITERAL("text"),xrtValueString(xrtStrView(Text)));Json=xrtJsonStringify(Root,false,NULL);
 CHECK("unicode_limit_is_characters",QaTodoCall(Json,false));xrtFree(Json);
 xrtValueObjectSetNew(Item,XRT_STR_LITERAL("text"),xrtValueString(XRT_STR_LITERAL("Task")));
 for(size_t I=1;I<24;I++)xrtValueArrayAppend(Items,Item);Json=xrtJsonStringify(Root,false,NULL);
 CHECK("maximum_24_items",QaTodoCall(Json,true));xrtFree(Json);xrtValueArrayAppend(Items,Item);Json=xrtJsonStringify(Root,false,NULL);
 CHECK("reject_25_items",QaTodoCall(Json,false));xrtFree(Json);xrtValueRelease(Item);xrtValueRelease(Items);xrtValueRelease(Root);
 char Oversized[12290];memset(Oversized,' ',sizeof(Oversized)-1);Oversized[sizeof(Oversized)-1]=0;
 CHECK("oversized_input_is_correctable",QaTodoCall(Oversized,false));
 CHECK("reject_invalid_utf8",!TodoTextValid("\xc0\x80")&&!TodoTextValid("\xed\xa0\x80")&&!TodoTextValid("\xf4\x90\x80\x80")&&!TodoTextValid("\xe4\xb8"));
 bool History=true;for(size_t I=0;I<2;I++){
  cstr Name=I?"mdo.todo":"todo";MdoSessionEventInfo E={0};MdoBackupEventFact F={0};E.Kind=XWORK_EVENT_TOOL_DONE;E.Success=true;E.ToolName=Name;
  History=History&&MdoBackupEventCopy(&E,&F)&&F.Todo&&MdoSessionsIsTodoTool(Name);E.AgentDepth=1;MdoBackupEventCopy(&E,&F);History=History&&!F.Todo;E.AgentDepth=0;E.Success=false;MdoBackupEventCopy(&E,&F);History=History&&!F.Todo;
 }
 CHECK("backup_history_accepts_both_names_main_success_only",History);
 CHECK("legacy_allowlist_uses_canonical_name",!strcmp(MdoBuiltinToolCanonicalId("mdo.todo"),"todo")&&!strcmp(MdoBuiltinToolCanonicalId("todo"),"todo"));
 #undef CHECK
 return R;
}
'''

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--url', required=True)
    p.add_argument('--home', type=Path, required=True)
    p.add_argument('--project', default='default')
    args = p.parse_args()
    out = ROOT / '.build/todo-native'
    out.mkdir(exist_ok=True)
    fixture = out / ('home-' + uuid.uuid4().hex[:10])
    fixture.mkdir()
    try:
        result = product(args, fixture, CONTRACT, out / 'result.json',
            compiler_setup='tcc_add_include_path(T,"/app/generated/module-sdk");')
        assert result and all(v is True for v in result.values()), result
        print('PASS', len(result), 'native todo contracts')
    finally:
        shutil.rmtree(fixture)

if __name__ == '__main__':
    main()
