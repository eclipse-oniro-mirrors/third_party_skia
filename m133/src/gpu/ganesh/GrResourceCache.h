/*
 * Copyright 2014 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef GrResourceCache_DEFINED
#define GrResourceCache_DEFINED

#include <set>
#include <stack>
#include <unordered_set>

#include "include/core/SkRefCnt.h"
#include "include/core/SkTypes.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/private/base/SkDebug.h"
#include "include/private/base/SkTArray.h"
#include "include/private/base/SkTDArray.h"
#include "include/private/base/SkTo.h"
#include "include/private/gpu/ganesh/GrTypesPriv.h"
#include "src/base/SkTDPQueue.h"
#include "src/core/SkMessageBus.h"
#include "src/core/SkTDynamicHash.h"
#include "src/core/SkTMultiMap.h"
#include "src/gpu/GpuTypesPriv.h"
#include "src/gpu/ResourceKey.h"
#include "src/gpu/ganesh/GrGpuResource.h"
#include "src/gpu/ganesh/GrGpuResourceCacheAccess.h"
#include "src/gpu/ganesh/GrGpuResourcePriv.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>
#include <unordered_map>
#include <utility>

class GrProxyProvider;
class GrSurface;
class GrThreadSafeCache;
class SkString;
class SkTraceMemoryDump;
enum class GrPurgeResourceOptions;
namespace skgpu {
class SingleOwner;
}

/**
 * Manages the lifetime of all GrGpuResource instances.
 *
 * Resources may have optionally have two types of keys:
 *      1) A scratch key. This is for resources whose allocations are cached but not their contents.
 *         Multiple resources can share the same scratch key. This is so a caller can have two
 *         resource instances with the same properties (e.g. multipass rendering that ping-pongs
 *         between two temporary surfaces). The scratch key is set at resource creation time and
 *         should never change. Resources need not have a scratch key.
 *      2) A unique key. This key's meaning is specific to the domain that created the key. Only one
 *         resource may have a given unique key. The unique key can be set, cleared, or changed
 *         anytime after resource creation.
 *
 * A unique key always takes precedence over a scratch key when a resource has both types of keys.
 * If a resource has neither key type then it will be deleted as soon as the last reference to it
 * is dropped.
 */
class GrResourceCache {
public:
    GrResourceCache(skgpu::SingleOwner* owner,
                    GrDirectContext::DirectContextID owningContextID,
                    uint32_t familyID);
    ~GrResourceCache();

    /**
     * This is used to safely return a resource to the cache when the owner may be on another
     * thread from GrDirectContext. If the context still exists then using this method ensures that
     * the resource is received by the cache for processing (recycling or destruction) on the
     * context's thread.
     *
     * This is templated as it is rather than simply taking sk_sp<GrGpuResource> in order to enforce
     * that the caller passes an rvalue. If the caller doesn't move its ref into this function
     * then it will retain ownership, defeating the purpose. (Note that sk_sp<GrGpuResource>&&
     * doesn't work either because calling with sk_sp<GrSpecificResource> will create a temporary
     * sk_sp<GrGpuResource> which is an rvalue).
     */
    template<typename T>
    static std::enable_if_t<std::is_base_of_v<GrGpuResource, T>, void>
    ReturnResourceFromThread(sk_sp<T>&& resource, GrDirectContext::DirectContextID id) {
        UnrefResourceMessage msg(std::move(resource), id);
        UnrefResourceMessage::Bus::Post(std::move(msg));
    }

    // Default maximum number of bytes of gpu memory of budgeted resources in the cache.
    static const size_t kDefaultMaxSize             = 256 * (1 << 20);

    /** Used to access functionality needed by GrGpuResource for lifetime management. */
    class ResourceAccess;
    ResourceAccess resourceAccess();

    /**
     * Get current resource tag for gpu cache recycle.
     */
    GrGpuResourceTag getCurrentGrResourceTag() const;

    /**
     * Set current resourcetag for gpu cache recycle.
     */
    void setCurrentGrResourceTag(const GrGpuResourceTag& tag);

    /**
     * Pop resource tag.
     */
    void popGrResourceTag();

