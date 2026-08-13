#define _GNU_SOURCE
#include "gladio.h"
#include "gl_context.h"
#include <dlfcn.h>

#define MSG_DEBUG_UNIMPLEMENTED_GLXCALL "gladio: unimplemented call %s"
#define DEFAULT_FBCONFIG_ID 1

static int maxContextId = 1;

#define GLX_CALL_LOCK() pthread_mutex_lock(&gl_call_mutex)
#define GLX_CALL_UNLOCK() pthread_mutex_unlock(&gl_call_mutex)

GLXFBConfig* glXChooseFBConfig(Display* dpy, int screen, const int* attrib_list, int* nelements) {
    int available = 0;
    GLXFBConfig* all = glXGetFBConfigs(dpy, screen, &available);
    if (!all || available <= 0) {
        if (nelements) *nelements = 0;
        return all;
    }
    if (!attrib_list) {
        if (nelements) *nelements = available;
        return all;
    }
    GLXFBConfig* matched = malloc((size_t)available * sizeof(GLXFBConfig));
    int count = 0;
    for (int i = 0; i < available; i++) {
        int ok = 1;
        for (int a = 0; attrib_list[a] != 0 && attrib_list[a] != None; a += 2) {
            int value = 0;
            if (glXGetFBConfigAttrib(dpy, all[i], attrib_list[a], &value) != 0) {
                ok = 0;
                break;
            }
            if (value != attrib_list[a + 1]
                    && !(attrib_list[a] == GLX_RED_SIZE && value >= attrib_list[a + 1])
                    && !(attrib_list[a] == GLX_GREEN_SIZE && value >= attrib_list[a + 1])
                    && !(attrib_list[a] == GLX_BLUE_SIZE && value >= attrib_list[a + 1])
                    && !(attrib_list[a] == GLX_ALPHA_SIZE && value >= attrib_list[a + 1])
                    && !(attrib_list[a] == GLX_DEPTH_SIZE && value >= attrib_list[a + 1])
                    && !(attrib_list[a] == GLX_STENCIL_SIZE && value >= attrib_list[a + 1])
                    && !(attrib_list[a] == GLX_DRAWABLE_TYPE && (value & attrib_list[a + 1]) == attrib_list[a + 1])) {
                ok = 0;
                break;
            }
        }
        if (ok) matched[count++] = all[i];
    }
    free(all);
    if (nelements) *nelements = count;
    if (count == 0) {
        free(matched);
        return NULL;
    }
    return matched;
}

XVisualInfo* glXChooseVisual(Display* dpy, int screen, int* attribList) {
    XVisualInfo visualInfo = {0};
    visualInfo.depth = 32;
    visualInfo.class = TrueColor;

    int count;
    XVisualInfo* visuals = XGetVisualInfo(dpy, VisualDepthMask|VisualClassMask, &visualInfo, &count);
    if (!count) return NULL;

    return visuals;
}

void glXCopyContext(Display* dpy, GLXContext src, GLXContext dst, unsigned long mask) {
    println(MSG_DEBUG_UNIMPLEMENTED_GLXCALL, "glXCopyContext");
}

