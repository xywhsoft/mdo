/* A portable path is at most 220 bytes, with nonempty components. Even a tree
 * of one-byte directory names therefore has at most 110 levels. Walk on the
 * heap, so accepting a deep valid package never consumes an unbounded C stack.
 * All readers use rooted, no-follow directory opens and identical validation. */
#define MDO_PORTABLE_TREE_DEPTH 110u
#define MDO_PORTABLE_TREE_ENTRIES 32768u
typedef bool (*MdoPortableTreeFile)(cstr Relative,const xfileinfo* Info,void* Data);
typedef struct MdoPortableTreeFrame {xdir Directory;char Prefix[221];} MdoPortableTreeFrame;
static bool MdoPortableTreeWalk(cstr Root,MdoPortableTreeFile Visit,void* Data)
{
    MdoPortableTreeFrame* Frames=xrtCalloc(MDO_PORTABLE_TREE_DEPTH+1u,sizeof(*Frames));
    if(!Frames)return false;
    unsigned Level=0;size_t Entries=0;bool Ok=true;
    Frames[0].Directory=MdoHomeOpenDirectory(Root,XDIR_STAT);
    if(!Frames[0].Directory)Ok=false;
    while(Ok){
        MdoPortableTreeFrame* Frame=&Frames[Level];xdirentry E;
        xdirnext Next=xrtDirNext(Frame->Directory,&E);
        if(Next==XDIR_NEXT_END){xrtDirClose(Frame->Directory);Frame->Directory=NULL;if(!Level)break;--Level;continue;}
        if(Next!=XDIR_NEXT_ITEM||++Entries>MDO_PORTABLE_TREE_ENTRIES||!(E.Flags&XDIR_ENTRY_UTF8)||!E.Name.Size||E.Name.Size>64||memchr(E.Name.Data,0,E.Name.Size)){Ok=false;break;}
        char Relative[221];int N=snprintf(Relative,sizeof(Relative),"%s%s%.*s",Frame->Prefix,Frame->Prefix[0]?"/":"",(int)E.Name.Size,E.Name.Data);
        if(N<0||(size_t)N>=sizeof(Relative)||!MdoPackagePath(Relative)){Ok=false;break;}
        if(E.Info.Type==XFILE_TYPE_DIRECTORY){
            if(Level>=MDO_PORTABLE_TREE_DEPTH){Ok=false;break;}
            char Directory[512];N=snprintf(Directory,sizeof(Directory),"%s/%s",Root,Relative);
            if(N<0||(size_t)N>=sizeof(Directory)){Ok=false;break;}
            ++Level;memcpy(Frames[Level].Prefix,Relative,strlen(Relative)+1);
            Frames[Level].Directory=MdoHomeOpenDirectory(Directory,XDIR_STAT);
            if(!Frames[Level].Directory)Ok=false;
        }else if(E.Info.Type==XFILE_TYPE_FILE)Ok=Visit(Relative,&E.Info,Data);
        else Ok=false; /* Never follow links, junctions, devices or pipes. */
    }
    for(unsigned I=0;I<=MDO_PORTABLE_TREE_DEPTH;I++)if(Frames[I].Directory)xrtDirClose(Frames[I].Directory);
    xrtFree(Frames);return Ok;
}