    /** Unique ID of the owning GrContext. */
    uint32_t contextUniqueID() const { return fContextUniqueID; }

    /** Sets the max gpu memory byte size of the cache. */
    void setLimit(size_t bytes);

    /**
     * Returns the number of resources.
     */
    int getResourceCount() const {
        return fPurgeableQueue.count() + fNonpurgeableResources.size();
    }

    /**
     * Returns the number of resources that count against the budget.
     */
    int getBudgetedResourceCount() const { return fBudgetedCount; }

    /**
     * Returns the number of bytes consumed by resources.
     */
    size_t getResourceBytes() const { return fBytes; }
#ifdef SKIA_DFX_FOR_OHOS
    void addAllocImageBytes(size_t bytes) { fAllocImageBytes += bytes; }
    void removeAllocImageBytes(size_t bytes) { fAllocImageBytes -= bytes; }
    void addAllocBufferBytes(size_t bytes) { fAllocBufferBytes += bytes; }
    void removeAllocBufferBytes(size_t bytes) { fAllocBufferBytes -= bytes; }
#endif

    /**
     * Returns the number of bytes held by unlocked resources which are available for purging.
     */
    size_t getPurgeableBytes() const { return fPurgeableBytes; }

    /**
     * Returns the number of bytes consumed by budgeted resources.
     */
    size_t getBudgetedResourceBytes() const { return fBudgetedBytes; }

    /**
     * Returns the number of bytes consumed by cached resources.
     */
    size_t getMaxResourceBytes() const { return fMaxBytes; }

    /**
     * Abandons the backend API resources owned by all GrGpuResource objects and removes them from
     * the cache.
     */
    void abandonAll();

    /**
     * Releases the backend API resources owned by all GrGpuResource objects and removes them from
     * the cache.
     */
    void releaseAll();

    /**
     * Release GrGpuResource objects and removes them from the cache by tag.
     */
    void releaseByTag(const GrGpuResourceTag& tag);
    /**
     * Get all GrGpuResource tags.
    */
    std::set<GrGpuResourceTag> getAllGrGpuResourceTag() const;

    /**
     * Find a resource that matches a scratch key.
     */
    GrGpuResource* findAndRefScratchResource(const skgpu::ScratchKey& scratchKey);

#ifdef SK_DEBUG
    // This is not particularly fast and only used for validation, so debug only.
    int countScratchEntriesForKey(const skgpu::ScratchKey& scratchKey) const {
        return fScratchMap.countForKey(scratchKey);
    }
#endif

    /**
     * Find a resource that matches a unique key.
     */
    GrGpuResource* findAndRefUniqueResource(const skgpu::UniqueKey& key) {
        GrGpuResource* resource = fUniqueHash.find(key);
        if (resource) {
            this->refAndMakeResourceMRU(resource);
        }
        return resource;
    }

    /**
     * Query whether a unique key exists in the cache.
     */
    bool hasUniqueKey(const skgpu::UniqueKey& key) const {
        return SkToBool(fUniqueHash.find(key));
    }

    /** Purges resources to become under budget and processes resources with invalidated unique
        keys. */
    void purgeAsNeeded();

    // Purge unlocked resources. If 'opts' is kScratchResourcesOnly, the purgeable resources
    // containing persistent data are spared. If it is kAllResources then all purgeable resources
    // will be deleted.
    void purgeUnlockedResources(GrPurgeResourceOptions opts) {
        this->purgeUnlockedResources(/*purgeTime=*/nullptr, opts);
    }

    void purgeUnlockedResourcesByTag(bool scratchResourceOnly, const GrGpuResourceTag& tag);
    void purgeUnlockedResourcesByPid(bool scratchResourceOnly, const std::set<int>& exitedPidSet);
    
    // Purge unlocked resources not used since the passed point in time. If 'opts' is
    // kScratchResourcesOnly, the purgeable resources containing persistent data are spared.
    // If it is kAllResources then all purgeable resources older than 'purgeTime' will be deleted.
    void purgeResourcesNotUsedSince(skgpu::StdSteadyClock::time_point purgeTime,
                                    GrPurgeResourceOptions opts) {
        this->purgeUnlockedResources(&purgeTime, opts);
    }

