/*
 * Copyright 2020 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef GrDirectContext_DEFINED
#define GrDirectContext_DEFINED

#include <set>
#include <unordered_map>

#include "src/gpu/ganesh/GrGpuResource.h"

#include "include/core/SkColor.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkTypes.h"
#include "include/gpu/GpuTypes.h"
#include "include/gpu/ganesh/GrContextOptions.h"
#include "include/gpu/ganesh/GrRecordingContext.h"
#include "include/gpu/ganesh/GrTypes.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

class GrAtlasManager;
class GrBackendSemaphore;
class GrBackendFormat;
class GrBackendTexture;
class GrBackendRenderTarget;
class GrClientMappedBufferManager;
class GrContextThreadSafeProxy;
class GrDirectContextPriv;
class GrGpu;
class GrResourceCache;
class GrResourceProvider;
class SkData;
class SkImage;
class SkPixmap;
class SkSurface;
class SkTaskGroup;
class SkTraceMemoryDump;
enum SkColorType : int;
enum class SkTextureCompressionType;
struct GrMockOptions;
struct GrD3DBackendContext; // IWYU pragma: keep

// OH ISSUE: callback for memory protect.
using MemoryOverflowCalllback = std::function<void(int32_t, size_t, bool)>;

namespace skgpu {
    class MutableTextureState;
#if !defined(SK_ENABLE_OPTIMIZE_SIZE)
    namespace ganesh { class SmallPathAtlasMgr; }
#endif
}
namespace sktext { namespace gpu { class StrikeCache; } }
namespace wgpu { class Device; } // IWYU pragma: keep

namespace SkSurfaces {
enum class BackendSurfaceAccess;
}

class SK_API GrDirectContext : public GrRecordingContext {
public:
#ifdef SK_DIRECT3D
    /**
     * Makes a GrDirectContext which uses Direct3D as the backend. The Direct3D context
     * must be kept alive until the returned GrDirectContext is first destroyed or abandoned.
     */
    static sk_sp<GrDirectContext> MakeDirect3D(const GrD3DBackendContext&, const GrContextOptions&);
    static sk_sp<GrDirectContext> MakeDirect3D(const GrD3DBackendContext&);
