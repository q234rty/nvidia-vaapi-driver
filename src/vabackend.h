#ifndef VABACKEND_H
#define VABACKEND_H

#include "direct/nv-driver.h"

#include <ffnvcodec/dynlink_loader.h>
#include <va/va_backend.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <stdbool.h>
#include <va/va_drmcommon.h>

#include <pthread.h>
#include "list.h"


#define SURFACE_QUEUE_SIZE 16
#define MAX_IMAGE_COUNT 64

typedef struct {
    void        *buf;
    uint64_t    size;
    uint64_t    allocated;
} AppendableBuffer;

typedef enum
{
    OBJECT_TYPE_CONFIG,
    OBJECT_TYPE_CONTEXT,
    OBJECT_TYPE_SURFACE,
    OBJECT_TYPE_BUFFER,
    OBJECT_TYPE_IMAGE
} ObjectType;

typedef struct Object_t
{
    ObjectType      type;
    VAGenericID     id;
    void            *obj;
} *Object;

typedef struct
{
    int             elements;
    int             size;
    VABufferType    bufferType;
    void            *ptr;
    int             offset;
} NVBuffer;

struct _NVContext;
struct _BackingImage;

typedef struct
{
    uint32_t                width;
    uint32_t                height;
    cudaVideoSurfaceFormat  format;
    int                     bitDepth;
    int                     pictureIdx;
    struct _NVContext       *context;
    int                     progressiveFrame;
    int                     topFieldFirst;
    int                     secondField;
    int                     order_hint; //needed for AV1
    struct _BackingImage    *backingImage;
    CUevent                 event;
    int                     resolving;
    pthread_mutex_t         mutex;
    pthread_cond_t          cond;
    CUdeviceptr             rawImageCopy;
} NVSurface;

typedef struct
{
    int         width;
    int         height;
    uint32_t    format;
    NVBuffer    *imageBuffer;
} NVImage;

typedef struct {
    CUexternalMemory extMem;
    CUmipmappedArray mipmapArray;
} NVCudaImage;

typedef struct _BackingImage {
    NVSurface   *surface;
    EGLImage    image;
    CUarray     arrays[2];
    uint32_t    width;
    uint32_t    height;
    int         fourcc;
    int         fds[4];
    int         offsets[4];
    int         strides[4];
    uint64_t    mods[4];
    uint32_t    size[4];
    //direct backend only
    NVCudaImage cudaImages[2];
} BackingImage;

struct _NVDriver;

typedef struct {
    const char *name;
    bool (*initExporter)(struct _NVDriver *drv);
    void (*releaseExporter)(struct _NVDriver *drv);
    bool (*exportCudaPtr)(struct _NVDriver *drv, CUdeviceptr ptr, NVSurface *surface, uint32_t pitch);
    void (*detachBackingImageFromSurface)(struct _NVDriver *drv, NVSurface *surface);
    bool (*realiseSurface)(struct _NVDriver *drv, NVSurface *surface);
    bool (*fillExportDescriptor)(struct _NVDriver *drv, NVSurface *surface, VADRMPRIMESurfaceDescriptor *desc);
    void (*destroyAllBackingImage)(struct _NVDriver *drv);
} NVBackend;

typedef struct _NVDriver
{
    CudaFunctions           *cu;
    CuvidFunctions          *cv;
    CUcontext               cudaContext;
    Array/*<Object>*/       objects;
    pthread_mutex_t         objectCreationMutex;
    VAGenericID             nextObjId;
    bool                    useCorrectNV12Format;
    bool                    supports16BitSurface;
    int                     cudaGpuId;
    int                     drmFd;
    int                     surfaceCount;
    pthread_mutex_t         exportMutex;
    pthread_mutex_t         imagesMutex;
    Array/*<NVEGLImage>*/   images;
    const NVBackend         *backend;
    //fields for direct backend
    NVDriverContext         driverContext;
    //fields for egl backend
    EGLDeviceEXT            eglDevice;
    EGLDisplay              eglDisplay;
    EGLContext              eglContext;
    EGLStreamKHR            eglStream;
    CUeglStreamConnection   cuStreamConnection;
    int                     numFramesPresented;
    //fields for YUVtoRGB module
    CUmodule                yuvModule;
    CUfunction              yuvFunction;
    void                    *xcbConnection;
} NVDriver;

struct _NVCodec;

typedef struct _NVContext
{
    NVDriver            *drv;
    VAProfile           profile;
    VAEntrypoint        entrypoint;
    int                 width;
    int                 height;
    CUvideodecoder      decoder;
    NVSurface           *renderTargets;
    void                *lastSliceParams;
    unsigned int        lastSliceParamsCount;
    AppendableBuffer    buf;
    AppendableBuffer    sliceOffsets;
    CUVIDPICPARAMS      pPicParams;
    const struct _NVCodec *codec;
    int                 currentPictureId;
    pthread_t           resolveThread;
    pthread_mutex_t     resolveMutex;
    pthread_cond_t      resolveCondition;
    NVSurface*          surfaceQueue[SURFACE_QUEUE_SIZE];
    int                 surfaceQueueReadIdx;
    int                 surfaceQueueWriteIdx;
    bool                exiting;
    pthread_mutex_t     surfaceCreationMutex;
    bool                copyMode;
} NVContext;

typedef struct
{
    VAProfile               profile;
    VAEntrypoint            entrypoint;
    cudaVideoSurfaceFormat  surfaceFormat;
    cudaVideoChromaFormat   chromaFormat;
    int                     bitDepth;
    cudaVideoCodec          cudaCodec;
} NVConfig;

typedef void (*HandlerFunc)(NVContext*, NVBuffer* , CUVIDPICPARAMS*);
typedef cudaVideoCodec (*ComputeCudaCodec)(VAProfile);

//padding/alignment is very important to this structure as it's placed in it's own section
//in the executable.
struct _NVCodec {
    ComputeCudaCodec    computeCudaCodec;
    HandlerFunc         handlers[VABufferTypeMax];
    VAEntrypoint        entrypoint;
    int                 supportedProfileCount;
    const VAProfile     *supportedProfiles;
};

typedef struct _NVCodec NVCodec;

void* getObjectPtr(NVDriver *drv, VAGenericID id);
void appendBuffer(AppendableBuffer *ab, const void *buf, uint64_t size);
int pictureIdxFromSurfaceId(NVDriver *ctx, VASurfaceID surf);
NVSurface* nvSurfaceFromSurfaceId(NVDriver *drv, VASurfaceID surf);
void checkCudaErrors(CUresult err, const char *file, const char *function, const int line);
void logger(const char *filename, const char *function, int line, const char *msg, ...);
#define CHECK_CUDA_RESULT(err) checkCudaErrors(err, __FILE__, __func__, __LINE__)
#define cudaVideoCodec_NONE ((cudaVideoCodec) -1)
#define LOG(...) logger(__FILE__, __func__, __LINE__, __VA_ARGS__);
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define PTROFF(base, bytes) ((void *)((unsigned char *)(base) + (bytes)))
#define DECLARE_CODEC(name) \
    __attribute__((used)) \
    __attribute__((retain)) \
    __attribute__((section("nvd_codecs"))) \
    __attribute__((aligned(__alignof__(NVCodec)))) \
    NVCodec name

#endif // VABACKEND_H
