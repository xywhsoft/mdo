/* Community command-line environments are read from committed descriptors.
 * This read never starts a program, edits PATH or creates Home. */
static void MdoDistCommunityTools(xvalue* Tools,xvalue* Instructions)
{
    xdir Directory=MdoHomeOpenDirectory("data/ecosystem/toolpacks",XDIR_STAT);
    if(!Directory){xrtClearError();return;}xdirentry Entry;size_t Count=0,PromptBytes=0;
    while(Count<64&&xrtDirNext(Directory,&Entry)==XDIR_NEXT_ITEM){
        size_t N=Entry.Name.Size;if(Entry.Info.Type!=XFILE_TYPE_FILE||N<6||N>69||memcmp(Entry.Name.Data+N-5,".json",5))continue;
        char Id[65],Path[384];memcpy(Id,Entry.Name.Data,N-5);Id[N-5]=0;if(!MdoPackageId(Id))continue;Count++;
        snprintf(Path,sizeof(Path),"data/ecosystem/toolpacks/%s.json",Id);xvalue* Descriptor=MdoDistRead(Path,1048576);
        cstr Root=MdoPackageText(Descriptor,"root",220),Prompt=MdoPackageText(Descriptor,"prompt",32768);
        bool Enabled=true;const xvalue* Flag=xrtValueObjectGet(Descriptor,XRT_STR_LITERAL("enabled"));
        if(Flag&&!xrtValueGetBool(Flag,&Enabled))Enabled=false;
        if(!Enabled||!Root||strncmp(Root,"data/ecosystem/toolpacks/",24)||!MdoPackagePath(Root)){xrtValueRelease(Descriptor);continue;}
        const xvalue* Programs=xrtValueObjectGet(Descriptor,XRT_STR_LITERAL("programs"));size_t Added=0;
        for(size_t i=0;i<xrtValueCount(Programs)&&i<32;i++){
            const xvalue* Program=xrtValueArrayGet(Programs,i);cstr Name=MdoPackageText(Program,"name",64),Relative=MdoPackageText(Program,"path",100);
            if(!MdoPackageId(Name)||!MdoPackagePath(Relative))continue;
            snprintf(Path,sizeof(Path),"%s/%s",Root,Relative);bool Exists=false;xfileinfo Info;
            if(!MdoHomeExternalStat(Path,&Exists,&Info)||!Exists||Info.Type!=XFILE_TYPE_FILE)continue;
            char* Native=MdoHomeExternalPath(Path);xvalue* Tool=xrtValueClone(Program);char Qualified[140];snprintf(Qualified,sizeof(Qualified),"%s.%s",Id,Name);
            bool Ok=Native&&Tool&&MdoDistSet(Tool,"name",Qualified)&&MdoDistSet(Tool,"id",Qualified)&&MdoDistSet(Tool,"package",Id)&&MdoDistSet(Tool,"path",Native)&&MdoDistSet(Tool,"verification","file-present")&&xrtValueArrayAppend(Tools,Tool);
            if(Ok)Added++;
            xrtFree(Native);xrtValueRelease(Tool);
        }
        if(Added&&Instructions&&Prompt&&Prompt[0]&&strlen(Prompt)<=16384u-PromptBytes){
            xvalue* Item=xrtValueObject();if(Item&&MdoDistSet(Item,"package",Id)&&MdoDistSet(Item,"instructions",Prompt))xrtValueArrayAppend(Instructions,Item);
            xrtValueRelease(Item);PromptBytes+=strlen(Prompt);
        }
        xrtValueRelease(Descriptor);
    }
    (void)xrtDirClose(Directory);xrtClearError();
}