GLXContext glXCreateContextAttribsARB(Display* dpy, GLXFBConfig config, GLXContext share_context, Bool direct, const int* attrib_list) {
    if (!gladioInitOnce(dpy)) return NULL;
    GLX_CALL_LOCK();
    int contextId = maxContextId++;
    
    ArrayBuffer requestData = {0};
    ArrayBuffer_putInt(&requestData, contextId);
    ArrayBuffer_putInt(&requestData, DEFAULT_FBCONFIG_ID);
    ArrayBuffer_putInt(&requestData, 0);
    ArrayBuffer_putInt(&requestData, share_context ? share_context->id : 0);
    ArrayBuffer_put(&requestData, direct);
    ArrayBuffer_putBytes(&requestData, NULL, 3);
    
    int i = 0;
    int num_attribs = 0;
    if (attrib_list) {
        while (attrib_list[i]) {
            num_attribs++;
            i += 2;
        }
    }
    ArrayBuffer_putInt(&requestData, num_attribs);

    i = 0;
    if (attrib_list) {
        while (attrib_list[i]) {
            ArrayBuffer_putInt(&requestData, attrib_list[i+0]);
            ArrayBuffer_putInt(&requestData, attrib_list[i+1]);
            i += 2;
        }
    }
    
    if (!glx_send(serverFd, GLX_OPCODE_CREATE_CONTEXT_ATTRIBS_ARB, requestData.buffer, requestData.size)) {
        GLX_CALL_UNLOCK();
        return NULL;
    }
    
    ArrayBuffer replyData = {0};
    if (!glx_recv(serverFd, &replyData)) {
        GLX_CALL_UNLOCK();
        return NULL;
    }
    
    GLXContext context = createGLXContext(dpy, contextId, share_context);
    GLX_CALL_UNLOCK();
    return context;
}

GLXContext glXCreateContext(Display* dpy, XVisualInfo* vis, GLXContext shareList, Bool direct) {
    if (!gladioInitOnce(dpy)) return NULL;
    GLX_CALL_LOCK();
    int contextId = maxContextId++;

    ArrayBuffer requestData = {0};
    ArrayBuffer_putInt(&requestData, contextId);
    ArrayBuffer_putInt(&requestData, 0);
    ArrayBuffer_putInt(&requestData, 0);
    ArrayBuffer_putInt(&requestData, shareList ? shareList->id : 0);
    ArrayBuffer_put(&requestData, direct);

    if (!glx_send(serverFd, GLX_OPCODE_CREATE_CONTEXT, requestData.buffer, requestData.size)) {
        GLX_CALL_UNLOCK();
        return NULL;
    }
    
    ArrayBuffer replyData = {0};
    if (!glx_recv(serverFd, &replyData)) {
        GLX_CALL_UNLOCK();
        return NULL;
    }    

    GLXContext context = createGLXContext(dpy, contextId, shareList);
    GLX_CALL_UNLOCK();
    return context;
}

GLXPixmap glXCreateGLXPixmap(Display* dpy, XVisualInfo* visual, Pixmap pixmap) {
    println(MSG_DEBUG_UNIMPLEMENTED_GLXCALL, "glXCreateGLXPixmap");
    return 0;
}

GLXContext glXCreateNewContext(Display* dpy, GLXFBConfig config, int render_type, GLXContext share_list, Bool direct) {
    return glXCreateContextAttribsARB(dpy, config, share_list, direct, NULL);
}

GLXPbuffer glXCreatePbuffer(Display* dpy, GLXFBConfig config, const int* attrib_list) {
    unsigned int width = 1;
    unsigned int height = 1;
    if (attrib_list) {
        for (int i = 0; attrib_list[i] != None; i += 2) {
            if (attrib_list[i] == GLX_PBUFFER_WIDTH)
                width = attrib_list[i + 1];
            else if (attrib_list[i] == GLX_PBUFFER_HEIGHT)
                height = attrib_list[i + 1];
        }
    }
    if (width == 0 || height == 0) return 0;
    return (GLXPbuffer)XCreateSimpleWindow(
            dpy, RootWindow(dpy, DefaultScreen(dpy)), 0, 0,
            width, height, 0, 0, 0);
}

GLXPixmap glXCreatePixmap(Display* dpy, GLXFBConfig config, Pixmap pixmap, const int* attrib_list) {
    println(MSG_DEBUG_UNIMPLEMENTED_GLXCALL, "glXCreatePixmap");
    return 0;
}

GLXWindow glXCreateWindow(Display* dpy, GLXFBConfig config, Window win, const int* attrib_list) {
    return (GLXWindow)win;
}