    /** If it's possible to purge enough resources to get the provided amount of budget
        headroom, do so and return true. If it's not possible, do nothing and return false.
     */
    bool purgeToMakeHeadroom(size_t desiredHeadroomBytes);

    bool overBudget() const { return fBudgetedBytes > fMaxBytes; }

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

    /** Returns true if the cache would like a flush to occur in order to make more resources
        purgeable. */
    bool requestsFlush() const;

#if GR_CACHE_STATS
    struct Stats {
        int fTotal;
        int fNumPurgeable;
        int fNumNonPurgeable;

        int fScratch;
        int fWrapped;
        size_t fUnbudgetedSize;

        Stats() { this->reset(); }

        void reset() {
            fTotal = 0;
            fNumPurgeable = 0;
            fNumNonPurgeable = 0;
            fScratch = 0;
            fWrapped = 0;
            fUnbudgetedSize = 0;
        }

        void update(GrGpuResource* resource) {
            if (resource->cacheAccess().isScratch()) {
                ++fScratch;
            }
            if (resource->resourcePriv().refsWrappedObjects()) {
                ++fWrapped;
            }
            if (GrBudgetedType::kBudgeted != resource->resourcePriv().budgetedType()) {
                fUnbudgetedSize += resource->gpuMemorySize();
            }
        }
    };

    void getStats(Stats*) const;

#if defined(GPU_TEST_UTILS)
    void dumpStats(SkString*) const;

    void dumpStatsKeyValuePairs(
            skia_private::TArray<SkString>* keys, skia_private::TArray<double>* value) const;
#endif

#endif // GR_CACHE_STATS

#if defined(GPU_TEST_UTILS)
    int countUniqueKeysWithTag(const char* tag) const;

    void changeTimestamp(uint32_t newTimestamp);

    void visitSurfaces(const std::function<void(const GrSurface*, bool purgeable)>&) const;
#endif

    // Enumerates all cached resources and dumps their details to traceMemoryDump.
    void dumpMemoryStatistics(SkTraceMemoryDump* traceMemoryDump) const;
    void dumpMemoryStatistics(SkTraceMemoryDump* traceMemoryDump, GrGpuResourceTag& tag) const;

    void setProxyProvider(GrProxyProvider* proxyProvider) { fProxyProvider = proxyProvider; }
    void setThreadSafeCache(GrThreadSafeCache* threadSafeCache) {
        fThreadSafeCache = threadSafeCache;
    }

    std::set<GrGpuResourceTag> getAllGrGpuResourceTags() const; // Get the tag of all GPU resources

    // OH ISSUE: get the memory information of the updated pid.
    void getUpdatedMemoryMap(std::unordered_map<int32_t, size_t> &out);

    // OH ISSUE: check whether the PID is abnormal.
    bool isPidAbnormal() const;

    // OH ISSUE: change the fbyte when the resource tag changes.
    void changeByteOfPid(int32_t beforePid, int32_t afterPid,
        size_t bytes, bool beforeRealAlloc, bool afterRealAlloc);

    // OH ISSUE: init gpu memory limit.
    void initGpuMemoryLimit(MemoryOverflowCalllback callback, uint64_t size);

    // It'd be nice if this could be private but SkMessageBus relies on macros to define types that
    // require this to be public.
    class UnrefResourceMessage {
    public:
        GrDirectContext::DirectContextID recipient() const { return fRecipient; }

        UnrefResourceMessage(UnrefResourceMessage&&) = default;
        UnrefResourceMessage& operator=(UnrefResourceMessage&&) = default;

    private:
        friend class GrResourceCache;

        using Bus = SkMessageBus<UnrefResourceMessage,
                                 GrDirectContext::DirectContextID,
                                 /*AllowCopyableMessage=*/false>;

        UnrefResourceMessage(sk_sp<GrGpuResource>&& resource,
                             GrDirectContext::DirectContextID recipient)
                : fResource(std::move(resource)), fRecipient(recipient) {}

        UnrefResourceMessage(const UnrefResourceMessage&) = delete;
        UnrefResourceMessage& operator=(const UnrefResourceMessage&) = delete;

