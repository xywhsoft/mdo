"""Compile the actual device-list cache repair in an existing mdo runtime.

No service, application copy, account operation or model call is created.
"""
import argparse
import json
from pathlib import Path
import uuid
from native_extension_runner import run
from native_product_runner import product

ROOT=Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url',required=True);parser.add_argument('--home',type=Path,required=True)
    parser.add_argument('--project',default='default');args=parser.parse_args()
    identity='device-list-cache-qa-'+uuid.uuid4().hex[:12]
    header='/* mdo-command '+json.dumps(dict(id=identity,description='Device cache regression',effects=['read'],timeout_ms=10000))+' */\n'
    source=header+'#include "mdo/command.h"\n#include <xsbase.h>\n#include <string.h>\n'
    source+=(ROOT/'app/src/remote/device_listing.inc.c').read_text(encoding='utf-8')
    source+=r'''
static void Check(xvalue* Out,cstr Key,bool Value){xrtValueObjectSetNew(Out,xrtStrView(Key),xrtValueBool(Value));}
static xvalue* Listing(void){
 xvalue* L=xrtValueObject(),*A=xrtValueArray();
 const cstr Ids[]={"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"};
 for(size_t I=0;I<2;I++){xvalue* D=xrtValueObject();xrtValueObjectSetNew(D,XRT_STR_LITERAL("id"),xrtValueString(xrtStrView(Ids[I])));xrtValueObjectSetNew(D,XRT_STR_LITERAL("revoked"),xrtValueBool(false));xrtValueObjectSetNew(D,XRT_STR_LITERAL("allow_remote"),xrtValueBool(true));xrtValueArrayAppendNew(A,D);}
 xrtValueObjectSetNew(L,XRT_STR_LITERAL("devices"),A);return L;
}
static bool Flag(const xvalue* V,cstr K,bool Expected){bool B=false;return xrtValueGetBool(xrtValueObjectGet(V,xrtStrView(K)),&B)&&B==Expected;}
MDO_EXPORT mdo_result mdoCommandRun(const mdo_command_context_v1* C,const xvalue* A,xvalue** R){
 (void)C;(void)A;*R=xrtValueObject();xvalue* L=Listing();
 xvalue* Rev=MdoRemoteListingAfterMutation(L,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",false);
 const xvalue* Rows=xrtValueObjectGet(Rev,XRT_STR_LITERAL("devices"));
 Check(*R,"revoke_changes_only_matching_device",xrtValueCount(Rows)==2&&Flag(xrtValueArrayGet(Rows,0),"revoked",true)&&Flag(xrtValueArrayGet(Rows,0),"allow_remote",false)&&Flag(xrtValueArrayGet(Rows,1),"revoked",false));
 Check(*R,"original_cache_snapshot_unchanged",Flag(xrtValueArrayGet(xrtValueObjectGet(L,XRT_STR_LITERAL("devices")),0),"revoked",false));
 xvalue* Removed=MdoRemoteListingAfterMutation(Rev,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",true);Rows=xrtValueObjectGet(Removed,XRT_STR_LITERAL("devices"));
 xstrview Id={0};Check(*R,"remove_hides_device_immediately",xrtValueCount(Rows)==1&&xrtValueGetString(xrtValueObjectGet(xrtValueArrayGet(Rows,0),XRT_STR_LITERAL("id")),&Id)&&Id.Size==32&&Id.Data[0]=='b');
 xvalue* Missing=MdoRemoteListingAfterMutation(L,"cccccccccccccccccccccccccccccccc",true);
 Check(*R,"missing_device_preserves_other_rows",xrtValueCount(xrtValueObjectGet(Missing,XRT_STR_LITERAL("devices")))==2);
 xvalue* Empty=xrtValueObject();xvalue* Invalid=MdoRemoteListingAfterMutation(Empty,"a",false);
 Check(*R,"missing_inventory_invalidates_cache",Invalid==NULL);
 xrtValueRelease(Invalid);xrtValueRelease(Empty);xrtValueRelease(Missing);xrtValueRelease(Removed);xrtValueRelease(Rev);xrtValueRelease(L);return MDO_RESULT_OK;
}
'''
    output=ROOT/'.build/device-picker-20261112';output.mkdir(parents=True,exist_ok=True)
    result=run(args,identity,source,{},output/'cache-native.json')
    assert len(result)==5 and all(result.values()),result
    fixture=output/'compile-contract';fixture.mkdir(exist_ok=True)
    compiled=product(args,fixture,r'''
xvalue* QaProductContract(const xvalue* P,xvfs V){
 (void)P;(void)V;xvalue* R=xrtValueObject();
 xrtValueObjectSetNew(R,XRT_STR_LITERAL("remote_action_metadata"),xrtValueBool(
  !strcmp(MdoRemoteActionName(MDO_REMOTE_REVOKE),"revoke")&&
  !strcmp(MdoRemoteActionName(MDO_REMOTE_REMOVE),"remove")&&
  !strcmp(MdoRemoteActionName(MDO_REMOTE_LIST),"refresh")));
 return R;
}
''',output/'product-compile.json')
    assert compiled.get('remote_action_metadata'),compiled
    result.update(compiled)
    print(json.dumps(result))

if __name__=='__main__':main()
