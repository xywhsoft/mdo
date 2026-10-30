from locked_sdk import sdk_root
"""Compile current platform startup and check registered authority/menus/pages."""
import argparse
import json
from pathlib import Path
import shutil
import uuid
from native_product_runner import product
from extension_zip_fixture import platform_store_source
from test_platform_directory_routes_native import PRELUDE, PLATFORM, ECO

ROOT=Path(__file__).resolve().parents[1]
STUBS=r'''
static xvalue* QaRoutes;static unsigned QaMenus;static bool QaMenuOk=true;static char QaPage[128];
int MdoExtensionsMenuParent(void){return 90;}
int XAdmin_RegisterRoute(XAdminPluginHandle H,const XAdminRouteDecl* D,XAdminRouteToken* T){(void)H;(void)T;xvalue* V=xrtValueObject();SdkPutUInt(V,"auth",D->auth_id);xrtValueObjectSetNew(V,XRT_STR_LITERAL("login"),xrtValueBool(D->need_auth));xrtValueObjectSetNew(V,XRT_STR_LITERAL("admin"),xrtValueBool(D->admin_only));bool Ok=xrtValueObjectSet(QaRoutes,xrtStrView(D->path),V);xrtValueRelease(V);return Ok?0:-1;}
int XAdmin_RegisterMenu(XAdminPluginHandle H,const XAdminMenuDecl* D,int* Id,XAdminMenuToken* T){(void)H;(void)Id;(void)T;QaMenus++;QaMenuOk=QaMenuOk&&D->visible&&D->parent_id==90&&!strcmp(D->open_type,"_component");return 0;}
int XAdmin_RegisterAuthGroup(XAdminPluginHandle H,const XAdminAuthGroupDecl* D,int* Id,XAdminAuthGroupToken* T){(void)H;(void)T;*Id=4;return strcmp(D->key,"mdo.ecosystem");}
int XAdmin_RegisterAuth(XAdminPluginHandle H,const XAdminAuthDecl* D,int* Id,XAdminAuthToken* T){(void)H;(void)T;*Id=7;return strcmp(D->key,"mdo.ecosystem.review");}
const char* XAdmin_PluginReqHeader(XS_RequestObject R,const char* Name){(void)Name;return R->Csrf;}
int XAdmin_LoadPluginPage(XAdminPluginHandle H,XS_ResponseObject R,int Code,const char* Headers,const char* Page){(void)H;(void)Headers;R->Status=Code;snprintf(QaPage,sizeof(QaPage),"%s",Page);return 0;}
'''
CONTRACT=r'''
xvalue* QaProductContract(const xvalue* P,xvfs V){
 (void)V;QaRoutes=xrtValueObject();xvalue* R=xrtValueObject();XAdminHostContext H={0};H.plugin_data_path=SdkField(P,"root");sqlite3_initialize();sqlite3_open(":memory:",&H.main_db);sqlite3_exec(H.main_db,"CREATE TABLE menu(plugin_xid TEXT,href TEXT,isDelete INTEGER);INSERT INTO menu VALUES('mdo','/admin/mdo/cli',0);INSERT INTO menu VALUES('other','/admin/mdo/cli',0)",NULL,NULL,NULL);G_Plugins[0].started=true;
 bool Ok=MdoEcologyStart(&G_Plugins[0],&H)==0;SdkPutUInt(R,"routes",xrtValueCount(QaRoutes));xrtValueObjectSetNew(R,XRT_STR_LITERAL("startup_and_ten_component_menus"),xrtValueBool(Ok&&QaMenus==10&&QaMenuOk));
 const xvalue* Public=SdkGet(QaRoutes,"/api/v1/mdo/extensions/catalog"),*Upload=SdkGet(QaRoutes,"/api/v1/mdo/extensions/uploads"),*Admin=SdkGet(QaRoutes,"/admin/api/mdo/extensions");
 xrtValueObjectSetNew(R,XRT_STR_LITERAL("public_private_and_admin_route_authority"),xrtValueBool(Public&&!MdStoreBool(Public,"login",true)&&Upload&&MdStoreBool(Upload,"login",false)&&Admin&&MdStoreBool(Admin,"admin",false)&&MdStoreInt(Admin,"auth",0)==7));
 xrtValueObjectSetNew(R,XRT_STR_LITERAL("old_package_routes_not_registered"),xrtValueBool(!SdkGet(QaRoutes,"/api/v1/mdo/ecosystem/packages")&&!SdkGet(QaRoutes,"/api/v1/mdo/cli/catalog")&&!SdkGet(QaRoutes,"/mdo/role-package.js")));
 sqlite3_stmt* Q=XP_SQL(H.main_db,"SELECT count(*) FROM menu WHERE isDelete=0");bool Preserved=Q&&sqlite3_step(Q)==SQLITE_ROW&&sqlite3_column_int(Q,0)==1;sqlite3_finalize(Q);xrtValueObjectSetNew(R,XRT_STR_LITERAL("only_obsolete_owned_navigation_retired"),xrtValueBool(Preserved));
 QaSiteRequest Request={0};Request.Method=XHTTP_METHOD_GET;Request.Path="/admin/mdo/extensions/mcp";Request.Csrf="XMLHttpRequest";MdDirectoryPage(NULL,NULL,&Request,&Request,NULL);bool Pages=!strcmp(QaPage,"extensions/component.html");Request.Csrf=NULL;MdDirectoryPage(NULL,NULL,&Request,&Request,NULL);Pages=Pages&&!strcmp(QaPage,"extensions/admin.html");Request.Path="/mdo/creator";MdDirectoryPage(NULL,NULL,&Request,&Request,NULL);Pages=Pages&&!strcmp(QaPage,"extensions/public.html");xrtValueObjectSetNew(R,XRT_STR_LITERAL("component_and_standalone_page_routing"),xrtValueBool(Pages));
 Request.Query=SdkParse("{\"page\":\"3\",\"limit\":\"20\"}");xvalue* Filter=MdDirectoryFilter(&Request);bool Paged=Filter&&MdStoreInt(Filter,"offset",-1)==40;xrtValueRelease(Filter);SdkPut(Request.Query,"offset","0");Filter=MdDirectoryFilter(&Request);Paged=Paged&&!Filter;xrtValueRelease(Filter);xrtValueRelease(Request.Query);xrtValueObjectSetNew(R,XRT_STR_LITERAL("layui_pages_map_to_row_offsets_and_reject_ambiguity"),xrtValueBool(Paged));
 MdoEcologyStop();sqlite3_close(H.main_db);xrtValueRelease(QaRoutes);return R;
}
'''
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--url',required=True);p.add_argument('--home',type=Path,required=True);p.add_argument('--project',default='default');a=p.parse_args()
    out=ROOT/'.build/platform-directory-startup-native';out.mkdir(parents=True,exist_ok=True);fixture=out/('home-'+uuid.uuid4().hex[:12]);fixture.mkdir();sdk=fixture/'plugin_sdk';sdk.mkdir();(fixture/'include/xadmin').mkdir(parents=True)
    for name in ('xs_plugin.h','xs_plugin_work.h','plugin_support.h','value_util.h'):shutil.copyfile(PLATFORM/'plugin_sdk'/name,sdk/name)
    shutil.copyfile(PLATFORM/'include/xadmin/time.h',fixture/'include/xadmin/time.h')
    shutil.copyfile(sdk_root()/'lib/sqlite3.h',sdk/'sqlite3.h')
    for name in ('extension_manifest.h','extension_identity.h','extension_path.h','extension_casefold.h','extension_zip.h'):shutil.copyfile(ECO/name,fixture/name)
    core=platform_store_source(ECO,None);routes=(ECO/'directory_routes.inc.c').read_text(encoding='utf-8').replace('#include "directory_store.inc.c"',core)
    startup=(ECO/'main.c').read_text(encoding='utf-8').replace('#include "directory_routes.inc.c"',routes)
    try:
        names=('initialize','bind_int','bind_text','prepare_v2','finalize','step','exec','column_count','column_name','column_type','column_int64','column_int','column_text','column_bytes','bind_int64','open','busy_timeout','close','changes')
        setup='QaSqlite=LoadLibraryW(L"C:/Program Files/Python312/DLLs/sqlite3.dll");tcc_add_include_path(T,"/app/qa/plugin_sdk");tcc_add_include_path(T,"/app/qa");'+''.join(f'tcc_add_symbol(T,"sqlite3_{name}",GetProcAddress(QaSqlite,"sqlite3_{name}"));' for name in names)
        result=product(a,fixture,PRELUDE+(PLATFORM/'src/net/plugin_owned_work.c').read_text(encoding='utf-8')+STUBS+startup+CONTRACT,out/'result.json',compiler_setup=setup,compiler_prelude='#include <windows.h>\nstatic HMODULE QaSqlite;',compiler_cleanup='if(QaSqlite)FreeLibrary(QaSqlite);')
        assert result['routes']==32,result
        assert all(value is True for key,value in result.items() if key!='routes'),result
        print(json.dumps(result,ensure_ascii=False))
    finally:
        if fixture.resolve().parent==out.resolve() and fixture.name.startswith('home-'):shutil.rmtree(fixture)
if __name__=='__main__':main()