void glXDestroyContext(Display* dpy, GLXContext ctx) {
    /* GLX 1.3: a NULL ctx is a no-op. Qt xcb-glx / Krita destroy unused
     * QGLXContext wrappers this way. */
    if (!ctx) return;
    (void)dpy;
    GLX_CALL_LOCK();
    glx_send(serverFd, GLX_OPCODE_DESTROY_CONTEXT, &ctx->id, sizeof(int));

    GLClientState_destroy(&ctx->clientState);
    SparseArray_remove(&glxContexts, ctx->id);
    free(ctx);
    GLX_CALL_UNLOCK();
}

void glXDestroyGLXPixmap(Display* dpy, GLXPixmap pixmap) {
    println(MSG_DEBUG_UNIMPLEMENTED_GLXCALL, "glXDestroyGLXPixmap");
}

void glXDestroyPbuffer(Display* dpy, GLXPbuffer pbuf) {
    if (pbuf) XDestroyWindow(dpy, (Window)pbuf);
}

void glXDestroyPixmap(Display* dpy, GLXPixmap pixmap) {
    println(MSG_DEBUG_UNIMPLEMENTED_GLXCALL, "glXDestroyPixmap");
}

void glXDestroyWindow(Display* dpy, GLXWindow win) {
    GLX_CALL_LOCK();
    for (int i = 0; i < glxContexts.size; i++) {
        GLXContext context = glxContexts.entries[i].value;
        if (context->drawable == win) context->drawable = 0;
    }
    GLX_CALL_UNLOCK();
}

const char* glXGetClientString(Display* dpy, int name) {
    return glXQueryServerString(dpy, 0, name);
}

int glXGetConfig(Display* dpy, XVisualInfo* visual, int attrib, int* value) {
    if (!visual || !value) return GLX_BAD_ATTRIBUTE;
    switch (attrib) {
        case GLX_USE_GL:
        case GLX_RGBA:
        case GLX_DOUBLEBUFFER:
        case GLX_X_RENDERABLE:
            *value = True;
            break;
        case GLX_BUFFER_SIZE:
            *value = 32;
            break;
        case GLX_RED_SIZE:
        case GLX_GREEN_SIZE:
        case GLX_BLUE_SIZE:
        case GLX_ALPHA_SIZE:
            *value = 8;
            break;
        case GLX_DEPTH_SIZE:
            *value = 24;
            break;
        case GLX_STENCIL_SIZE:
            *value = 8;
            break;
        case GLX_X_VISUAL_TYPE:
            *value = GLX_TRUE_COLOR;
            break;
        case GLX_CONFIG_CAVEAT:
        case GLX_TRANSPARENT_TYPE:
            *value = GLX_NONE;
            break;
        case GLX_DRAWABLE_TYPE:
            *value = GLX_WINDOW_BIT;
            break;
        case GLX_RENDER_TYPE:
            *value = GLX_RGBA_BIT;
            break;
        case GLX_FBCONFIG_ID:
            *value = DEFAULT_FBCONFIG_ID;
            break;
        case GLX_LEVEL:
        case GLX_STEREO:
        case GLX_AUX_BUFFERS:
        case GLX_ACCUM_RED_SIZE:
        case GLX_ACCUM_GREEN_SIZE:
        case GLX_ACCUM_BLUE_SIZE:
        case GLX_ACCUM_ALPHA_SIZE:
            *value = 0;
            break;
        default:
            return GLX_BAD_ATTRIBUTE;
    }
    return Success;
}

GLXContext glXGetCurrentContext() {
    return currentGLContext ? currentGLContext->glxContext : NULL;
}

Display* glXGetCurrentDisplay() {
    return currentGLContext && currentGLContext->glxContext ? currentGLContext->glxContext->dpy : NULL;
}

GLXDrawable glXGetCurrentDrawable() {
    return currentGLContext && currentGLContext->glxContext ? currentGLContext->glxContext->drawable : 0;
}

