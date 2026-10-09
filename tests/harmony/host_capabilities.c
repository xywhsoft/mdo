/* Read-only Windows checks for the HarmonyOS emulator. No visible window is
 * created, and no Windows feature, driver, or security setting is changed. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include <cpuid.h>
#include <stdio.h>

int main(void)
{
    unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
    const int cpuid_ok = __get_cpuid(1, &eax, &ebx, &ecx, &edx);
    HWND window = CreateWindowA("STATIC", "mdo-harmony-preflight", 0,
        0, 0, 1, 1, NULL, NULL, GetModuleHandleA(NULL), NULL);
    if (window == NULL) {
        fprintf(stderr, "CreateWindow failed: %lu\n", GetLastError());
        return 1;
    }
    HDC dc = GetDC(window);
    PIXELFORMATDESCRIPTOR pixel = {
        .nSize = sizeof(PIXELFORMATDESCRIPTOR), .nVersion = 1,
        .dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL,
        .iPixelType = PFD_TYPE_RGBA, .cColorBits = 24,
        .iLayerType = PFD_MAIN_PLANE
    };
    const int format = dc != NULL ? ChoosePixelFormat(dc, &pixel) : 0;
    HGLRC context = NULL;
    int result = 1;
    if (format != 0 && SetPixelFormat(dc, format, &pixel)) {
        context = wglCreateContext(dc);
    }
    if (context != NULL && wglMakeCurrent(dc, context)) {
        const char *version = (const char *)glGetString(GL_VERSION);
        const char *renderer = (const char *)glGetString(GL_RENDERER);
        const char *vendor = (const char *)glGetString(GL_VENDOR);
        /* Driver strings are recorded as lines; the runner handles JSON
         * escaping instead of embedding untrusted driver text in JSON here. */
        printf("opengl_version=%s\n", version != NULL ? version : "unknown");
        printf("opengl_renderer=%s\n", renderer != NULL ? renderer : "unknown");
        printf("opengl_vendor=%s\n", vendor != NULL ? vendor : "unknown");
        result = version != NULL ? 0 : 1;
    } else {
        fprintf(stderr, "OpenGL context failed: %lu\n", GetLastError());
    }
    printf("aes=%d\n", cpuid_ok && (ecx & (1u << 25)) != 0);
    printf("hypervisor=%d\n", cpuid_ok && (ecx & (1u << 31)) != 0);
    printf("virtualization_firmware=%d\n",
        IsProcessorFeaturePresent(PF_VIRT_FIRMWARE_ENABLED) != 0);
    /* SLAT may be hidden by an active hypervisor. Record it, but do not
     * interpret a false flag as proof that the physical CPU lacks SLAT. */
    printf("slat_visible=%d\n",
        IsProcessorFeaturePresent(PF_SECOND_LEVEL_ADDRESS_TRANSLATION) != 0);
    wglMakeCurrent(NULL, NULL);
    if (context != NULL) wglDeleteContext(context);
    if (dc != NULL) ReleaseDC(window, dc);
    DestroyWindow(window);
    return result;
}