#endif

    static sk_sp<GrDirectContext> MakeMock(const GrMockOptions*, const GrContextOptions&);
    static sk_sp<GrDirectContext> MakeMock(const GrMockOptions*);

    ~GrDirectContext() override;

    /**
     * The context normally assumes that no outsider is setting state
     * within the underlying 3D API's context/device/whatever. This call informs
     * the context that the state was modified and it should resend. Shouldn't
     * be called frequently for good performance.
     * The flag bits, state, is dependent on which backend is used by the
     * context, either GL or D3D (possible in future).
     */
    void resetContext(uint32_t state = kAll_GrBackendState);

    /**
     * If the backend is GrBackendApi::kOpenGL, then all texture unit/target combinations for which
     * the context has modified the bound texture will have texture id 0 bound. This does not
     * flush the context. Calling resetContext() does not change the set that will be bound
     * to texture id 0 on the next call to resetGLTextureBindings(). After this is called
     * all unit/target combinations are considered to have unmodified bindings until the context
     * subsequently modifies them (meaning if this is called twice in a row with no intervening
     * context usage then the second call is a no-op.)
     */
    void resetGLTextureBindings();

    /**
     * Abandons all GPU resources and assumes the underlying backend 3D API context is no longer
     * usable. Call this if you have lost the associated GPU context, and thus internal texture,
     * buffer, etc. references/IDs are now invalid. Calling this ensures that the destructors of the
     * context and any of its created resource objects will not make backend 3D API calls. Content
     * rendered but not previously flushed may be lost. After this function is called all subsequent
     * calls on the context will fail or be no-ops.
     *
     * The typical use case for this function is that the underlying 3D context was lost and further
     * API calls may crash.
     *
     * This call is not valid to be made inside ReleaseProcs passed into SkSurface or SkImages. The
     * call will simply fail (and assert in debug) if it is called while inside a ReleaseProc.
     *
     * For Vulkan, even if the device becomes lost, the VkQueue, VkDevice, or VkInstance used to
     * create the context must be kept alive even after abandoning the context. Those objects must
     * live for the lifetime of the context object itself. The reason for this is so that
     * we can continue to delete any outstanding GrBackendTextures/RenderTargets which must be
     * cleaned up even in a device lost state.
     */
    void abandonContext() override;

    /**
     * Returns true if the context was abandoned or if the backend specific context has gotten into
     * an unrecoverarble, lost state (e.g. in Vulkan backend if we've gotten a
     * VK_ERROR_DEVICE_LOST). If the backend context is lost, this call will also abandon this
     * context.
     */
    bool abandoned() override;

    /**
     * Returns true if the backend specific context has gotten into an unrecoverarble, lost state
     * (e.g. in Vulkan backend if we've gotten a VK_ERROR_DEVICE_LOST). If the backend context is
     * lost, this call will also abandon this context.
     */
    bool isDeviceLost();

    // TODO: Remove this from public after migrating Chrome.
    sk_sp<GrContextThreadSafeProxy> threadSafeProxy();

    /**
     * Checks if the underlying 3D API reported an out-of-memory error. If this returns true it is
     * reset and will return false until another out-of-memory error is reported by the 3D API. If
     * the context is abandoned then this will report false.
     *
     * Currently this is implemented for:
     *
     * OpenGL [ES] - Note that client calls to glGetError() may swallow GL_OUT_OF_MEMORY errors and
     * therefore hide the error from Skia. Also, it is not advised to use this in combination with
     * enabling GrContextOptions::fSkipGLErrorChecks. That option may prevent the context from ever
     * checking the GL context for OOM.
     *
     * Vulkan - Reports true if VK_ERROR_OUT_OF_HOST_MEMORY or VK_ERROR_OUT_OF_DEVICE_MEMORY has
     * occurred.
     */
    bool oomed();

    /**
     * This is similar to abandonContext() however the underlying 3D context is not yet lost and
     * the context will cleanup all allocated resources before returning. After returning it will
     * assume that the underlying context may no longer be valid.
     *
     * The typical use case for this function is that the client is going to destroy the 3D context
     * but can't guarantee that context will be destroyed first (perhaps because it may be ref'ed
     * elsewhere by either the client or Skia objects).
     *
     * For Vulkan, even if the device becomes lost, the VkQueue, VkDevice, or VkInstance used to
     * create the context must be alive before calling releaseResourcesAndAbandonContext.
     */
    void releaseResourcesAndAbandonContext();

    ///////////////////////////////////////////////////////////////////////////
    // Resource Cache

    /** DEPRECATED
     *  Return the current GPU resource cache limits.
     *
     *  @param maxResources If non-null, will be set to -1.
     *  @param maxResourceBytes If non-null, returns maximum number of bytes of
     *                          video memory that can be held in the cache.
     */
    void getResourceCacheLimits(int* maxResources, size_t* maxResourceBytes) const;

    /**
     *  Return the current GPU resource cache limit in bytes.
     */
    size_t getResourceCacheLimit() const;

    /**
     *  Gets the current GPU resource cache usage.
     *
     *  @param resourceCount If non-null, returns the number of resources that are held in the
     *                       cache.
     *  @param maxResourceBytes If non-null, returns the total number of bytes of video memory held
     *                          in the cache.
     */
    void getResourceCacheUsage(int* resourceCount, size_t* resourceBytes) const;

    /**
     *  Gets the number of bytes in the cache consumed by purgeable (e.g. unlocked) resources.
     */
    size_t getResourceCachePurgeableBytes() const;

    /** DEPRECATED
     *  Specify the GPU resource cache limits. If the current cache exceeds the maxResourceBytes
     *  limit, it will be purged (LRU) to keep the cache within the limit.
     *
     *  @param maxResources Unused.
     *  @param maxResourceBytes The maximum number of bytes of video memory
     *                          that can be held in the cache.
     */
    void setResourceCacheLimits(int maxResources, size_t maxResourceBytes);

    /**
     *  Specify the GPU resource cache limit. If the cache currently exceeds this limit,
     *  it will be purged (LRU) to keep the cache within the limit.
     *
     *  @param maxResourceBytes The maximum number of bytes of video memory
     *                          that can be held in the cache.
     */
    void setResourceCacheLimit(size_t maxResourceBytes);

    /**
     * Frees GPU created by the context. Can be called to reduce GPU memory
     * pressure.
     */
    void freeGpuResources();

    /**
     * Purge GPU resources that haven't been used in the past 'msNotUsed' milliseconds or are
     * otherwise marked for deletion, regardless of whether the context is under budget.

     *
     * @param msNotUsed   Only unlocked resources not used in these last milliseconds will be
     *                    cleaned up.
     * @param opts        Specify which resources should be cleaned up. If kScratchResourcesOnly
     *                    then, all unlocked scratch resources older than 'msNotUsed' will be purged
     *                    but the unlocked resources with persistent data will remain. If
     *                    kAllResources
     */

    void performDeferredCleanup(
            std::chrono::milliseconds msNotUsed,
            GrPurgeResourceOptions opts = GrPurgeResourceOptions::kAllResources);

    // Temporary compatibility API for Android.
    void purgeResourcesNotUsedInMs(std::chrono::milliseconds msNotUsed) {
        this->performDeferredCleanup(msNotUsed);
    }

    /**
     * Purge unlocked resources from the cache until the the provided byte count has been reached
     * or we have purged all unlocked resources. The default policy is to purge in LRU order, but
     * can be overridden to prefer purging scratch resources (in LRU order) prior to purging other
     * resource types.
     *
     * @param maxBytesToPurge the desired number of bytes to be purged.
     * @param preferScratchResources If true scratch resources will be purged prior to other
     *                               resource types.
     */
    void purgeUnlockedResources(size_t bytesToPurge, bool preferScratchResources);
    void purgeUnlockedResourcesByTag(bool scratchResourcesOnly, const GrGpuResourceTag& tag);
    void purgeUnlockedResourcesByPid(bool scratchResourcesOnly, const std::set<int>& exitedPidSet);

    /**
     * This entry point is intended for instances where an app has been backgrounded or
     * suspended.
     * If 'scratchResourcesOnly' is true all unlocked scratch resources will be purged but the
     * unlocked resources with persistent data will remain. If 'scratchResourcesOnly' is false
     * then all unlocked resources will be purged.
     * In either case, after the unlocked resources are purged a separate pass will be made to
     * ensure that resource usage is under budget (i.e., even if 'scratchResourcesOnly' is true
     * some resources with persistent data may be purged to be under budget).
     *
     * @param opts If kScratchResourcesOnly only unlocked scratch resources will be purged prior
     *             enforcing the budget requirements.
     */
    void purgeUnlockedResources(GrPurgeResourceOptions opts);

    /*
     * Gets the types of GPU stats supported by this Context.
     */
    skgpu::GpuStatsFlags supportedGpuStats() const;

    /**
     * Gets the maximum supported texture size.
     */
    using GrRecordingContext::maxTextureSize;

    /**
     * Gets the maximum supported render target size.
     */
    using GrRecordingContext::maxRenderTargetSize;

    /**
     * Can a SkImage be created with the given color type.
     */
    using GrRecordingContext::colorTypeSupportedAsImage;

    /**
     * Does this context support protected content?
     */
    using GrRecordingContext::supportsProtectedContent;

    /**
     * Can a SkSurface be created with the given color type. To check whether MSAA is supported
     * use maxSurfaceSampleCountForColorType().
     */
    using GrRecordingContext::colorTypeSupportedAsSurface;

    /**
     * Gets the maximum supported sample count for a color type. 1 is returned if only non-MSAA
     * rendering is supported for the color type. 0 is returned if rendering to this color type
     * is not supported at all.
     */
    using GrRecordingContext::maxSurfaceSampleCountForColorType;

    ///////////////////////////////////////////////////////////////////////////
    // Misc.

    /**
     * Inserts a list of GPU semaphores that the current GPU-backed API must wait on before
     * executing any more commands on the GPU. We only guarantee blocking transfer and fragment
     * shader work, but may block earlier stages as well depending on the backend.If this call
     * returns false, then the GPU back-end will not wait on any passed in semaphores, and the
     * client will still own the semaphores, regardless of the value of deleteSemaphoresAfterWait.
     *
     * If deleteSemaphoresAfterWait is false then Skia will not delete the semaphores. In this case
     * it is the client's responsibility to not destroy or attempt to reuse the semaphores until it
     * knows that Skia has finished waiting on them. This can be done by using finishedProcs on
     * flush calls.
     *
     * This is not supported on the GL backend.
     */
    bool wait(int numSemaphores, const GrBackendSemaphore* waitSemaphores,
              bool deleteSemaphoresAfterWait = true);

    /**
     * Call to ensure all drawing to the context has been flushed and submitted to the underlying 3D
     * API. This is equivalent to calling GrContext::flush with a default GrFlushInfo followed by
     * GrContext::submit(sync).
     */
    void flushAndSubmit(GrSyncCpu sync = GrSyncCpu::kNo) {
        this->flush(GrFlushInfo());
        this->submit(sync);
    }

    /**
     * Call to ensure all drawing to the context has been flushed to underlying 3D API specific
     * objects. A call to `submit` is always required to ensure work is actually sent to
     * the gpu. Some specific API details:
     *     GL: Commands are actually sent to the driver, but glFlush is never called. Thus some
     *         sync objects from the flush will not be valid until a submission occurs.
     *
     *     Vulkan/Metal/D3D/Dawn: Commands are recorded to the backend APIs corresponding command
     *         buffer or encoder objects. However, these objects are not sent to the gpu until a
     *         submission occurs.
     *
     * If the return is GrSemaphoresSubmitted::kYes, only initialized GrBackendSemaphores will be
     * submitted to the gpu during the next submit call (it is possible Skia failed to create a
     * subset of the semaphores). The client should not wait on these semaphores until after submit
     * has been called, and must keep them alive until then. If this call returns
     * GrSemaphoresSubmitted::kNo, the GPU backend will not submit any semaphores to be signaled on
     * the GPU. Thus the client should not have the GPU wait on any of the semaphores passed in with
     * the GrFlushInfo. Regardless of whether semaphores were submitted to the GPU or not, the
     * client is still responsible for deleting any initialized semaphores.
     * Regardless of semaphore submission the context will still be flushed. It should be
     * emphasized that a return value of GrSemaphoresSubmitted::kNo does not mean the flush did not
     * happen. It simply means there were no semaphores submitted to the GPU. A caller should only
     * take this as a failure if they passed in semaphores to be submitted.
     */
    GrSemaphoresSubmitted flush(const GrFlushInfo& info);

    void flush() { this->flush(GrFlushInfo()); }

    /** Flushes any pending uses of texture-backed images in the GPU backend. If the image is not
     *  texture-backed (including promise texture images) or if the GrDirectContext does not
     *  have the same context ID as the context backing the image then this is a no-op.
     *  If the image was not used in any non-culled draws in the current queue of work for the
     *  passed GrDirectContext then this is a no-op unless the GrFlushInfo contains semaphores or
     *  a finish proc. Those are respected even when the image has not been used.
     *  @param image    the non-null image to flush.
     *  @param info     flush options
     */
    GrSemaphoresSubmitted flush(const sk_sp<const SkImage>& image, const GrFlushInfo& info);
    void flush(const sk_sp<const SkImage>& image);

    /** Version of flush() that uses a default GrFlushInfo. Also submits the flushed work to the
     *   GPU.
     */
    void flushAndSubmit(const sk_sp<const SkImage>& image);

    /** Issues pending SkSurface commands to the GPU-backed API objects and resolves any SkSurface
     *  MSAA. A call to GrDirectContext::submit is always required to ensure work is actually sent
     *  to the gpu. Some specific API details:
     *      GL: Commands are actually sent to the driver, but glFlush is never called. Thus some
     *          sync objects from the flush will not be valid until a submission occurs.
     *
     *      Vulkan/Metal/D3D/Dawn: Commands are recorded to the backend APIs corresponding command
     *          buffer or encoder objects. However, these objects are not sent to the gpu until a
     *          submission occurs.
     *
     *  The work that is submitted to the GPU will be dependent on the BackendSurfaceAccess that is
     *  passed in.
     *
     *  If BackendSurfaceAccess::kNoAccess is passed in all commands will be issued to the GPU.
     *
     *  If BackendSurfaceAccess::kPresent is passed in and the backend API is not Vulkan, it is
     *  treated the same as kNoAccess. If the backend API is Vulkan, the VkImage that backs the
     *  SkSurface will be transferred back to its original queue. If the SkSurface was created by
     *  wrapping a VkImage, the queue will be set to the queue which was originally passed in on
     *  the GrVkImageInfo. Additionally, if the original queue was not external or foreign the
     *  layout of the VkImage will be set to VK_IMAGE_LAYOUT_PRESENT_SRC_KHR.
     *
     *  The GrFlushInfo describes additional options to flush. Please see documentation at
     *  GrFlushInfo for more info.
     *
     *  If the return is GrSemaphoresSubmitted::kYes, only initialized GrBackendSemaphores will be
     *  submitted to the gpu during the next submit call (it is possible Skia failed to create a
     *  subset of the semaphores). The client should not wait on these semaphores until after submit
     *  has been called, but must keep them alive until then. If a submit flag was passed in with
     *  the flush these valid semaphores can we waited on immediately. If this call returns
     *  GrSemaphoresSubmitted::kNo, the GPU backend will not submit any semaphores to be signaled on
     *  the GPU. Thus the client should not have the GPU wait on any of the semaphores passed in
     *  with the GrFlushInfo. Regardless of whether semaphores were submitted to the GPU or not, the
     *  client is still responsible for deleting any initialized semaphores.
     *  Regardless of semaphore submission the context will still be flushed. It should be
     *  emphasized that a return value of GrSemaphoresSubmitted::kNo does not mean the flush did not
     *  happen. It simply means there were no semaphores submitted to the GPU. A caller should only
     *  take this as a failure if they passed in semaphores to be submitted.
     *
     *  Pending surface commands are flushed regardless of the return result.
     *
     *  @param surface  The GPU backed surface to be flushed. Has no effect on a CPU-backed surface.
     *  @param access  type of access the call will do on the backend object after flush
     *  @param info    flush options
     */
    GrSemaphoresSubmitted flush(SkSurface* surface,
                                SkSurfaces::BackendSurfaceAccess access,
                                const GrFlushInfo& info);

    /**
     *  Same as above except:
     *
     *  If a skgpu::MutableTextureState is passed in, at the end of the flush we will transition
     *  the surface to be in the state requested by the skgpu::MutableTextureState. If the surface
     *  (or SkImage or GrBackendSurface wrapping the same backend object) is used again after this
     *  flush the state may be changed and no longer match what is requested here. This is often
     *  used if the surface will be used for presenting or external use and the client wants backend
     *  object to be prepped for that use. A finishedProc or semaphore on the GrFlushInfo will also
     *  include the work for any requested state change.
     *
     *  If the backend API is Vulkan, the caller can set the skgpu::MutableTextureState's
     *  VkImageLayout to VK_IMAGE_LAYOUT_UNDEFINED or queueFamilyIndex to VK_QUEUE_FAMILY_IGNORED to
     *  tell Skia to not change those respective states.
     *
     *  @param surface  The GPU backed surface to be flushed. Has no effect on a CPU-backed surface.
     *  @param info     flush options
     *  @param newState optional state change request after flush
     */
    GrSemaphoresSubmitted flush(SkSurface* surface,
                                const GrFlushInfo& info,
                                const skgpu::MutableTextureState* newState = nullptr);

    /** Call to ensure all reads/writes of the surface have been issued to the underlying 3D API.
     *  Skia will correctly order its own draws and pixel operations. This must to be used to ensure
     *  correct ordering when the surface backing store is accessed outside Skia (e.g. direct use of
     *  the 3D API or a windowing system). This is equivalent to
     *  calling ::flush with a default GrFlushInfo followed by ::submit(syncCpu).
     *
     *  Has no effect on a CPU-backed surface.
     */
    void flushAndSubmit(SkSurface* surface, GrSyncCpu sync = GrSyncCpu::kNo);

    /**
     * Flushes the given surface with the default GrFlushInfo.
     *
     *  Has no effect on a CPU-backed surface.
     */
    void flush(SkSurface* surface);

    /**
     * Submit outstanding work to the gpu from all previously un-submitted flushes. The return
     * value of the submit will indicate whether or not the submission to the GPU was successful.
     *
     * If the call returns true, all previously passed in semaphores in flush calls will have been
     * submitted to the GPU and they can safely be waited on. The caller should wait on those
     * semaphores or perform some other global synchronization before deleting the semaphores.
     *
     * If it returns false, then those same semaphores will not have been submitted and we will not
     * try to submit them again. The caller is free to delete the semaphores at any time.
     *
     * If GrSubmitInfo::fSync flag is GrSyncCpu::kYes, this function will return once the gpu has
     * finished with all submitted work.
     *
     * If GrSubmitInfo::fMarkBoundary flag is GrMarkFrameBoundary::kYes and the GPU supports a way
     * to be notified about frame boundaries, then we will notify the GPU during/after the
     * submission of work to the GPU. GrSubmitInfo::fFrameID is a frame ID that is passed to the
     * GPU when marking a boundary. Ideally this value should be unique for each frame. Currently
     * marking frame boundaries is only supported with the Vulkan backend and only if the
     * VK_EXT_frame_boudnary extenstion is available.
     */
    bool submit(const GrSubmitInfo&);

    bool submit(GrSyncCpu sync = GrSyncCpu::kNo) {
        GrSubmitInfo info;
        info.fSync = sync;

        return this->submit(info);
    }


    /**
     * Checks whether any asynchronous work is complete and if so calls related callbacks.
     */
    void checkAsyncWorkCompletion();

    /** Enumerates all cached GPU resources and dumps their memory to traceMemoryDump. */
    // Chrome is using this!
    void dumpMemoryStatistics(SkTraceMemoryDump* traceMemoryDump) const;
    void dumpMemoryStatisticsByTag(SkTraceMemoryDump* traceMemoryDump, GrGpuResourceTag& tag) const;

    bool supportsDistanceFieldText() const;

    void storeVkPipelineCacheData();

    /**
     * Retrieve the default GrBackendFormat for a given SkColorType and renderability.
     * It is guaranteed that this backend format will be the one used by the following
     * SkColorType and GrSurfaceCharacterization-based createBackendTexture methods.
     *
     * The caller should check that the returned format is valid.
     */
    using GrRecordingContext::defaultBackendFormat;

    /**
     * The explicitly allocated backend texture API allows clients to use Skia to create backend
     * objects outside of Skia proper (i.e., Skia's caching system will not know about them.)
     *
     * It is the client's responsibility to delete all these objects (using deleteBackendTexture)
     * before deleting the context used to create them. If the backend is Vulkan, the textures must
     * be deleted before abandoning the context as well. Additionally, clients should only delete
     * these objects on the thread for which that context is active.
     *
     * The client is responsible for ensuring synchronization between different uses
     * of the backend object (i.e., wrapping it in a surface, rendering to it, deleting the
     * surface, rewrapping it in a image and drawing the image will require explicit
     * synchronization on the client's part).
     */

     /**
      * If possible, create an uninitialized backend texture. The client should ensure that the
      * returned backend texture is valid.
      * For the Vulkan backend the layout of the created VkImage will be:
      *      VK_IMAGE_LAYOUT_UNDEFINED.
      */
    GrBackendTexture createBackendTexture(int width,
                                          int height,
                                          const GrBackendFormat&,
                                          skgpu::Mipmapped,
                                          GrRenderable,
                                          GrProtected = GrProtected::kNo,
                                          std::string_view label = {});

    /**
     * If possible, create an uninitialized backend texture. The client should ensure that the
     * returned backend texture is valid.
     * If successful, the created backend texture will be compatible with the provided
     * SkColorType.
     * For the Vulkan backend the layout of the created VkImage will be:
     *      VK_IMAGE_LAYOUT_UNDEFINED.
     */
    GrBackendTexture createBackendTexture(int width,
                                          int height,
                                          SkColorType,
                                          skgpu::Mipmapped,
                                          GrRenderable,
                                          GrProtected = GrProtected::kNo,
                                          std::string_view label = {});

    /**
     * If possible, create a backend texture initialized to a particular color. The client should
     * ensure that the returned backend texture is valid. The client can pass in a finishedProc
     * to be notified when the data has been uploaded by the gpu and the texture can be deleted. The
     * client is required to call `submit` to send the upload work to the gpu. The
     * finishedProc will always get called even if we failed to create the GrBackendTexture.
     * For the Vulkan backend the layout of the created VkImage will be:
     *      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
     */
    GrBackendTexture createBackendTexture(int width,
                                          int height,
                                          const GrBackendFormat&,
                                          const SkColor4f& color,
                                          skgpu::Mipmapped,
                                          GrRenderable,
                                          GrProtected = GrProtected::kNo,
                                          GrGpuFinishedProc finishedProc = nullptr,
                                          GrGpuFinishedContext finishedContext = nullptr,
                                          std::string_view label = {});

    /**
     * If possible, create a backend texture initialized to a particular color. The client should
     * ensure that the returned backend texture is valid. The client can pass in a finishedProc
     * to be notified when the data has been uploaded by the gpu and the texture can be deleted. The
     * client is required to call `submit` to send the upload work to the gpu. The
     * finishedProc will always get called even if we failed to create the GrBackendTexture.
     * If successful, the created backend texture will be compatible with the provided
     * SkColorType.
     * For the Vulkan backend the layout of the created VkImage will be:
     *      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
     */
    GrBackendTexture createBackendTexture(int width,
                                          int height,
                                          SkColorType,
                                          const SkColor4f& color,
                                          skgpu::Mipmapped,
                                          GrRenderable,
                                          GrProtected = GrProtected::kNo,
                                          GrGpuFinishedProc finishedProc = nullptr,
                                          GrGpuFinishedContext finishedContext = nullptr,
                                          std::string_view label = {});

    /**
     * If possible, create a backend texture initialized with the provided pixmap data. The client
     * should ensure that the returned backend texture is valid. The client can pass in a
     * finishedProc to be notified when the data has been uploaded by the gpu and the texture can be
     * deleted. The client is required to call `submit` to send the upload work to the gpu.
     * The finishedProc will always get called even if we failed to create the GrBackendTexture.
     * If successful, the created backend texture will be compatible with the provided
     * pixmap(s). Compatible, in this case, means that the backend format will be the result
     * of calling defaultBackendFormat on the base pixmap's colortype. The src data can be deleted
     * when this call returns.
     * If numLevels is 1 a non-mipmapped texture will result. If a mipmapped texture is desired
     * the data for all the mipmap levels must be provided. In the mipmapped case all the
     * colortypes of the provided pixmaps must be the same. Additionally, all the miplevels
     * must be sized correctly (please see SkMipmap::ComputeLevelSize and ComputeLevelCount). The
     * GrSurfaceOrigin controls whether the pixmap data is vertically flipped in the texture.
     * Note: the pixmap's alphatypes and colorspaces are ignored.
     * For the Vulkan backend the layout of the created VkImage will be:
     *      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
     */
    GrBackendTexture createBackendTexture(const SkPixmap srcData[],
                                          int numLevels,
                                          GrSurfaceOrigin,
                                          GrRenderable,
                                          GrProtected,
                                          GrGpuFinishedProc finishedProc = nullptr,
                                          GrGpuFinishedContext finishedContext = nullptr,
                                          std::string_view label = {});

    /**
     * Convenience version createBackendTexture() that takes just a base level pixmap.
     */
     GrBackendTexture createBackendTexture(const SkPixmap& srcData,
                                           GrSurfaceOrigin textureOrigin,
                                           GrRenderable renderable,
                                           GrProtected isProtected,
                                           GrGpuFinishedProc finishedProc = nullptr,
                                           GrGpuFinishedContext finishedContext = nullptr,
                                           std::string_view label = {});

    // Deprecated versions that do not take origin and assume top-left.
    GrBackendTexture createBackendTexture(const SkPixmap srcData[],
                                          int numLevels,
                                          GrRenderable renderable,
                                          GrProtected isProtected,
                                          GrGpuFinishedProc finishedProc = nullptr,
                                          GrGpuFinishedContext finishedContext = nullptr,
                                          std::string_view label = {});

    GrBackendTexture createBackendTexture(const SkPixmap& srcData,
                                          GrRenderable renderable,
                                          GrProtected isProtected,
                                          GrGpuFinishedProc finishedProc = nullptr,
                                          GrGpuFinishedContext finishedContext = nullptr,
                                          std::string_view label = {});

    /**
     * If possible, updates a backend texture to be filled to a particular color. The client should
     * check the return value to see if the update was successful. The client can pass in a
     * finishedProc to be notified when the data has been uploaded by the gpu and the texture can be
     * deleted. The client is required to call `submit` to send the upload work to the gpu.
     * The finishedProc will always get called even if we failed to update the GrBackendTexture.
     * For the Vulkan backend after a successful update the layout of the created VkImage will be:
     *      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
     */
    bool updateBackendTexture(const GrBackendTexture&,
                              const SkColor4f& color,
                              GrGpuFinishedProc finishedProc,
                              GrGpuFinishedContext finishedContext);

    /**
     * If possible, updates a backend texture to be filled to a particular color. The data in
     * GrBackendTexture and passed in color is interpreted with respect to the passed in
     * SkColorType. The client should check the return value to see if the update was successful.
     * The client can pass in a finishedProc to be notified when the data has been uploaded by the
     * gpu and the texture can be deleted. The client is required to call `submit` to send
     * the upload work to the gpu. The finishedProc will always get called even if we failed to
     * update the GrBackendTexture.
     * For the Vulkan backend after a successful update the layout of the created VkImage will be:
     *      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
     */
    bool updateBackendTexture(const GrBackendTexture&,
                              SkColorType skColorType,
                              const SkColor4f& color,
                              GrGpuFinishedProc finishedProc,
                              GrGpuFinishedContext finishedContext);

    /**
     * If possible, updates a backend texture filled with the provided pixmap data. The client
     * should check the return value to see if the update was successful. The client can pass in a
     * finishedProc to be notified when the data has been uploaded by the gpu and the texture can be
     * deleted. The client is required to call `submit` to send the upload work to the gpu.
     * The finishedProc will always get called even if we failed to create the GrBackendTexture.
     * The backend texture must be compatible with the provided pixmap(s). Compatible, in this case,
     * means that the backend format is compatible with the base pixmap's colortype. The src data
     * can be deleted when this call returns.
     * If the backend texture is mip mapped, the data for all the mipmap levels must be provided.
     * In the mipmapped case all the colortypes of the provided pixmaps must be the same.
     * Additionally, all the miplevels must be sized correctly (please see
     * SkMipmap::ComputeLevelSize and ComputeLevelCount). The GrSurfaceOrigin controls whether the
     * pixmap data is vertically flipped in the texture.
     * Note: the pixmap's alphatypes and colorspaces are ignored.
     * For the Vulkan backend after a successful update the layout of the created VkImage will be:
     *      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
     */
    bool updateBackendTexture(const GrBackendTexture&,
                              const SkPixmap srcData[],
                              int numLevels,
                              GrSurfaceOrigin = kTopLeft_GrSurfaceOrigin,
                              GrGpuFinishedProc finishedProc = nullptr,
                              GrGpuFinishedContext finishedContext = nullptr);

    /**
     * Convenience version of updateBackendTexture that takes just a base level pixmap.
     */
    bool updateBackendTexture(const GrBackendTexture& texture,
                              const SkPixmap& srcData,
                              GrSurfaceOrigin textureOrigin = kTopLeft_GrSurfaceOrigin,
                              GrGpuFinishedProc finishedProc = nullptr,
                              GrGpuFinishedContext finishedContext = nullptr) {
        return this->updateBackendTexture(texture,
                                          &srcData,
                                          1,
                                          textureOrigin,
                                          finishedProc,
                                          finishedContext);
    }

    // Deprecated version that does not take origin and assumes top-left.
    bool updateBackendTexture(const GrBackendTexture& texture,
                             const SkPixmap srcData[],
                             int numLevels,
                             GrGpuFinishedProc finishedProc,
                             GrGpuFinishedContext finishedContext);

    /**
     * Retrieve the GrBackendFormat for a given SkTextureCompressionType. This is
     * guaranteed to match the backend format used by the following
     * createCompressedBackendTexture methods that take a CompressionType.
     *
     * The caller should check that the returned format is valid.
     */
    using GrRecordingContext::compressedBackendFormat;

    /**
     *If possible, create a compressed backend texture initialized to a particular color. The
     * client should ensure that the returned backend texture is valid. The client can pass in a
     * finishedProc to be notified when the data has been uploaded by the gpu and the texture can be
     * deleted. The client is required to call `submit` to send the upload work to the gpu.
     * The finishedProc will always get called even if we failed to create the GrBackendTexture.
     * For the Vulkan backend the layout of the created VkImage will be:
     *      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
     */
    GrBackendTexture createCompressedBackendTexture(int width,
                                                    int height,
                                                    const GrBackendFormat&,
                                                    const SkColor4f& color,
                                                    skgpu::Mipmapped,
                                                    GrProtected = GrProtected::kNo,
                                                    GrGpuFinishedProc finishedProc = nullptr,
                                                    GrGpuFinishedContext finishedContext = nullptr);

    GrBackendTexture createCompressedBackendTexture(int width,
                                                    int height,
                                                    SkTextureCompressionType,
                                                    const SkColor4f& color,
                                                    skgpu::Mipmapped,
                                                    GrProtected = GrProtected::kNo,
                                                    GrGpuFinishedProc finishedProc = nullptr,
                                                    GrGpuFinishedContext finishedContext = nullptr);

    /**
     * If possible, create a backend texture initialized with the provided raw data. The client
     * should ensure that the returned backend texture is valid. The client can pass in a
     * finishedProc to be notified when the data has been uploaded by the gpu and the texture can be
     * deleted. The client is required to call `submit` to send the upload work to the gpu.
     * The finishedProc will always get called even if we failed to create the GrBackendTexture
     * If numLevels is 1 a non-mipmapped texture will result. If a mipmapped texture is desired
     * the data for all the mipmap levels must be provided. Additionally, all the miplevels
     * must be sized correctly (please see SkMipmap::ComputeLevelSize and ComputeLevelCount).
     * For the Vulkan backend the layout of the created VkImage will be:
     *      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
     */
    GrBackendTexture createCompressedBackendTexture(int width,
                                                    int height,
                                                    const GrBackendFormat&,
                                                    const void* data,
                                                    size_t dataSize,
                                                    skgpu::Mipmapped,
                                                    GrProtected = GrProtected::kNo,
                                                    GrGpuFinishedProc finishedProc = nullptr,
                                                    GrGpuFinishedContext finishedContext = nullptr);

    GrBackendTexture createCompressedBackendTexture(int width,
                                                    int height,
                                                    SkTextureCompressionType,
                                                    const void* data,
                                                    size_t dataSize,
                                                    skgpu::Mipmapped,
                                                    GrProtected = GrProtected::kNo,
                                                    GrGpuFinishedProc finishedProc = nullptr,
                                                    GrGpuFinishedContext finishedContext = nullptr);

    /**
     * If possible, updates a backend texture filled with the provided color. If the texture is
     * mipmapped, all levels of the mip chain will be updated to have the supplied color. The client
     * should check the return value to see if the update was successful. The client can pass in a
     * finishedProc to be notified when the data has been uploaded by the gpu and the texture can be
     * deleted. The client is required to call `submit` to send the upload work to the gpu.
     * The finishedProc will always get called even if we failed to create the GrBackendTexture.
     * For the Vulkan backend after a successful update the layout of the created VkImage will be:
     *      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
     */
    bool updateCompressedBackendTexture(const GrBackendTexture&,
                                        const SkColor4f& color,
                                        GrGpuFinishedProc finishedProc,
                                        GrGpuFinishedContext finishedContext);

    /**
     * If possible, updates a backend texture filled with the provided raw data. The client
     * should check the return value to see if the update was successful. The client can pass in a
     * finishedProc to be notified when the data has been uploaded by the gpu and the texture can be
     * deleted. The client is required to call `submit` to send the upload work to the gpu.
     * The finishedProc will always get called even if we failed to create the GrBackendTexture.
     * If a mipmapped texture is passed in, the data for all the mipmap levels must be provided.
     * Additionally, all the miplevels must be sized correctly (please see
     * SkMipMap::ComputeLevelSize and ComputeLevelCount).
     * For the Vulkan backend after a successful update the layout of the created VkImage will be:
     *      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
     */
    bool updateCompressedBackendTexture(const GrBackendTexture&,
                                        const void* data,
                                        size_t dataSize,
                                        GrGpuFinishedProc finishedProc,
                                        GrGpuFinishedContext finishedContext);

    /**
     * Updates the state of the GrBackendTexture/RenderTarget to have the passed in
     * skgpu::MutableTextureState. All objects that wrap the backend surface (i.e. SkSurfaces and
     * SkImages) will also be aware of this state change. This call does not submit the state change
     * to the gpu, but requires the client to call `submit` to send it to the GPU. The work
     * for this call is ordered linearly with all other calls that require GrContext::submit to be
     * called (e.g updateBackendTexture and flush). If finishedProc is not null then it will be
     * called with finishedContext after the state transition is known to have occurred on the GPU.
     *
     * See skgpu::MutableTextureState to see what state can be set via this call.
     *
     * If the backend API is Vulkan, the caller can set the skgpu::MutableTextureState's
     * VkImageLayout to VK_IMAGE_LAYOUT_UNDEFINED or queueFamilyIndex to VK_QUEUE_FAMILY_IGNORED to
     * tell Skia to not change those respective states.
     *
     * If previousState is not null and this returns true, then Skia will have filled in
     * previousState to have the values of the state before this call.
     */
    bool setBackendTextureState(const GrBackendTexture&,
                                const skgpu::MutableTextureState&,
                                skgpu::MutableTextureState* previousState = nullptr,
                                GrGpuFinishedProc finishedProc = nullptr,
                                GrGpuFinishedContext finishedContext = nullptr);
    bool setBackendRenderTargetState(const GrBackendRenderTarget&,
                                     const skgpu::MutableTextureState&,
                                     skgpu::MutableTextureState* previousState = nullptr,
                                     GrGpuFinishedProc finishedProc = nullptr,
                                     GrGpuFinishedContext finishedContext = nullptr);

    void deleteBackendTexture(const GrBackendTexture&);

    // This interface allows clients to pre-compile shaders and populate the runtime program cache.
    // The key and data blobs should be the ones passed to the PersistentCache, in SkSL format.
    //
    // Steps to use this API:
    //
    // 1) Create a GrDirectContext as normal, but set fPersistentCache on GrContextOptions to
    //    something that will save the cached shader blobs. Set fShaderCacheStrategy to kSkSL. This
    //    will ensure that the blobs are SkSL, and are suitable for pre-compilation.
    // 2) Run your application, and save all of the key/data pairs that are fed to the cache.
    //
    // 3) Switch over to shipping your application. Include the key/data pairs from above.
    // 4) At startup (or any convenient time), call precompileShader for each key/data pair.
    //    This will compile the SkSL to create a GL program, and populate the runtime cache.
    //
    // This is only guaranteed to work if the context/device used in step #2 are created in the
    // same way as the one used in step #4, and the same GrContextOptions are specified.
    // Using cached shader blobs on a different device or driver are undefined.
    bool precompileShader(const SkData& key, const SkData& data);
    void registerVulkanErrorCallback(const std::function<void()>& vulkanErrorCallback);
    void processVulkanError();