GLXDrawable glXGetCurrentReadDrawable() {
    return glXGetCurrentDrawable();
}

int glXGetFBConfigAttrib(Display* dpy, GLXFBConfig config, int attribute, int* value) {
    for (int i = 0; i < config->numProperties; i++) {
        if (config->attributes[i].name == attribute) {
            *value = config->attributes[i].value;
            return 0;
        }
    }
    
    /* Qt's qglx_buildSpec always sends GLX_LEVEL 0. A missing property
     * must not reject the config the way a hard BAD_ATTRIBUTE would. */
    *value = 0;
    return 0;
}

GLXFBConfig* glXGetFBConfigs(Display* dpy, int screen, int* nelements) {
    if (!gladioInitOnce(dpy)) return NULL;
    static struct __GLXFBConfigRec* globalFBConfigs = NULL;
    GLX_CALL_LOCK();
   
    if (!glx_send(serverFd, GLX_OPCODE_GET_FB_CONFIGS, &screen, sizeof(int))) {
        GLX_CALL_UNLOCK();
        return NULL;
    }
    
    ArrayBuffer replyData = {0};
    if (!glx_recv(serverFd, &replyData)) {
        GLX_CALL_UNLOCK();
        return NULL;
    }
    
    int numFBConfigs = ArrayBuffer_getInt(&replyData);
    int numProperties = ArrayBuffer_getInt(&replyData);
    ArrayBuffer_skip(&replyData, 16);
    *nelements = numFBConfigs;
    
    if (!globalFBConfigs) globalFBConfigs = calloc(numFBConfigs, sizeof(struct __GLXFBConfigRec));
    GLXFBConfig* fbConfigs = malloc(numFBConfigs * sizeof(GLXFBConfig));

    for (int i = 0, j; i < numFBConfigs; i++) {
        fbConfigs[i] = &globalFBConfigs[i];
        fbConfigs[i]->numProperties = numProperties;
        for (j = 0; j < numProperties; j++) {
            fbConfigs[i]->attributes[j].name = ArrayBuffer_getInt(&replyData);
            fbConfigs[i]->attributes[j].value = ArrayBuffer_getInt(&replyData);
        }        
    }

    GLX_CALL_UNLOCK();
    return fbConfigs;
}

