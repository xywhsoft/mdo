/* Offline functional probes: never contact a user's SSH host or use credentials. */
static bool MdoDistToolProbe(size_t Index,cstr Path,char Version[257])
{
    const cstr VersionArgs[]={Index==0?"--help":Index==3?"-V":Index>4?"-h":"--version"};
    if(!MdoDistRun(Path,VersionArgs,1,NULL,Index>4?1:0,Version))return false;
    if(Index==0) {const cstr Args[]={"sh","-c","printf mdo_probe | cat"};char Out[257]={0};return MdoDistRun(Path,Args,3,NULL,0,Out)&&!strcmp(Out,"mdo_probe");}
    if(Index==1)return MdoDistRunCheck(Path,VersionArgs,1,NULL,0,NULL,"https");
    if(Index==2) {const cstr Args[]={"-n","-e","{verified:true}.verified"};char Out[257]={0};return MdoDistRun(Path,Args,3,NULL,0,Out)&&!strcmp(Out,"true");}
    if(Index==3) {const cstr Args[]={"-F","none","-G","localhost"};char Out[257]={0};return MdoDistRun(Path,Args,4,NULL,0,Out)&&strstr(Out,"host ")!=NULL;}
    if(Index==4) {
        const cstr Args[]={"-I","-c","import json,ssl,sqlite3,zipfile,hashlib,urllib.request,io; c=ssl.create_default_context(); assert c.verify_mode==ssl.CERT_REQUIRED; d=sqlite3.connect(':memory:'); assert d.execute('select 42').fetchone()[0]==42; b=io.BytesIO(); z=zipfile.ZipFile(b,'w'); z.writestr('probe','ok'); z.close(); assert zipfile.ZipFile(b).read('probe')==b'ok'; assert json.loads(json.dumps({'ok':True}))['ok']; print('mdo_python_verified')"};
        char Out[257]={0};return MdoDistRun(Path,Args,3,NULL,0,Out)&&!strcmp(Out,"mdo_python_verified");
    }
    if(Index==5) {
        char Relative[128];snprintf(Relative,sizeof(Relative),"data/toolpacks/probe-%llu",(unsigned long long)xrtClock());
        if(!MdoHomeCreateDirectory(Relative))return false;
        char Input[160],Output[160];snprintf(Input,sizeof(Input),"%s/input",Relative);snprintf(Output,sizeof(Output),"%s/output",Relative);
        str Work=MdoHomeExternalPath(Relative);const cstr Args[]={"input","output"};
        bool Ok=MdoHomeAtomicWrite(Input,"mdo_copy_probe",14,false)&&MdoDistRun(Path,Args,2,Work,0,NULL);
        xfile File=Ok?MdoHomeOpenRead(Output):NULL;char Buffer[16]={0};size_t Got=0;
        Ok=File&&xrtRead(File,Buffer,sizeof(Buffer),&Got)&&Got==14&&!memcmp(Buffer,"mdo_copy_probe",14);
        xrtClose(File);MdoHomeRemove(Input,false);MdoHomeRemove(Output,false);MdoHomeRemoveEmptyDirectory(Relative);xrtFree(Work);if(!Ok)return false;
    }
    if(Index==6&&!strstr(Version,"usage:"))return false;
    str Parent=xrtPathParent(Path);
#if defined(__ANDROID__)
    str Ssh=Parent?xrtPathJoin(Parent,"ssh"):NULL;
#else
    str Ssh=Parent?xrtPathJoin(Parent,"ssh.exe"):NULL;
#endif
    const cstr Args[]={"-V"};bool Ok=Ssh&&MdoDistRun(Ssh,Args,1,NULL,0,Version);xrtFree(Ssh);xrtFree(Parent);return Ok;
}
