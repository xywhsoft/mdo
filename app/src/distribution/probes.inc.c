/* Offline functional probes: never contact a user's SSH host or use credentials. */
static bool MdoDistToolProbe(size_t Index,cstr Path,char Version[257])
{
    const cstr VersionArgs[]={Index==0?"--help":Index==3?"-V":Index==5||Index==6?"-h":Index==9?"i":"--version"};
    if(!MdoDistRun(Path,VersionArgs,1,NULL,Index==5||Index==6?1:0,Version))return false;
    if(Index==7)return MdoDistRunCheck(Path,VersionArgs,1,NULL,0,NULL,"HTTPS");
    if(Index==8||Index==9) {
        char Relative[128],Input[160],Archive[160],Output[160],Directory[160];
        snprintf(Relative,sizeof(Relative),"data/toolpacks/extra-probe-%llu",(unsigned long long)xrtClock());
        if(!MdoHomeCreateDirectory(Relative))return false;
        snprintf(Input,sizeof(Input),"%s/input",Relative);snprintf(Archive,sizeof(Archive),"%s/probe.7z",Relative);
        snprintf(Output,sizeof(Output),"%s/out/input",Relative);snprintf(Directory,sizeof(Directory),"%s/out",Relative);
        cstr Body="mdo_probe\n中文内容\n";str Work=MdoHomeExternalPath(Relative);
        bool Ok=MdoHomeAtomicWrite(Input,Body,strlen(Body),false);
        if(Index==8) {const cstr Args[]={"--no-config","--json","--fixed-strings","中文内容","input"};Ok=Ok&&MdoDistRunCheck(Path,Args,5,Work,0,NULL,"中文内容");}
        else {
            const cstr Create[]={"a","-t7z","-mx=1","-y","probe.7z","input"},Test[]={"t","-y","probe.7z"},Extract[]={"x","-y","-oout","probe.7z"};
            Ok=Ok&&MdoDistRun(Path,Create,6,Work,0,NULL)&&MdoDistRun(Path,Test,3,Work,0,NULL)&&MdoDistRun(Path,Extract,4,Work,0,NULL);
            xfile File=Ok?MdoHomeOpenRead(Output):NULL;char Buffer[64]={0};size_t Got=0;
            Ok=File&&xrtRead(File,Buffer,sizeof(Buffer),&Got)&&Got==strlen(Body)&&!memcmp(Buffer,Body,Got);xrtClose(File);
        }
        MdoHomeRemove(Output,false);MdoHomeRemoveEmptyDirectory(Directory);MdoHomeRemove(Archive,false);MdoHomeRemove(Input,false);MdoHomeRemoveEmptyDirectory(Relative);xrtFree(Work);return Ok;
    }
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
    if(Index==6) {
        if(!strstr(Version,"usage:"))return false;
        str SshDir=xrtPathParent(Path),Root=SshDir?xrtPathParent(SshDir):NULL;
#if defined(__ANDROID__) || defined(__linux__)
        str Busybox=Root?xrtPathJoin(Root,"busybox/busybox"):NULL;
#else
        str Busybox=Root?xrtPathJoin(Root,"busybox/busybox.exe"):NULL;
#endif
        char Relative[128],Script[160],Batch[160],Command[1024];snprintf(Relative,sizeof(Relative),"data/toolpacks/sftp-probe-%llu",(unsigned long long)xrtClock());
        bool Ok=Busybox&&MdoHomeCreateDirectory(Relative);snprintf(Script,sizeof(Script),"%s/probe.sh",Relative);snprintf(Batch,sizeof(Batch),"%s/batch",Relative);
        /* Tiny local SFTP v3 peer: INIT, VERSION, REALPATH and NAME. No network/authentication. */
        cstr Body="\"$1\" dd bs=1 count=9 >/dev/null 2>/dev/null\nprintf '\\000\\000\\000\\005\\002\\000\\000\\000\\003'\n\"$1\" dd bs=1 count=14 >/dev/null 2>/dev/null\nprintf '\\000\\000\\000\\027\\150\\000\\000\\000\\001\\000\\000\\000\\001\\000\\000\\000\\001.\\000\\000\\000\\001.\\000\\000\\000\\000'\n\"$1\" cat >/dev/null\n";
        if(Busybox)for(char* p=Busybox;*p;p++)if(*p=='\\')*p='/';
        snprintf(Command,sizeof(Command),"\"%s\" sh probe.sh \"%s\"",Busybox?Busybox:"",Busybox?Busybox:"");str Work=MdoHomeExternalPath(Relative);
        const cstr Args[]={"-D",Command,"-b","batch"};Ok=Ok&&MdoHomeAtomicWrite(Script,Body,strlen(Body),false)&&MdoHomeAtomicWrite(Batch,"quit\n",5,false)&&MdoDistRun(Path,Args,4,Work,0,NULL);
        MdoHomeRemove(Script,false);MdoHomeRemove(Batch,false);MdoHomeRemoveEmptyDirectory(Relative);xrtFree(Work);xrtFree(Busybox);xrtFree(Root);xrtFree(SshDir);if(!Ok)return false;
    }
    str Parent=xrtPathParent(Path);
#if defined(__ANDROID__) || defined(__linux__)
    str Ssh=Parent?xrtPathJoin(Parent,"ssh"):NULL;
#else
    str Ssh=Parent?xrtPathJoin(Parent,"ssh.exe"):NULL;
#endif
    const cstr Args[]={"-V"};bool Ok=Ssh&&MdoDistRun(Ssh,Args,1,NULL,0,Version);xrtFree(Ssh);xrtFree(Parent);return Ok;
}