__GLXextFuncPtr glXGetProcAddressARB(const GLubyte* procName) {
    const char* name = (const char*)procName;
    if (strcmp(name, "glXChooseFBConfig") == 0) return (__GLXextFuncPtr)glXChooseFBConfig;
    else if (strcmp(name, "glXChooseVisual") == 0) return (__GLXextFuncPtr)glXChooseVisual;
    else if (strcmp(name, "glXCopyContext") == 0) return (__GLXextFuncPtr)glXCopyContext;
    else if (strcmp(name, "glXCreateContextAttribsARB") == 0) return (__GLXextFuncPtr)glXCreateContextAttribsARB;
    else if (strcmp(name, "glXCreateContext") == 0) return (__GLXextFuncPtr)glXCreateContext;
    else if (strcmp(name, "glXCreateGLXPixmap") == 0) return (__GLXextFuncPtr)glXCreateGLXPixmap;
    else if (strcmp(name, "glXCreateNewContext") == 0) return (__GLXextFuncPtr)glXCreateNewContext;
    else if (strcmp(name, "glXCreatePbuffer") == 0) return (__GLXextFuncPtr)glXCreatePbuffer;
    else if (strcmp(name, "glXCreatePixmap") == 0) return (__GLXextFuncPtr)glXCreatePixmap;
    else if (strcmp(name, "glXCreateWindow") == 0) return (__GLXextFuncPtr)glXCreateWindow;
    else if (strcmp(name, "glXDestroyContext") == 0) return (__GLXextFuncPtr)glXDestroyContext;
    else if (strcmp(name, "glXDestroyGLXPixmap") == 0) return (__GLXextFuncPtr)glXDestroyGLXPixmap;
    else if (strcmp(name, "glXDestroyPbuffer") == 0) return (__GLXextFuncPtr)glXDestroyPbuffer;
    else if (strcmp(name, "glXDestroyPixmap") == 0) return (__GLXextFuncPtr)glXDestroyPixmap;
    else if (strcmp(name, "glXDestroyWindow") == 0) return (__GLXextFuncPtr)glXDestroyWindow;
    else if (strcmp(name, "glXGetClientString") == 0) return (__GLXextFuncPtr)glXGetClientString;
    else if (strcmp(name, "glXGetConfig") == 0) return (__GLXextFuncPtr)glXGetConfig;
    else if (strcmp(name, "glXGetCurrentContext") == 0) return (__GLXextFuncPtr)glXGetCurrentContext;
    else if (strcmp(name, "glXGetCurrentDisplay") == 0) return (__GLXextFuncPtr)glXGetCurrentDisplay;
    else if (strcmp(name, "glXGetCurrentDrawable") == 0) return (__GLXextFuncPtr)glXGetCurrentDrawable;
    else if (strcmp(name, "glXGetCurrentReadDrawable") == 0) return (__GLXextFuncPtr)glXGetCurrentReadDrawable;
    else if (strcmp(name, "glXGetFBConfigAttrib") == 0) return (__GLXextFuncPtr)glXGetFBConfigAttrib;
    else if (strcmp(name, "glXGetFBConfigs") == 0) return (__GLXextFuncPtr)glXGetFBConfigs;
    else if (strcmp(name, "glXGetSelectedEvent") == 0) return (__GLXextFuncPtr)glXGetSelectedEvent;
    else if (strcmp(name, "glXGetVisualFromFBConfig") == 0) return (__GLXextFuncPtr)glXGetVisualFromFBConfig;
    else if (strcmp(name, "glXIsDirect") == 0) return (__GLXextFuncPtr)glXIsDirect;
    else if (strcmp(name, "glXMakeContextCurrent") == 0) return (__GLXextFuncPtr)glXMakeContextCurrent;
    else if (strcmp(name, "glXMakeCurrent") == 0) return (__GLXextFuncPtr)glXMakeCurrent;
    else if (strcmp(name, "glXQueryContext") == 0) return (__GLXextFuncPtr)glXQueryContext;
    else if (strcmp(name, "glXQueryDrawable") == 0) return (__GLXextFuncPtr)glXQueryDrawable;
    else if (strcmp(name, "glXQueryExtension") == 0) return (__GLXextFuncPtr)glXQueryExtension;
    else if (strcmp(name, "glXQueryExtensionsString") == 0) return (__GLXextFuncPtr)glXQueryExtensionsString;
    else if (strcmp(name, "glXQueryServerString") == 0) return (__GLXextFuncPtr)glXQueryServerString;
    else if (strcmp(name, "glXQueryVersion") == 0) return (__GLXextFuncPtr)glXQueryVersion;
    else if (strcmp(name, "glXSelectEvent") == 0) return (__GLXextFuncPtr)glXSelectEvent;
    else if (strcmp(name, "glXSwapBuffers") == 0) return (__GLXextFuncPtr)glXSwapBuffers;
    else if (strcmp(name, "glXUseXFont") == 0) return (__GLXextFuncPtr)glXUseXFont;
    else if (strcmp(name, "glXWaitGL") == 0) return (__GLXextFuncPtr)glXWaitGL;
    else if (strcmp(name, "glXWaitX") == 0) return (__GLXextFuncPtr)glXWaitX;
    /* GLX loaders also use this API for ordinary OpenGL entry points. All
     * Gladio gl* calls are exported by this DSO. Chrome loads libGL locally,
     * so RTLD_DEFAULT alone cannot reliably see those exports. */
    if (strncmp(name, "gl", 2) == 0) {
        static void* selfHandle = NULL;
        if (!selfHandle) {
            Dl_info info;
            if (dladdr((void*)glXGetProcAddressARB, &info) && info.dli_fname)
                selfHandle = dlopen(info.dli_fname, RTLD_LAZY | RTLD_NOLOAD);
        }
        return (__GLXextFuncPtr)dlsym(
                selfHandle ? selfHandle : RTLD_DEFAULT, name);
    }
    return NULL;
}

