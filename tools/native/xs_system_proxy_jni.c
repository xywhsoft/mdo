/* Included by host_jni.c. Proxy discovery is optional at library startup. */
static JavaVM* SystemProxyVm;
static jclass SystemProxyClass;
static jmethodID SystemProxyMethod;

static void XS_SystemProxyJniInit(JavaVM* Vm, JNIEnv* Env)
{
    jclass Local = (*Env)->FindClass(Env, "org/xserver/android/SystemProxy");
    if (Local) {
        SystemProxyClass = (*Env)->NewGlobalRef(Env, Local);
        (*Env)->DeleteLocalRef(Env, Local);
    }
    if (SystemProxyClass) SystemProxyMethod = (*Env)->GetStaticMethodID(Env,
        SystemProxyClass, "resolve", "(Ljava/lang/String;I)[Ljava/lang/String;");
    if ((*Env)->ExceptionCheck(Env)) (*Env)->ExceptionClear(Env);
    if (!SystemProxyMethod) {
        if (SystemProxyClass) (*Env)->DeleteGlobalRef(Env, SystemProxyClass);
        SystemProxyClass = NULL;
    } else SystemProxyVm = Vm;
}

static void XS_SystemProxyJniUnit(JNIEnv* Env)
{
    if (SystemProxyClass) (*Env)->DeleteGlobalRef(Env, SystemProxyClass);
    SystemProxyClass = NULL; SystemProxyMethod = NULL; SystemProxyVm = NULL;
}

static bool XS_SystemProxyJniText(JNIEnv* Env, jobjectArray Values, jsize Index,
    char* Out, size_t Capacity)
{
    jstring Value = (jstring)(*Env)->GetObjectArrayElement(Env, Values, Index);
    const char* Text = Value ? (*Env)->GetStringUTFChars(Env, Value, NULL) : NULL;
    bool Ok = Text && strlen(Text) < Capacity;
    if (Ok) memcpy(Out, Text, strlen(Text) + 1u);
    if (Text) (*Env)->ReleaseStringUTFChars(Env, Value, Text);
    if (Value) (*Env)->DeleteLocalRef(Env, Value);
    return Ok && !(*Env)->ExceptionCheck(Env);
}

bool XS_AndroidSystemProxy(cstr Url, uint32 TimeoutMs, XS_SystemProxyInfo* Result)
{
    JNIEnv* Env = NULL; bool Detach = false, Ok = false;
    jstring Target = NULL; jobjectArray Values = NULL;
    if (!SystemProxyVm || !SystemProxyClass || !SystemProxyMethod) goto done;
    jint Status = (*SystemProxyVm)->GetEnv(SystemProxyVm, (void**)&Env, JNI_VERSION_1_6);
    if (Status == JNI_EDETACHED) {
        if ((*SystemProxyVm)->AttachCurrentThread(SystemProxyVm, &Env, NULL) != JNI_OK) goto done;
        Detach = true;
    } else if (Status != JNI_OK) goto done;
    Target = (*Env)->NewStringUTF(Env, Url);
    if (!Target || (*Env)->ExceptionCheck(Env)) goto done;
    Values = (jobjectArray)(*Env)->CallStaticObjectMethod(Env, SystemProxyClass,
        SystemProxyMethod, Target, (jint)TimeoutMs);
    if (!Values || (*Env)->ExceptionCheck(Env)) goto done;
    jsize Count = (*Env)->GetArrayLength(Env, Values);
    char Kind[16] = {0}, Port[8] = {0};
    if (!Count || !XS_SystemProxyJniText(Env, Values, 0, Kind, sizeof(Kind))) goto done;
    if (Count == 1 && !strcmp(Kind, "direct")) { Ok = true; goto done; }
    if (Count != 3 || (strcmp(Kind, "http") && strcmp(Kind, "socks5")) ||
        !XS_SystemProxyJniText(Env, Values, 1, Result->Host, sizeof(Result->Host)) ||
        !XS_SystemProxyJniText(Env, Values, 2, Port, sizeof(Port))) goto done;
    unsigned Number = 0;
    for (size_t I = 0; Port[I]; ++I) {
        if (Port[I] < '0' || Port[I] > '9' || Number > 6553u) goto done;
        Number = Number * 10u + (unsigned)(Port[I] - '0');
    }
    if (!Number || Number > 65535u || !Result->Host[0]) goto done;
    for (size_t I = 0; Result->Host[I]; ++I)
        if ((unsigned char)Result->Host[I] <= 32u || (unsigned char)Result->Host[I] > 126u ||
            strchr("/@?#\\", Result->Host[I])) goto done;
    Result->Port = (uint16)Number;
    Result->Kind = !strcmp(Kind, "http") ? XS_SYSTEM_PROXY_HTTP_CONNECT : XS_SYSTEM_PROXY_SOCKS5;
    Ok = true;
done:
    if (Env) {
        if ((*Env)->ExceptionCheck(Env)) (*Env)->ExceptionClear(Env);
        if (Values) (*Env)->DeleteLocalRef(Env, Values);
        if (Target) (*Env)->DeleteLocalRef(Env, Target);
    }
    if (Detach) (*SystemProxyVm)->DetachCurrentThread(SystemProxyVm);
    if (!Ok) {
        Result->Kind = XS_SYSTEM_PROXY_DIRECT; Result->Port = 0;
        Result->Host[0] = 0;
        snprintf(Result->Reason, sizeof(Result->Reason), "%s",
            "Android system proxy resolution failed; check network proxy settings or use a manual proxy");
    }
    return Ok;
}
