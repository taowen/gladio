#define _GNU_SOURCE
#include "gladio.h"
#include "gl_context.h"

#include <EGL/egl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * Minimal EGL WSI for Wayland clients. Rendering still goes through the
 * Gladio rings + host GLES AHB. The native window is a wl_egl_window.
 */

struct gladio_wl_egl_window {
    void *surface;
    int width;
    int height;
};

struct GladioEGLSurface {
    struct gladio_wl_egl_window *win; /* NULL for pbuffer */
    int width;
    int height;
    int is_pbuffer;
};

#define GLADIO_EGL_DISPLAY ((EGLDisplay)(intptr_t)0x474c4144)
#define GLADIO_EGL_CONFIG ((EGLConfig)(intptr_t)1)

static EGLint g_egl_error = EGL_SUCCESS;
static int g_egl_interval;
static int g_egl_context_id = 1;
static EGLContext g_egl_current_ctx;
static EGLSurface g_egl_current_surf;
static EGLDisplay g_egl_current_dpy;

static void egl_fail(EGLint err)
{
    g_egl_error = err;
}

static int egl_set_window(GLXContext ctx, struct GladioEGLSurface *surf)
{
    ArrayBuffer requestData = {0};
    int w = 0;
    int h = 0;

    if (!ctx || !surf) {
        return 0;
    }
    if (surf->win) {
        w = surf->win->width;
        h = surf->win->height;
    }
    if (w <= 0) {
        w = surf->width;
    }
    if (h <= 0) {
        h = surf->height;
    }
    if (w <= 0) {
        w = 1;
    }
    if (h <= 0) {
        h = 1;
    }
    surf->width = w;
    surf->height = h;
    ArrayBuffer_putInt(&requestData, 0);
    ArrayBuffer_putInt(&requestData, ctx->id);
    ArrayBuffer_putInt(&requestData, w);
    ArrayBuffer_putInt(&requestData, h);
    GL_SEND_CHECKED(REQUEST_CODE_SET_CURRENT_RENDER_WINDOW, requestData.buffer,
                    requestData.size, 0);
    GL_RECV_CHECKED(0);
    return 1;
}

EGLDisplay eglGetDisplay(EGLNativeDisplayType display_id)
{
    (void)display_id;
    return GLADIO_EGL_DISPLAY;
}

EGLDisplay eglGetPlatformDisplay(EGLenum platform, void *native_display,
                                 const EGLAttrib *attrib_list)
{
    (void)platform;
    (void)native_display;
    (void)attrib_list;
    return GLADIO_EGL_DISPLAY;
}