__GLXextFuncPtr glXGetProcAddress(const GLubyte* procName) {
    return glXGetProcAddressARB(procName);
}

void glXGetSelectedEvent(Display* dpy, GLXDrawable draw, unsigned long* event_mask) {
    println(MSG_DEBUG_UNIMPLEMENTED_GLXCALL, "glXGetSelectedEvent");
}

XVisualInfo* glXGetVisualFromFBConfig(Display* dpy, GLXFBConfig config) {
    XVisualInfo visualInfo = {0};
    visualInfo.depth = 32;
    visualInfo.class = TrueColor;
    
    int count;
    XVisualInfo *visuals = XGetVisualInfo(dpy, VisualDepthMask|VisualClassMask, &visualInfo, &count);
    if (!count) return NULL;
    
    return visuals;
}

Bool glXIsDirect(Display* dpy, GLXContext ctx) {
    return true;
}

Bool glXMakeContextCurrent(Display* dpy, GLXDrawable draw, GLXDrawable read, GLXContext ctx) {
    return glXMakeCurrent(dpy, draw, ctx);
}

Bool glXMakeCurrent(Display* dpy, GLXDrawable drawable, GLXContext ctx) {
    GLX_CALL_LOCK();
    if (ctx) {
        ctx->drawable = drawable;
        createGLContextForCallingThread();
        if (currentGLContext) {
            currentGLContext->glxContext = ctx;
            currentGLContext->clientState = &ctx->clientState;
        }
    }
    else {
        destroyGLContextForCallingThread();
        GLX_CALL_UNLOCK();
        return true;
    }

    ArrayBuffer requestData = {0};
    ArrayBuffer_putInt(&requestData, (int)drawable);
    ArrayBuffer_putInt(&requestData, ctx->id);
    GL_SEND_CHECKED(REQUEST_CODE_SET_CURRENT_RENDER_WINDOW, requestData.buffer, requestData.size, GL_RETURN);
    GL_RECV_CHECKED(GL_RETURN);
    GLX_CALL_UNLOCK();
    return true;
}

int glXQueryContext(Display* dpy, GLXContext ctx, int attribute, int* value) {
    println(MSG_DEBUG_UNIMPLEMENTED_GLXCALL, "glXQueryContext");
    return 0;
}

void glXQueryDrawable(Display* dpy, GLXDrawable draw, int attribute, unsigned int* value) {
    if (!value) return;
    Window root;
    int x, y;
    unsigned int width, height, border, depth;
    if (!XGetGeometry(dpy, (Drawable)draw, &root, &x, &y, &width,
                      &height, &border, &depth)) {
        *value = 0;
    }
    else if (attribute == GLX_WIDTH) {
        *value = width;
    }
    else if (attribute == GLX_HEIGHT) {
        *value = height;
    }
    else {
        *value = 0;
    }
}

Bool glXQueryExtension(Display* dpy, int* errorb, int* event) {
    int majorOpcode;
    return XQueryExtension(dpy, "GLX", &majorOpcode, event, errorb);
}