#ifdef SK_ENABLE_DUMP_GPU
    /** Returns a string with detailed information about the context & GPU, in JSON format. */
    SkString dump() const;
#endif

    class DirectContextID {
    public:
        static GrDirectContext::DirectContextID Next();

        DirectContextID() : fID(SK_InvalidUniqueID) {}

        bool operator==(const DirectContextID& that) const { return fID == that.fID; }
        bool operator!=(const DirectContextID& that) const { return !(*this == that); }

        void makeInvalid() { fID = SK_InvalidUniqueID; }
        bool isValid() const { return fID != SK_InvalidUniqueID; }

    private:
        constexpr DirectContextID(uint32_t id) : fID(id) {}
        uint32_t fID;
    };

    DirectContextID directContextID() const { return fDirectContextID; }

    // Provides access to functions that aren't part of the public API.
    GrDirectContextPriv priv();
    const GrDirectContextPriv priv() const;  // NOLINT(readability-const-return-type)

    /**
     * Set current resource tag for gpu cache recycle.
     */
    void setCurrentGrResourceTag(const GrGpuResourceTag& tag);

    /**
     * Pop resource tag.
     */
    void popGrResourceTag();


    /**
     * Get current resource tag for gpu cache recycle.
     *
     * @return all GrGpuResourceTags.
     */
    GrGpuResourceTag getCurrentGrResourceTag() const;

    /**
     * Releases GrGpuResource objects and removes them from the cache by tag.
     */
    void releaseByTag(const GrGpuResourceTag& tag);

    /**
     * Get all GrGpuResource tag.
     *
     * @return all GrGpuResourceTags.
     */
    std::set<GrGpuResourceTag> getAllGrGpuResourceTags() const;

    // OH ISSUE: get the memory information of the updated pid.
    void getUpdatedMemoryMap(std::unordered_map<int32_t, size_t> &out);

    // OH ISSUE: init gpu memory limit.
    void initGpuMemoryLimit(MemoryOverflowCalllback callback, uint64_t size);

    // OH ISSUE: check whether the PID is abnormal.
    bool isPidAbnormal() const override;

    void vmaDefragment();
    void dumpVmaStats(SkString *out);
