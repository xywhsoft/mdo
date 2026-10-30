"""Execute the production storage-warning projection with the native SDK."""
import argparse
from pathlib import Path
import tempfile
from runtime_sources import copy_app_source
from test_model_runtime import ROOT, write_site, run_probe

SOURCE=r'''
#include <xsbase.h>
#include <stdio.h>
#include "src/api/diagnostics_storage.inc.c"
void ServiceInit(XS_HostInfo* Host) {
    (void)Host;bool Ok=true;
    for(unsigned I=0;I<5;I++) {
        MdoHomeSnapshot Home={0};Home.Size=sizeof(Home);Home.Path="owned-home";
        Home.Persistence=I==0?MDO_PERSISTENCE_LAZY:I==1||I==3?MDO_PERSISTENCE_EXTERNAL:MDO_PERSISTENCE_EPHEMERAL;
        Home.RestartRequired=I>=3;
        xvalue* Rows=xrtValueArray();Ok=Ok&&Rows&&MdoApiStorageWarnings(Rows,&Home);
        size_t Count=xrtValueCount(Rows);Ok=Ok&&Count==(I<2?0:I==4?2:1);
        for(size_t J=0;J<Count;J++) {
            const xvalue* Row=xrtValueArrayGet(Rows,J);xstrview Value;
            Ok=Ok&&xrtValueGetString(xrtValueObjectGet(Row,XRT_STR_LITERAL("domain")),&Value)&&xrtStrEqual(Value,XRT_STR_LITERAL("storage"));
            Ok=Ok&&xrtValueGetString(xrtValueObjectGet(Row,XRT_STR_LITERAL("remedy")),&Value)&&Value.Size>0;
            Ok=Ok&&xrtValueGetString(xrtValueObjectGet(Row,XRT_STR_LITERAL("settings_section")),&Value)&&!Value.Size;
        }
        xrtValueRelease(Rows);
    }
    printf("storage_warnings=%d\nprobe_done=1\n",Ok);
}
'''
if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument("--host",type=Path,required=True);args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="storage-warning-",dir=ROOT/".build") as raw:
        base=Path(raw);write_site(base/"site");copy_app_source("src/api/diagnostics_storage.inc.c",base/"site")
        (base/"site/probe.c").write_text(SOURCE,encoding="utf-8")
        output=run_probe(args.host.resolve(),base/"site",base/"home");assert "storage_warnings=1" in output,output
    print("Storage current-warning projection: PASS (lazy/external/temporary/restart/combined)")