const char* glXQueryExtensionsString(Display* dpy, int screen) {
    if (!gladioInitOnce(dpy)) return NULL;
    GLX_CALL_LOCK();
    char* cachedString = getCachedString(GLX_EXTENSIONS);
    if (cachedString) {
        GLX_CALL_UNLOCK();
        return cachedString;
    }
    
    if (!glx_send(serverFd, GLX_OPCODE_QUERY_EXTENSIONS_STRING, &screen, sizeof(int))) {
        GLX_CALL_UNLOCK();
        return false;
    }
    
    ArrayBuffer replyData = {0};
    if (!glx_recv(serverFd, &replyData)) {
        GLX_CALL_UNLOCK();
        return false;
    }
    
    ArrayBuffer_skip(&replyData, 4);
    int length = ArrayBuffer_getInt(&replyData);
    ArrayBuffer_skip(&replyData, 16);
    
    char* string = ArrayBuffer_getBytes(&replyData, length);
    char* result = putCachedString(GLX_EXTENSIONS, string, length);
    GLX_CALL_UNLOCK();
    return result;
}

const char* glXQueryServerString(Display* dpy, int screen, int name) {
    if (!gladioInitOnce(dpy)) return NULL;
    GLX_CALL_LOCK();
    char* cachedString = getCachedString(name);
    if (cachedString) {
        GLX_CALL_UNLOCK();
        return cachedString;
    }
    
    ArrayBuffer requestData = {0};
    ArrayBuffer_putInt(&requestData, screen);
    ArrayBuffer_putInt(&requestData, name);
    
    if (!glx_send(serverFd, GLX_OPCODE_QUERY_SERVER_STRING, requestData.buffer, requestData.size)) {
        GLX_CALL_UNLOCK();
        return false;
    }
    
    ArrayBuffer replyData = {0};
    if (!glx_recv(serverFd, &replyData)) {
        GLX_CALL_UNLOCK();
        return false;
    }
    
    ArrayBuffer_skip(&replyData, 4);
    int length = ArrayBuffer_getInt(&replyData);
    ArrayBuffer_skip(&replyData, 16);
    
    char* string = ArrayBuffer_getBytes(&replyData, length);
    char* result = putCachedString(name, string, length);
    GLX_CALL_UNLOCK();
    return result;
}

Bool glXQueryVersion(Display* dpy, int* maj, int* min) {
    if (!gladioInitOnce(dpy)) return false;
    GLX_CALL_LOCK();
    ArrayBuffer requestData = {0};
    ArrayBuffer_putInt(&requestData, *maj);
    ArrayBuffer_putInt(&requestData, *min);
    
    if (!glx_send(serverFd, GLX_OPCODE_QUERY_VERSION, requestData.buffer, requestData.size)) {
        GLX_CALL_UNLOCK();
        return false;
    }
    
    ArrayBuffer replyData = {0};
    if (!glx_recv(serverFd, &replyData)) {
        GLX_CALL_UNLOCK();
        return false;
    }
    
    *maj = ArrayBuffer_getInt(&replyData);
    *min = ArrayBuffer_getInt(&replyData);
    GLX_CALL_UNLOCK();
    return true;
}

void glXSelectEvent(Display* dpy, GLXDrawable draw, unsigned long event_mask) {
    println(MSG_DEBUG_UNIMPLEMENTED_GLXCALL, "glXSelectEvent");
}

void glXSwapBuffers(Display* dpy, GLXDrawable drawable) {
    GL_CALL_LOCK();
    int drawableId = (int)drawable;
    GL_SEND_CHECKED(REQUEST_CODE_SWAP_DISPLAY_BUFFERS, &drawableId, sizeof(int));
    GL_RECV_CHECKED();
    GL_CALL_UNLOCK();
}

void glXUseXFont(Font font, int first, int count, int list) {
    println(MSG_DEBUG_UNIMPLEMENTED_GLXCALL, "glXUseXFont");
}

void glXWaitGL() {
    println(MSG_DEBUG_UNIMPLEMENTED_GLXCALL, "glXWaitGL");
}

void glXWaitX() {
    println(MSG_DEBUG_UNIMPLEMENTED_GLXCALL, "glXWaitX");
}