        sk_sp<GrGpuResource> fResource;
        GrDirectContext::DirectContextID fRecipient;
    };

#ifdef SKIA_DFX_FOR_OHOS
    void dumpInfo(SkString* out);
    std::string cacheInfo();

#ifdef SKIA_OHOS
    static bool purgeUnlocakedResTraceEnabled_;
    struct SimpleCacheInfo {
        int fPurgeableQueueCount;
        int fNonpurgeableResourcesCount;
        size_t fPurgeableBytes;
        int fBudgetedCount;
        size_t fBudgetedBytes;
        size_t fAllocImageBytes;
        size_t fAllocBufferBytes;
    };
#endif
#endif
private:
    ///////////////////////////////////////////////////////////////////////////
    /// @name Methods accessible via ResourceAccess
    ////
    void insertResource(GrGpuResource*);
    void removeResource(GrGpuResource*);
    void notifyARefCntReachedZero(GrGpuResource*, GrGpuResource::LastRemovedRef);
    void changeUniqueKey(GrGpuResource*, const skgpu::UniqueKey&);
    void removeUniqueKey(GrGpuResource*);
    void willRemoveScratchKey(const GrGpuResource*);
    void didChangeBudgetStatus(GrGpuResource*);
    void refResource(GrGpuResource* resource);
    /// @}

    void refAndMakeResourceMRU(GrGpuResource*);
    void processFreedGpuResources();
    void addToNonpurgeableArray(GrGpuResource*);
    void removeFromNonpurgeableArray(GrGpuResource*);

    bool wouldFit(size_t bytes) const { return fBudgetedBytes+bytes <= fMaxBytes; }

    uint32_t getNextTimestamp();

    void purgeUnlockedResources(const skgpu::StdSteadyClock::time_point* purgeTime,
                                GrPurgeResourceOptions opts);

#ifdef SK_DEBUG
    bool isInCache(const GrGpuResource* r) const;
    void validate() const;
#else
    void validate() const {}
#endif

#ifdef SKIA_DFX_FOR_OHOS
#ifdef SKIA_OHOS
    void traceBeforePurgeUnlockRes(const std::string& method, SimpleCacheInfo& simpleCacheInfo);
    void traceAfterPurgeUnlockRes(const std::string& method, const SimpleCacheInfo& simpleCacheInfo);
    std::string cacheInfoComparison(const SimpleCacheInfo& simpleCacheInfo);
#endif
    std::string cacheInfoPurgeableQueue();
    std::string cacheInfoNoPurgeableQueue();
    void updatePurgeableWidMap(GrGpuResource* resource,
                     std::map<uint64_t, std::string>& nameInfoWid,
                     std::map<uint64_t, size_t>& sizeInfoWid,
                     std::map<uint64_t, int>& pidInfoWid,
                     std::map<uint64_t, int>& countInfoWid);
    void updatePurgeablePidMap(GrGpuResource* resource,
                     std::map<uint32_t, std::string>& nameInfoPid,
                     std::map<uint32_t, size_t>& sizeInfoPid,
                     std::map<uint32_t, int>& countInfoPid);
    void updatePurgeableFidMap(GrGpuResource* resource,
                     std::map<uint32_t, std::string>& nameInfoFid,
                     std::map<uint32_t, size_t>& sizeInfoFid,
                     std::map<uint32_t, int>& countInfoFid);
    void updatePurgeableWidInfo(std::string& infoStr,
                     std::map<uint64_t, std::string>& nameInfoWid,
                     std::map<uint64_t, size_t>& sizeInfoWid,
                     std::map<uint64_t, int>& pidInfoWid,
                     std::map<uint64_t, int>& countInfoWid);
    void updatePurgeablePidInfo(std::string& infoStr,
                     std::map<uint32_t, std::string>& nameInfoPid,
                     std::map<uint32_t, size_t>& sizeInfoPid,
                     std::map<uint32_t, int>& countInfoPid);
    void updatePurgeableFidInfo(std::string& infoStr,
                     std::map<uint32_t, std::string>& nameInfoFid,
                     std::map<uint32_t, size_t>& sizeInfoFid,
                     std::map<uint32_t, int>& countInfoFid);
    void updatePurgeableUnknownInfo(std::string& infoStr, const std::string& unknownPrefix,
        const int countUnknown, const size_t sizeUnknown);
#endif

