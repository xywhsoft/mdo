/* First-party PIE helper installed by Android's PackageManager, not executed
 * from writable application data. Uses the same bundled xs/TCC runtime. */
#include <dlfcn.h>
#include <stddef.h>
#include "xs_host.h"
int main(int argc,char** argv) {
    if(argc!=3)return 64;
    void* library=dlopen(argv[2],RTLD_NOW|RTLD_LOCAL);if(!library)return 69;
    union {void* address;int (*run)(const xs_host_options*);} entry;
    entry.address=dlsym(library,"xsHostRun");if(!entry.run){dlclose(library);return 69;}
    xs_host_options options={0};options.Size=sizeof(options);options.ApplicationRoot=argv[1];
    int code=entry.run(&options);dlclose(library);return code;
}