EGLBoolean eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor)
{
    if (dpy != GLADIO_EGL_DISPLAY) {
        egl_fail(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }
    if (!gladioInitOnce(NULL)) {
        egl_fail(EGL_NOT_INITIALIZED);
        return EGL_FALSE;
    }
    if (major) {
        *major = 1;
    }
    if (minor) {
        *minor = 5;
    }
    g_egl_error = EGL_SUCCESS;
    return EGL_TRUE;
}

EGLBoolean eglTerminate(EGLDisplay dpy)
{
    (void)dpy;
    return EGL_TRUE;
}

EGLBoolean eglBindAPI(EGLenum api)
{
    if (api != EGL_OPENGL_ES_API && api != EGL_OPENGL_API) {
        egl_fail(EGL_BAD_PARAMETER);
        return EGL_FALSE;
    }
    return EGL_TRUE;
}

EGLenum eglQueryAPI(void)
{
    return EGL_OPENGL_ES_API;
}

const char *eglQueryString(EGLDisplay dpy, EGLint name)
{
    (void)dpy;
    if (name == EGL_VENDOR) {
        return "Gladio";
    }
    if (name == EGL_VERSION) {
        return "1.5 Gladio";
    }
    if (name == EGL_EXTENSIONS) {
        return "EGL_KHR_platform_wayland EGL_KHR_surfaceless_context";
    }
    if (name == EGL_CLIENT_APIS) {
        return "OpenGL_ES OpenGL";
    }
    return NULL;
}

EGLBoolean eglGetConfigs(EGLDisplay dpy, EGLConfig *configs, EGLint config_size,
                         EGLint *num_config)
{
    (void)dpy;
    if (!num_config) {
        egl_fail(EGL_BAD_PARAMETER);
        return EGL_FALSE;
    }
    *num_config = 1;
    if (configs && config_size > 0) {
        configs[0] = GLADIO_EGL_CONFIG;
    }
    return EGL_TRUE;
}

EGLBoolean eglChooseConfig(EGLDisplay dpy, const EGLint *attrib_list,
                           EGLConfig *configs, EGLint config_size,
                           EGLint *num_config)
{
    (void)dpy;
    (void)attrib_list;
    if (!num_config) {
        egl_fail(EGL_BAD_PARAMETER);
        return EGL_FALSE;
    }
    *num_config = 1;
    if (configs && config_size > 0) {
        configs[0] = GLADIO_EGL_CONFIG;
    }
    return EGL_TRUE;
}

EGLBoolean eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config, EGLint attribute,
                              EGLint *value)
{
    (void)dpy;
    (void)config;
    if (!value) {
        egl_fail(EGL_BAD_PARAMETER);
        return EGL_FALSE;
    }
    switch (attribute) {
    case EGL_RED_SIZE:
    case EGL_GREEN_SIZE:
    case EGL_BLUE_SIZE:
    case EGL_ALPHA_SIZE:
        *value = 8;
        break;
    case EGL_DEPTH_SIZE:
        *value = 24;
        break;
    case EGL_STENCIL_SIZE:
        *value = 8;
        break;
    case EGL_SURFACE_TYPE:
        *value = EGL_WINDOW_BIT | EGL_PBUFFER_BIT;
        break;
    case EGL_CONFIG_ID:
        *value = 1;
        break;
    case EGL_RENDERABLE_TYPE:
        *value = EGL_OPENGL_ES2_BIT | EGL_OPENGL_BIT;
        break;
    case EGL_NATIVE_VISUAL_ID:
        *value = 0;
        break;
    default:
        *value = 0;
        break;
    }
    return EGL_TRUE;
}

EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig config,
                            EGLContext share_context, const EGLint *attrib_list)
{
    GLXContext share = share_context;
    int contextId;
    ArrayBuffer requestData = {0};
    ArrayBuffer replyData = {0};

    (void)config;
    (void)attrib_list;
    if (dpy != GLADIO_EGL_DISPLAY) {
        egl_fail(EGL_BAD_DISPLAY);
        return EGL_NO_CONTEXT;
    }
    if (!gladioInitOnce(NULL)) {
        egl_fail(EGL_NOT_INITIALIZED);
        return EGL_NO_CONTEXT;
    }
    pthread_mutex_lock(&gl_call_mutex);
    contextId = g_egl_context_id++;
    ArrayBuffer_putInt(&requestData, contextId);
    ArrayBuffer_putInt(&requestData, 0);
    ArrayBuffer_putInt(&requestData, 0);
    ArrayBuffer_putInt(&requestData, share ? share->id : 0);
    ArrayBuffer_put(&requestData, 1);
    ArrayBuffer_putBytes(&requestData, NULL, 3);
    if (!glx_send(serverFd, GLX_OPCODE_CREATE_CONTEXT, requestData.buffer,
                  requestData.size) ||
            !glx_recv(serverFd, &replyData)) {
        pthread_mutex_unlock(&gl_call_mutex);
        egl_fail(EGL_BAD_ALLOC);
        return EGL_NO_CONTEXT;
    }
    EGLContext ctx = createGLXContext(NULL, contextId, share);
    pthread_mutex_unlock(&gl_call_mutex);
    return ctx;
}

EGLBoolean eglDestroyContext(EGLDisplay dpy, EGLContext ctx)
{
    (void)dpy;
    if (ctx) {
        free(ctx);
    }
    return EGL_TRUE;
}

EGLSurface eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config,
                                  EGLNativeWindowType native_window,
                                  const EGLint *attrib_list)
{
    struct GladioEGLSurface *surf;
    struct gladio_wl_egl_window *win = (struct gladio_wl_egl_window *)native_window;

    (void)config;
    (void)attrib_list;
    if (dpy != GLADIO_EGL_DISPLAY) {
        egl_fail(EGL_BAD_DISPLAY);
        return EGL_NO_SURFACE;
    }
    if (!win) {
        egl_fail(EGL_BAD_NATIVE_WINDOW);
        return EGL_NO_SURFACE;
    }
    surf = calloc(1, sizeof(*surf));
    if (!surf) {
        egl_fail(EGL_BAD_ALLOC);
        return EGL_NO_SURFACE;
    }
    surf->win = win;
    surf->width = win->width > 0 ? win->width : 1;
    surf->height = win->height > 0 ? win->height : 1;
    surf->is_pbuffer = 0;
    return (EGLSurface)surf;
}

