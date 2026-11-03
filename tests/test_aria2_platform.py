"""Compile and execute the actual platform range sender, with bounded I/O stubs."""
import argparse,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
HARNESS=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint64_t uint64;typedef int64_t int64;typedef uint16_t uint16;typedef uint8_t uint8;typedef const char* xfile;
typedef struct {const char* Data;size_t Size;} xstrview;
typedef struct {xstrview Name,Value;} xhttpfield;
#define XRT_STR_LITERAL(S) ((xstrview){S,sizeof(S)-1})
static xstrview xrtStrView(const char* S){return (xstrview){S,strlen(S)};}
typedef void* XS_RequestObject;typedef void* XS_ResponseObject;
typedef struct {xfile File;uint64 Size;bool Proof;char Hash[65],Id[33];} MdDirectoryCall;
#define MDS_CHUNK 8
static const char Packet[]="0123456789abcdefghijklmnopqrstuv";
static const char *Range,*IfRange,*Offset;
static unsigned Status;static size_t Written,ReadAt;static bool Finished;static char ContentRange[96];
static int xsReqQueryValue(void* R,const char* K,char* Out,size_t Cap){(void)R;(void)K;if(!Offset)return -1;snprintf(Out,Cap,"%s",Offset);return (int)strlen(Offset);}
static bool XP_Positive(const char* S,int64_t* Out){char* End;*Out=strtoll(S,&End,10);return !*End&&*Out>0;}
static void xrtClose(xfile F){(void)F;}
static void XP_Reply(void* R,unsigned Code,const char* E,void* D){(void)R;(void)E;(void)D;Status=Code;}
static int XAdmin_ReqHeaderCopy(void* R,const char* K,char* Out,size_t Cap){(void)R;const char* S=!strcmp(K,"Range")?Range:IfRange;if(!S)return -1;snprintf(Out,Cap,"%s",S);return (int)strlen(S);}
static int XAdmin_StreamBegin(void* R,unsigned Code,const xhttpfield* Fields,size_t Count,unsigned Timeout){
 (void)R;(void)Timeout;Status=Code;for(size_t I=0;I<Count;I++)if(!strcmp(Fields[I].Name.Data,"Content-Range"))snprintf(ContentRange,sizeof(ContentRange),"%s",Fields[I].Value.Data);return 0;
}
static bool xrtReadAtFull(xfile F,uint64 At,void* Out,size_t N,void* E){(void)E;assert(At+N<=sizeof(Packet)-1);ReadAt=(size_t)At;memcpy(Out,F+At,N);return true;}
static int XAdmin_StreamWrite(void* R,const void* Data,size_t N){(void)R;assert(!memcmp(Data,Packet+ReadAt,N));Written+=N;return 0;}
static void XAdmin_StreamFinish(void* R,bool Ok){(void)R;Finished=Ok;}
@@HEADER@@
@@SENDER@@
static void Run(const char* Header,const char* Validator,const char* Slice,unsigned Code,size_t Bytes,const char* ExpectedRange){
 Range=Header;IfRange=Validator;Offset=Slice;Status=0;Written=0;Finished=false;ContentRange[0]=0;
 MdDirectoryCall C={0};C.File=Packet;C.Size=sizeof(Packet)-1;strcpy(C.Hash,"hash");strcpy(C.Id,"fixture");MdDirectorySend(NULL,NULL,&C);
 assert(!C.File);assert(Status==Code);assert(Written==Bytes);assert(Finished);assert(!strcmp(ContentRange,ExpectedRange));
}
int main(void){
 Run(NULL,NULL,NULL,200,32,"");Run("bytes=2-5",NULL,NULL,206,4,"bytes 2-5/32");Run("bytes=4-",NULL,NULL,206,28,"bytes 4-31/32");
 Run("bytes=-3",NULL,NULL,206,3,"bytes 29-31/32");Run("bytes=1-999",NULL,NULL,206,31,"bytes 1-31/32");Run("bytes=-999",NULL,NULL,206,32,"bytes 0-31/32");
 Run("bytes=1-2","\"hash\"",NULL,206,2,"bytes 1-2/32");Run("bytes=1-2","\"old\"",NULL,200,32,"");Run("bytes=1-2","W/\"hash\"",NULL,200,32,"");
 const char* Bad[]={"bytes=32-","bytes=9-1","bytes=-0","bytes=1-2,4-5","bytes=18446744073709551616-","bytes=0-18446744073709551616","items=0-1","bytes=","bytes=0--1"};
 for(size_t I=0;I<sizeof(Bad)/sizeof(*Bad);I++)Run(Bad[I],NULL,NULL,416,0,"bytes */32");
 Run(NULL,NULL,"2",200,8,"");Run("bytes=20-25",NULL,"0",200,8,"");
 assert(MdDownloadSelectRange("bytes=0-",NULL,"tag",0).Status==416);
 assert(MdDownloadSelectRange("bytes=18446744073709551615-",NULL,"tag",UINT64_MAX).Status==416);
 puts("PASS actual platform sender: full/ranged/suffix/If-Range/416/overflow and legacy offset responses (22 checks)");
}
'''
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--platform-root',type=Path,default=Path('D:/GIT/home'));args=parser.parse_args()
    source=args.platform_root/'host/xywhsoft_ai/plugin/mdo/modules/ecosystem'
    route=(source/'directory_routes.inc.c').read_text(encoding='utf-8')
    sender=route[route.index('static void MdDirectorySend('):route.index('static void MdDirectoryRouteWork(')]
    with tempfile.TemporaryDirectory(dir=ROOT/'.build',prefix='aria2-range-') as raw:
        base=Path(raw);c=base/'range.c';exe=base/'range.exe'
        c.write_text(HARNESS.replace('@@HEADER@@',(source/'download_range.h').read_text(encoding='utf-8')).replace('@@SENDER@@',sender),encoding='utf-8')
        subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',str(c),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
if __name__=='__main__':main()
