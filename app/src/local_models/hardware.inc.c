/* Capability observations may be unknown. Never infer available VRAM from a
 * board's advertised capacity, or add it to unified system memory. */
static xvalue* LmHardwareRun(cstr Program,const cstr* Args,size_t Count){
 xprocessconfig C;xprocessrunoptions O;xprocessresult R={0};xrtProcessConfigInit(&C);xrtProcessRunOptionsInit(&O);
 C.Program=Program;C.Args=Args;C.ArgCount=Count;C.HideWindow=true;C.Stdin.Mode=XPROCESS_IO_NULL;O.Timeout=8000;O.StdoutLimit=131072;O.StderrLimit=2048;
 bool Ok=xrtProcessRun(&C,&O,&R)&&xrtProcessResultSuccess(&R);xvalue* V=Ok?xrtJsonParse(xrtStrViewN((cstr)R.Stdout,R.StdoutSize)):NULL;xrtProcessResultUnit(&R);return V;
}
static void LmCpuFeatures(xvalue* Out){
 xvalue* Features=xrtValueArray();bool Known=false;
#if defined(__x86_64__) || defined(__i386__)
 unsigned A=1,B,C,D;__asm__ volatile("cpuid":"=a"(A),"=b"(B),"=c"(C),"=d"(D):"a"(A),"c"(0));
 bool Sse2=(D&(1u<<26))!=0,Avx=false;unsigned Low=0,High=0;
 if((C&(1u<<27))&&(C&(1u<<28))){__asm__ volatile(".byte 0x0f, 0x01, 0xd0":"=a"(Low),"=d"(High):"c"(0));Avx=(Low&6)==6;}
 if(Sse2)xrtValueArrayAppendNew(Features,xrtValueString(XRT_STR_LITERAL("sse2")));
 if(Avx)xrtValueArrayAppendNew(Features,xrtValueString(XRT_STR_LITERAL("avx")));
 if(Avx&&(C&(1u<<12)))xrtValueArrayAppendNew(Features,xrtValueString(XRT_STR_LITERAL("fma")));
 if(Avx&&(C&(1u<<29)))xrtValueArrayAppendNew(Features,xrtValueString(XRT_STR_LITERAL("f16c")));
 A=0;__asm__ volatile("cpuid":"=a"(A),"=b"(B),"=c"(C),"=d"(D):"a"(A),"c"(0));
 if(A>=7){A=7;__asm__ volatile("cpuid":"=a"(A),"=b"(B),"=c"(C),"=d"(D):"a"(A),"c"(0));
  if(Avx&&(B&(1u<<5)))xrtValueArrayAppendNew(Features,xrtValueString(XRT_STR_LITERAL("avx2")));
  if(Avx&&(Low&224)==224&&(B&(1u<<16)))xrtValueArrayAppendNew(Features,xrtValueString(XRT_STR_LITERAL("avx512f")));
  if(B&(1u<<8))xrtValueArrayAppendNew(Features,xrtValueString(XRT_STR_LITERAL("bmi2")));
 }Known=true;
#elif defined(__aarch64__)
 xrtValueArrayAppendNew(Features,xrtValueString(XRT_STR_LITERAL("neon")));Known=true;
#endif
 xrtValueObjectSetNew(Out,XRT_STR_LITERAL("cpu_features"),Features);xrtValueObjectSetNew(Out,XRT_STR_LITERAL("cpu_features_known"),xrtValueBool(Known));
}
static xvalue* LmHardware(void){
 xvalue* Out=NULL;
#if defined(_WIN32)
 char* System=xrtEnvGet("SystemRoot");char* Program=System?xrtFormat("%s/System32/WindowsPowerShell/v1.0/powershell.exe",System):NULL;
 const cstr Args[]={"-NoLogo","-NoProfile","-NonInteractive","-Command","$ErrorActionPreference='Stop';$o=Get-CimInstance Win32_OperatingSystem;$g=@(Get-CimInstance Win32_VideoController | ForEach-Object { @{name=$_.Name;capacity_bytes=[uint64]$_.AdapterRAM;available_known=$false} });@{ram_bytes=[uint64]$o.TotalVisibleMemorySize*1024;available_ram_bytes=[uint64]$o.FreePhysicalMemory*1024;os_version=$o.Version;gpus=$g} | ConvertTo-Json -Compress -Depth 4"};
 if(Program)Out=LmHardwareRun(Program,Args,sizeof(Args)/sizeof(*Args));
 xrtFree(Program);xrtFree(System);
#elif defined(__linux__)
 xfile F=xrtOpen("/proc/meminfo",XFILE_READ);char Buffer[32768]={0};size_t N=0;
 bool Read=F&&xrtRead(F,Buffer,sizeof(Buffer)-1,&N);xrtClose(F);
 uint64 Total=0,Available=0;
 if(Read&&N>0){cstr T=strstr(Buffer,"MemTotal:"),A=strstr(Buffer,"MemAvailable:");unsigned long long X=0,Y=0;if(T&&A&&sscanf(T,"MemTotal: %llu",&X)==1&&sscanf(A,"MemAvailable: %llu",&Y)==1){Total=X*1024;Available=Y*1024;}}
 Out=xrtValueObject();xrtValueObjectSetNew(Out,XRT_STR_LITERAL("ram_bytes"),xrtValueUInt(Total));xrtValueObjectSetNew(Out,XRT_STR_LITERAL("available_ram_bytes"),xrtValueUInt(Available));
#elif defined(__APPLE__)
 const cstr Args[]={"-c","p=$(/usr/sbin/sysctl -n hw.pagesize);t=$(/usr/sbin/sysctl -n hw.memsize);f=$(/usr/bin/vm_stat | /usr/bin/awk '/Pages free|Pages inactive|Pages speculative/ {gsub(/[^0-9]/,\"\",$NF);s+=$NF} END {printf \"%.0f\",s}');printf '{\"ram_bytes\":%s,\"available_ram_bytes\":%s,\"unified_memory\":true}' \"$t\" \"$((p*f))\""};
 Out=LmHardwareRun("/bin/sh",Args,2);
#endif
 if(!Out)Out=xrtValueObject();
 LmPut(Out,"platform",MdoExtensionRuntimePlatform());LmCpuFeatures(Out);
 xrtValueObjectSetNew(Out,XRT_STR_LITERAL("ram_known"),xrtValueBool(LmUInt(Out,"available_ram_bytes")>0));
 xrtValueObjectSetNew(Out,XRT_STR_LITERAL("vram_available_known"),xrtValueBool(false));return Out;
}