    class AutoValidate;

    struct ScratchMapTraits {
        static const skgpu::ScratchKey& GetKey(const GrGpuResource& r) {
            return r.resourcePriv().getScratchKey();
        }

        static uint32_t Hash(const skgpu::ScratchKey& key) { return key.hash(); }
        static void OnFree(GrGpuResource*) { }
    };
    typedef SkTMultiMap<GrGpuResource, skgpu::ScratchKey, ScratchMapTraits> ScratchMap;

    struct UniqueHashTraits {
        static const skgpu::UniqueKey& GetKey(const GrGpuResource& r) { return r.getUniqueKey(); }

        static uint32_t Hash(const skgpu::UniqueKey& key) { return key.hash(); }
    };
    typedef SkTDynamicHash<GrGpuResource, skgpu::UniqueKey, UniqueHashTraits> UniqueHash;

    static bool CompareTimestamp(GrGpuResource* const& a, GrGpuResource* const& b) {
        return a->cacheAccess().timestamp() < b->cacheAccess().timestamp();
    }

    static int* AccessResourceIndex(GrGpuResource* const& res) {
        return res->cacheAccess().accessCacheIndex();
    }

    typedef SkMessageBus<skgpu::UniqueKeyInvalidatedMessage, uint32_t>::Inbox InvalidUniqueKeyInbox;
    typedef SkTDPQueue<GrGpuResource*, CompareTimestamp, AccessResourceIndex> PurgeableQueue;
    typedef SkTDArray<GrGpuResource*> ResourceArray;

    GrProxyProvider*                    fProxyProvider = nullptr;
    GrThreadSafeCache*                  fThreadSafeCache = nullptr;

    // Whenever a resource is added to the cache or the result of a cache lookup, fTimestamp is
    // assigned as the resource's timestamp and then incremented. fPurgeableQueue orders the
    // purgeable resources by this value, and thus is used to purge resources in LRU order.
    uint32_t                            fTimestamp = 0;
    PurgeableQueue                      fPurgeableQueue;
    ResourceArray                       fNonpurgeableResources;

    // This map holds all resources that can be used as scratch resources.
    ScratchMap                          fScratchMap;
    // This holds all resources that have unique keys.
    UniqueHash                          fUniqueHash;

    // our budget, used in purgeAsNeeded()
    size_t                              fMaxBytes = kDefaultMaxSize;

#if GR_CACHE_STATS
    int                                 fHighWaterCount = 0;
    size_t                              fHighWaterBytes = 0;
    int                                 fBudgetedHighWaterCount = 0;
    size_t                              fBudgetedHighWaterBytes = 0;
#endif

    // our current stats for all resources
    SkDEBUGCODE(int                     fCount = 0;)
    size_t                              fBytes = 0;
#ifdef SKIA_DFX_FOR_OHOS
    size_t                              fAllocImageBytes = 0;
    size_t                              fAllocBufferBytes = 0;
#endif

    // our current stats for resources that count against the budget
    int                                 fBudgetedCount = 0;
    size_t                              fBudgetedBytes = 0;
    size_t                              fPurgeableBytes = 0;
    int                                 fNumBudgetedResourcesFlushWillMakePurgeable = 0;

    InvalidUniqueKeyInbox               fInvalidUniqueKeyInbox;
    UnrefResourceMessage::Bus::Inbox    fUnrefResourceInbox;

    GrDirectContext::DirectContextID    fOwningContextID;
    uint32_t                            fContextUniqueID = SK_InvalidUniqueID;
    skgpu::SingleOwner*                 fSingleOwner = nullptr;

    // This resource is allowed to be in the nonpurgeable array for the sake of validate() because
    // we're in the midst of converting it to purgeable status.
    SkDEBUGCODE(GrGpuResource*          fNewlyPurgeableResourceForValidation = nullptr;)
    std::stack<GrGpuResourceTag> grResourceTagCacheStack;