EGLSurface eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig config,
                                   const EGLint *attrib_list)
{
    struct GladioEGLSurface *surf;
    int width = 1;
    int height = 1;

    (void)config;
    if (dpy != GLADIO_EGL_DISPLAY) {
        egl_fail(EGL_BAD_DISPLAY);
        return EGL_NO_SURFACE;
    }
    if (attrib_list) {
        for (int i = 0; attrib_list[i] != EGL_NONE; i += 2) {
            if (attrib_list[i] == EGL_WIDTH) {
                width = attrib_list[i + 1];
            } else if (attrib_list[i] == EGL_HEIGHT) {
                height = attrib_list[i + 1];
            }
        }
    }
    if (width <= 0 || height <= 0) {
        egl_fail(EGL_BAD_PARAMETER);
        return EGL_NO_SURFACE;
    }
    surf = calloc(1, sizeof(*surf));
    if (!surf) {
        egl_fail(EGL_BAD_ALLOC);
        return EGL_NO_SURFACE;
    }
    surf->win = NULL;
    surf->width = width;
    surf->height = height;
    surf->is_pbuffer = 1;
    return (EGLSurface)surf;
}

EGLBoolean eglDestroySurface(EGLDisplay dpy, EGLSurface surface)
{
    (void)dpy;
    free(surface);
    return EGL_TRUE;
}

EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read,
                          EGLContext ctx)
{
    struct GladioEGLSurface *surf = draw;
    GLXContext glx = ctx;

    (void)read;
    if (dpy != GLADIO_EGL_DISPLAY && dpy != EGL_NO_DISPLAY) {
        egl_fail(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }
    if (!ctx) {
        destroyGLContextForCallingThread();
        g_egl_current_ctx = EGL_NO_CONTEXT;
        g_egl_current_surf = EGL_NO_SURFACE;
        g_egl_current_dpy = EGL_NO_DISPLAY;
        return EGL_TRUE;
    }
    pthread_mutex_lock(&gl_call_mutex);
    glx->drawable = 0;
    createGLContextForCallingThread();
    if (currentGLContext) {
        currentGLContext->glxContext = glx;
        currentGLContext->clientState = &glx->clientState;
    }
    pthread_mutex_unlock(&gl_call_mutex);
    if (!currentGLContext) {
        egl_fail(EGL_BAD_ALLOC);
        return EGL_FALSE;
    }
    if (surf && !egl_set_window(glx, surf)) {
        egl_fail(EGL_BAD_SURFACE);
        return EGL_FALSE;
    }
    g_egl_current_ctx = ctx;
    g_egl_current_surf = draw;
    g_egl_current_dpy = dpy;
    return EGL_TRUE;
}

EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surface)
{
    struct GladioEGLSurface *surf = surface;
    int drawableId = 0;

    if (dpy != GLADIO_EGL_DISPLAY) {
        egl_fail(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }
    if (!currentGLContext || !surf) {
        egl_fail(EGL_BAD_SURFACE);
        return EGL_FALSE;
    }
    GL_CALL_LOCK();
    if (surf->win && (surf->win->width != surf->width
            || surf->win->height != surf->height)) {
        if (!egl_set_window(currentGLContext->glxContext, surf)) {
            GL_CALL_UNLOCK();
            egl_fail(EGL_BAD_SURFACE);
            return EGL_FALSE;
        }
    }
    GL_SEND_CHECKED(REQUEST_CODE_SWAP_DISPLAY_BUFFERS, &drawableId, sizeof(int),
                    EGL_FALSE);
    GL_RECV_CHECKED(EGL_FALSE);
    GL_CALL_UNLOCK();
    return EGL_TRUE;
}

EGLBoolean eglSwapInterval(EGLDisplay dpy, EGLint interval)
{
    (void)dpy;
    g_egl_interval = interval;
    return EGL_TRUE;
}

EGLBoolean eglQuerySurface(EGLDisplay dpy, EGLSurface surface, EGLint attribute,
                           EGLint *value)
{
    struct GladioEGLSurface *surf = surface;

    (void)dpy;
    if (!surf || !value) {
        egl_fail(EGL_BAD_PARAMETER);
        return EGL_FALSE;
    }
    if (attribute == EGL_WIDTH) {
        *value = surf->width;
        return EGL_TRUE;
    }
    if (attribute == EGL_HEIGHT) {
        *value = surf->height;
        return EGL_TRUE;
    }
    if (attribute == EGL_RENDER_BUFFER) {
        *value = EGL_BACK_BUFFER;
        return EGL_TRUE;
    }
    *value = 0;
    return EGL_TRUE;
}

EGLBoolean eglQueryContext(EGLDisplay dpy, EGLContext ctx, EGLint attribute,
                           EGLint *value)
{
    (void)dpy;
    if (!ctx || !value) {
        egl_fail(EGL_BAD_PARAMETER);
        return EGL_FALSE;
    }
    switch (attribute) {
    case EGL_CONFIG_ID:
        *value = 1;
        break;
    case EGL_CONTEXT_CLIENT_TYPE:
        *value = EGL_OPENGL_ES_API;
        break;
    case EGL_CONTEXT_CLIENT_VERSION:
        *value = 2;
        break;
    case EGL_RENDER_BUFFER:
        *value = g_egl_current_ctx == ctx ? EGL_BACK_BUFFER : EGL_NONE;
        break;
    default:
        egl_fail(EGL_BAD_ATTRIBUTE);
        return EGL_FALSE;
    }
    return EGL_TRUE;
}

EGLint eglGetError(void)
{
    EGLint err = g_egl_error;
    g_egl_error = EGL_SUCCESS;
    return err;
}

EGLContext eglGetCurrentContext(void)
{
    return g_egl_current_ctx;
}

EGLSurface eglGetCurrentSurface(EGLint readdraw)
{
    (void)readdraw;
    return g_egl_current_surf;
}

EGLDisplay eglGetCurrentDisplay(void)
{
    return g_egl_current_dpy;
}

EGLBoolean eglWaitClient(void)
{
    return EGL_TRUE;
}

EGLBoolean eglWaitGL(void)
{
    return EGL_TRUE;
}

EGLBoolean eglWaitNative(EGLint engine)
{
    (void)engine;
    return EGL_TRUE;
}

EGLBoolean eglReleaseThread(void)
{
    return EGL_TRUE;
}

__eglMustCastToProperFunctionPointerType eglGetProcAddress(const char *procname)
{
    if (!procname) {
        return NULL;
    }
    if (strcmp(procname, "eglGetDisplay") == 0) {
        return (__eglMustCastToProperFunctionPointerType)eglGetDisplay;
    }
    if (strcmp(procname, "eglInitialize") == 0) {
        return (__eglMustCastToProperFunctionPointerType)eglInitialize;
    }
    if (strcmp(procname, "eglCreateContext") == 0) {
        return (__eglMustCastToProperFunctionPointerType)eglCreateContext;
    }
    if (strcmp(procname, "eglCreateWindowSurface") == 0) {
        return (__eglMustCastToProperFunctionPointerType)eglCreateWindowSurface;
    }
    if (strcmp(procname, "eglCreatePbufferSurface") == 0) {
        return (__eglMustCastToProperFunctionPointerType)eglCreatePbufferSurface;
    }
    if (strcmp(procname, "eglMakeCurrent") == 0) {
        return (__eglMustCastToProperFunctionPointerType)eglMakeCurrent;
    }
    if (strcmp(procname, "eglSwapBuffers") == 0) {
        return (__eglMustCastToProperFunctionPointerType)eglSwapBuffers;
    }
    if (strcmp(procname, "eglSwapInterval") == 0) {
        return (__eglMustCastToProperFunctionPointerType)eglSwapInterval;
    }
    if (strcmp(procname, "eglQueryContext") == 0) {
        return (__eglMustCastToProperFunctionPointerType)eglQueryContext;
    }
    if (strcmp(procname, "eglGetProcAddress") == 0) {
        return (__eglMustCastToProperFunctionPointerType)eglGetProcAddress;
    }
    return NULL;
}