protected:
    GrDirectContext(GrBackendApi backend,
                    const GrContextOptions& options,
                    sk_sp<GrContextThreadSafeProxy> proxy);

    bool init() override;

    GrAtlasManager* onGetAtlasManager() { return fAtlasManager.get(); }
#if !defined(SK_ENABLE_OPTIMIZE_SIZE)
    skgpu::ganesh::SmallPathAtlasMgr* onGetSmallPathAtlasMgr();
#endif

    GrDirectContext* asDirectContext() override { return this; }

private:
    // This call will make sure out work on the GPU is finished and will execute any outstanding
    // asynchronous work (e.g. calling finished procs, freeing resources, etc.) related to the
    // outstanding work on the gpu. The main use currently for this function is when tearing down or
    // abandoning the context.
    //
    // When we finish up work on the GPU it could trigger callbacks to the client. In the case we
    // are abandoning the context we don't want the client to be able to use the GrDirectContext to
    // issue more commands during the callback. Thus before calling this function we set the
    // GrDirectContext's state to be abandoned. However, we need to be able to get by the abaonded
    // check in the call to know that it is safe to execute this. The shouldExecuteWhileAbandoned
    // bool is used for this signal.
    void syncAllOutstandingGpuWork(bool shouldExecuteWhileAbandoned);

    // This delete callback needs to be the first thing on the GrDirectContext so that it is the
    // last thing destroyed. The callback may signal the client to clean up things that may need
    // to survive the lifetime of some of the other objects on the GrDirectCotnext. So make sure
    // we don't call it until all else has been destroyed.
    class DeleteCallbackHelper {
    public:
        DeleteCallbackHelper(GrDirectContextDestroyedContext context,
                             GrDirectContextDestroyedProc proc)
                : fContext(context), fProc(proc) {}

        ~DeleteCallbackHelper() {
            if (fProc) {
                fProc(fContext);
            }
        }

    private:
        GrDirectContextDestroyedContext fContext;
        GrDirectContextDestroyedProc fProc;
    };
    std::unique_ptr<DeleteCallbackHelper> fDeleteCallbackHelper;

    const DirectContextID                   fDirectContextID;
    // fTaskGroup must appear before anything that uses it (e.g. fGpu), so that it is destroyed
    // after all of its users. Clients of fTaskGroup will generally want to ensure that they call
    // wait() on it as they are being destroyed, to avoid the possibility of pending tasks being
    // invoked after objects they depend upon have already been destroyed.
    std::unique_ptr<SkTaskGroup>              fTaskGroup;
    std::unique_ptr<sktext::gpu::StrikeCache> fStrikeCache;
    std::unique_ptr<GrGpu>                    fGpu;
    std::unique_ptr<GrResourceCache>          fResourceCache;
    std::unique_ptr<GrResourceProvider>       fResourceProvider;

    // This is incremented before we start calling ReleaseProcs from GrSurfaces and decremented
    // after. A ReleaseProc may trigger code causing another resource to get freed so we to track
    // the count to know if we in a ReleaseProc at any level. When this is set to a value greated
    // than zero we will not allow abandonContext calls to be made on the context.
    int                                     fInsideReleaseProcCnt = 0;

    bool                                    fDidTestPMConversions;
    // true if the PM/UPM conversion succeeded; false otherwise
    bool                                    fPMUPMConversionsRoundTrip;

    GrContextOptions::PersistentCache*      fPersistentCache;

    std::unique_ptr<GrClientMappedBufferManager> fMappedBufferManager;
    std::unique_ptr<GrAtlasManager> fAtlasManager;
    std::function<void()> vulkanErrorCallback_;

#if !defined(SK_ENABLE_OPTIMIZE_SIZE)
    std::unique_ptr<skgpu::ganesh::SmallPathAtlasMgr> fSmallPathAtlasMgr;
#endif

    friend class GrDirectContextPriv;
};


#endif