    // OH ISSUE: gpu memory limit.
    uint64_t fMemoryControl_ = UINT64_MAX;
    // OH ISSUE: memory overflow callback.
    MemoryOverflowCalllback fMemoryOverflowCallback_ = nullptr;
    // OH ISSUE: record pid had exited.
    std::unordered_set<int32_t> fExitedPid_;

    //Indicates the cached resource tags.
    std::stack<GrGpuResourceTag> GrResourceCacheStack;

    // OH ISSUE: stores fBytes of each pid.
    std::unordered_map<int32_t, size_t> fBytesOfPid;
    // OH ISSUE: stores the memory information of the updated pid.
    std::unordered_map<int32_t, size_t> fUpdatedBytesOfPid;
};

class GrResourceCache::ResourceAccess {
private:
    ResourceAccess(GrResourceCache* cache) : fCache(cache) { }
    ResourceAccess(const ResourceAccess& that) : fCache(that.fCache) { }
    ResourceAccess& operator=(const ResourceAccess&) = delete;

    /**
     * Insert a resource into the cache.
     */
    void insertResource(GrGpuResource* resource) { fCache->insertResource(resource); }

    /**
     * Removes a resource from the cache.
     */
    void removeResource(GrGpuResource* resource) { fCache->removeResource(resource); }

    /**
     * Adds a ref to a resource with proper tracking if the resource has 0 refs prior to
     * adding the ref.
     */
    void refResource(GrGpuResource* resource) { fCache->refResource(resource); }

    /**
     * Get current resource tag for gpu cache recycle.
     */
    GrGpuResourceTag getCurrentGrResourceTag() const { return fCache->getCurrentGrResourceTag(); }

    /**
     * Notifications that should be sent to the cache when the ref/io cnt status of resources
     * changes.
     */
    enum RefNotificationFlags {
        /** All types of refs on the resource have reached zero. */
        kAllCntsReachedZero_RefNotificationFlag = 0x1,
        /** The normal (not pending IO type) ref cnt has reached zero. */
        kRefCntReachedZero_RefNotificationFlag  = 0x2,
    };
    /**
     * Called by GrGpuResources when they detect one of their ref cnts have reached zero. This may
     * either be the main ref or the command buffer usage ref.
     */
    void notifyARefCntReachedZero(GrGpuResource* resource,
                                  GrGpuResource::LastRemovedRef removedRef) {
        fCache->notifyARefCntReachedZero(resource, removedRef);
    }

    /**
     * Called by GrGpuResources to change their unique keys.
     */
    void changeUniqueKey(GrGpuResource* resource, const skgpu::UniqueKey& newKey) {
         fCache->changeUniqueKey(resource, newKey);
    }

    /**
     * Called by a GrGpuResource to remove its unique key.
     */
    void removeUniqueKey(GrGpuResource* resource) { fCache->removeUniqueKey(resource); }

    /**
     * Called by a GrGpuResource when it removes its scratch key.
     */
    void willRemoveScratchKey(const GrGpuResource* resource) {
        fCache->willRemoveScratchKey(resource);
    }

    /**
     * Called by GrGpuResources when they change from budgeted to unbudgeted or vice versa.
     */
    void didChangeBudgetStatus(GrGpuResource* resource) { fCache->didChangeBudgetStatus(resource); }

    void changeByteOfPid(int32_t beforePid, int32_t afterPid,
        size_t bytes, bool beforeRealAlloc, bool afterRealAlloc)
    {
        fCache->changeByteOfPid(beforePid, afterPid, bytes, beforeRealAlloc, afterRealAlloc);
    }

    // No taking addresses of this type.
    const ResourceAccess* operator&() const;
    ResourceAccess* operator&();

    GrResourceCache* fCache;

    friend class GrGpuResource; // To access all the proxy inline methods.
    friend class GrResourceCache; // To create this type.
};

static inline bool SkShouldPostMessageToBus(const GrResourceCache::UnrefResourceMessage& msg,
                                            GrDirectContext::DirectContextID potentialRecipient) {
    return potentialRecipient == msg.recipient();
}

inline GrResourceCache::ResourceAccess GrResourceCache::resourceAccess() {
    return ResourceAccess(this);
}

#endif
