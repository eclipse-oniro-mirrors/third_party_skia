/*
 * Copyright 2014 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/gpu/GrResourceCache.h"
#include <atomic>
#include <ctime>
#include <vector>
#include <map>
#include <sstream>
#ifdef NOT_BUILD_FOR_OHOS_SDK
#include <parameters.h>
#endif
#include "include/core/SkString.h"
#include "include/gpu/GrDirectContext.h"
#include "include/private/GrSingleOwner.h"
#include "include/private/SkTo.h"
#include "include/utils/SkRandom.h"
#include "src/core/SkMessageBus.h"
#include "src/core/SkOpts.h"
#include "src/core/SkScopeExit.h"
#include "src/core/SkTSort.h"
#include "src/gpu/GrCaps.h"
#include "src/gpu/GrDirectContextPriv.h"
#include "src/gpu/GrGpuResourceCacheAccess.h"
#include "src/gpu/GrProxyProvider.h"
#include "src/gpu/GrTexture.h"
#include "src/gpu/GrTextureProxyCacheAccess.h"
#include "src/gpu/GrThreadSafeCache.h"
#include "src/gpu/GrTracing.h"
#include "src/gpu/SkGr.h"

DECLARE_SKMESSAGEBUS_MESSAGE(GrUniqueKeyInvalidatedMessage, uint32_t, true);

DECLARE_SKMESSAGEBUS_MESSAGE(GrTextureFreedMessage, GrDirectContext::DirectContextID, true);

#define ASSERT_SINGLE_OWNER GR_ASSERT_SINGLE_OWNER(fSingleOwner)

//////////////////////////////////////////////////////////////////////////////

GrScratchKey::ResourceType GrScratchKey::GenerateResourceType() {
    static std::atomic<int32_t> nextType{INHERITED::kInvalidDomain + 1};

    int32_t type = nextType.fetch_add(1, std::memory_order_relaxed);
    if (type > SkTo<int32_t>(UINT16_MAX)) {
        SK_ABORT("Too many Resource Types");
    }

    return static_cast<ResourceType>(type);
}

GrUniqueKey::Domain GrUniqueKey::GenerateDomain() {
    static std::atomic<int32_t> nextDomain{INHERITED::kInvalidDomain + 1};

    int32_t domain = nextDomain.fetch_add(1, std::memory_order_relaxed);
    if (domain > SkTo<int32_t>(UINT16_MAX)) {
        SK_ABORT("Too many GrUniqueKey Domains");
    }

    return static_cast<Domain>(domain);
}

uint32_t GrResourceKeyHash(const uint32_t* data, size_t size) {
    return SkOpts::hash(data, size);
}

//////////////////////////////////////////////////////////////////////////////

class GrResourceCache::AutoValidate : ::SkNoncopyable {
public:
    AutoValidate(GrResourceCache* cache) : fCache(cache) { cache->validate(); }
    ~AutoValidate() { fCache->validate(); }
private:
    GrResourceCache* fCache;
};

//////////////////////////////////////////////////////////////////////////////

inline GrResourceCache::TextureAwaitingUnref::TextureAwaitingUnref() = default;

inline GrResourceCache::TextureAwaitingUnref::TextureAwaitingUnref(GrTexture* texture)
        : fTexture(texture), fNumUnrefs(1) {}

inline GrResourceCache::TextureAwaitingUnref::TextureAwaitingUnref(TextureAwaitingUnref&& that) {
    fTexture = std::exchange(that.fTexture, nullptr);
    fNumUnrefs = std::exchange(that.fNumUnrefs, 0);
}

inline GrResourceCache::TextureAwaitingUnref& GrResourceCache::TextureAwaitingUnref::operator=(
        TextureAwaitingUnref&& that) {
    fTexture = std::exchange(that.fTexture, nullptr);
    fNumUnrefs = std::exchange(that.fNumUnrefs, 0);
    return *this;
}

inline GrResourceCache::TextureAwaitingUnref::~TextureAwaitingUnref() {
    if (fTexture) {
        for (int i = 0; i < fNumUnrefs; ++i) {
            fTexture->unref();
        }
    }
}

inline void GrResourceCache::TextureAwaitingUnref::TextureAwaitingUnref::addRef() { ++fNumUnrefs; }

inline void GrResourceCache::TextureAwaitingUnref::unref() {
    SkASSERT(fNumUnrefs > 0);
    fTexture->unref();
    --fNumUnrefs;
}

inline bool GrResourceCache::TextureAwaitingUnref::finished() { return !fNumUnrefs; }

//////////////////////////////////////////////////////////////////////////////

GrResourceCache::GrResourceCache(GrSingleOwner* singleOwner,
                                 GrDirectContext::DirectContextID owningContextID,
                                 uint32_t familyID)
        : fInvalidUniqueKeyInbox(familyID)
        , fFreedTextureInbox(owningContextID)
        , fOwningContextID(owningContextID)
        , fContextUniqueID(familyID)
        , fSingleOwner(singleOwner) {
    SkASSERT(owningContextID.isValid());
    SkASSERT(familyID != SK_InvalidUniqueID);
#ifdef NOT_BUILD_FOR_OHOS_SDK
    static int overtimeDuration =
        std::atoi(OHOS::system::GetParameter("persist.sys.graphic.mem.async_free_cache_overtime", "600").c_str());
    static double maxBytesRate =
        std::atof(OHOS::system::GetParameter("persist.sys.graphic.mem.async_free_cache_max_rate", "0.9").c_str());
#else
    static int overtimeDuration = 600;
    static double maxBytesRate = 0.9;
#endif
    fMaxBytesRate = maxBytesRate;
    fOvertimeDuration = overtimeDuration;
}

GrResourceCache::~GrResourceCache() {
    this->releaseAll();
}

void GrResourceCache::setLimit(size_t bytes) {
    fMaxBytes = bytes;
    this->purgeAsNeeded();
}

#ifdef SKIA_DFX_FOR_OHOS
static constexpr int MB = 1024 * 1024;

#ifdef SKIA_OHOS_FOR_OHOS_TRACE
bool GrResourceCache::purgeUnlocakedResTraceEnabled_ =
    std::atoi((OHOS::system::GetParameter("sys.graphic.skia.cache.debug", "0").c_str())) == 1;
#endif

void GrResourceCache::dumpInfo(SkString* out) {
    if (out == nullptr) {
        SkDebugf("OHOS GrResourceCache::dumpInfo outPtr is nullptr!");
        return;
    }
    auto info = cacheInfo();
    constexpr uint8_t STEP_INDEX = 1;
    SkTArray<SkString> lines;
    SkStrSplit(info.substr(STEP_INDEX, info.length() - STEP_INDEX).c_str(), ";", &lines);
    for (int i = 0; i < lines.size(); ++i) {
        out->appendf("    %s\n", lines[i].c_str());
    }
}

std::string GrResourceCache::cacheInfo()
{
    auto fPurgeableQueueInfoStr = cacheInfoPurgeableQueue();
    auto fNonpurgeableResourcesInfoStr = cacheInfoNoPurgeableQueue();
    size_t fRealAllocBytes = cacheInfoRealAllocSize();
    auto fRealAllocInfoStr = cacheInfoRealAllocQueue();
    auto fRealBytesOfPidInfoStr = realBytesOfPid();

    std::ostringstream cacheInfoStream;
    cacheInfoStream << "[fPurgeableQueueInfoStr.count : " << fPurgeableQueue.count()
        << "; fNonpurgeableResources.count : " << fNonpurgeableResources.count()
        << "; fBudgetedBytes : " << fBudgetedBytes
        << "(" << static_cast<size_t>(fBudgetedBytes / MB)
        << " MB) / " << fMaxBytes
        << "(" << static_cast<size_t>(fMaxBytes / MB)
        << " MB); fBudgetedCount : " << fBudgetedCount
        << "; fBytes : " << fBytes
        << "(" << static_cast<size_t>(fBytes / MB)
        << " MB); fPurgeableBytes : " << fPurgeableBytes
        << "(" << static_cast<size_t>(fPurgeableBytes / MB)
        << " MB); fAllocImageBytes : " << fAllocImageBytes
        << "(" << static_cast<size_t>(fAllocImageBytes / MB)
        << " MB); fAllocBufferBytes : " << fAllocBufferBytes
        << "(" << static_cast<size_t>(fAllocBufferBytes / MB)
        << " MB); fRealAllocBytes : " << fRealAllocBytes
        << "(" << static_cast<size_t>(fRealAllocBytes / MB)
        << " MB); fTimestamp : " << fTimestamp
        << "; " << fPurgeableQueueInfoStr << "; " << fNonpurgeableResourcesInfoStr
        << "; " << fRealAllocInfoStr << "; " << fRealBytesOfPidInfoStr;
    return cacheInfoStream.str();
}

#ifdef SKIA_OHOS_FOR_OHOS_TRACE
void GrResourceCache::traceBeforePurgeUnlockRes(const std::string& method, SimpleCacheInfo& simpleCacheInfo)
{
    if (purgeUnlocakedResTraceEnabled_) {
        StartTrace(HITRACE_TAG_GRAPHIC_AGP, method + " begin cacheInfo = " + cacheInfo());
    } else {
        simpleCacheInfo.fPurgeableQueueCount = fPurgeableQueue.count();
        simpleCacheInfo.fNonpurgeableResourcesCount = fNonpurgeableResources.count();
        simpleCacheInfo.fPurgeableBytes = fPurgeableBytes;
        simpleCacheInfo.fBudgetedCount = fBudgetedCount;
        simpleCacheInfo.fBudgetedBytes = fBudgetedBytes;
        simpleCacheInfo.fAllocImageBytes = fAllocImageBytes;
        simpleCacheInfo.fAllocBufferBytes = fAllocBufferBytes;
    }
}

void GrResourceCache::traceAfterPurgeUnlockRes(const std::string& method, const SimpleCacheInfo& simpleCacheInfo)
{
    if (purgeUnlocakedResTraceEnabled_) {
        HITRACE_OHOS_NAME_FMT_ALWAYS("%s end cacheInfo = %s", method.c_str(), cacheInfo().c_str());
        FinishTrace(HITRACE_TAG_GRAPHIC_AGP);
    } else {
        HITRACE_OHOS_NAME_FMT_ALWAYS("%s end cacheInfo = %s",
            method.c_str(), cacheInfoComparison(simpleCacheInfo).c_str());
    }
}

std::string GrResourceCache::cacheInfoComparison(const SimpleCacheInfo& simpleCacheInfo)
{
    std::ostringstream cacheInfoComparison;
    cacheInfoComparison << "PurgeableCount : " << simpleCacheInfo.fPurgeableQueueCount
        << " / " << fPurgeableQueue.count()
        << "; NonpurgeableCount : " << simpleCacheInfo.fNonpurgeableResourcesCount
        << " / " << fNonpurgeableResources.count()
        << "; PurgeableBytes : " << simpleCacheInfo.fPurgeableBytes << " / " << fPurgeableBytes
        << "; BudgetedCount : " << simpleCacheInfo.fBudgetedCount << " / " << fBudgetedCount
        << "; BudgetedBytes : " << simpleCacheInfo.fBudgetedBytes << " / " << fBudgetedBytes
        << "; AllocImageBytes : " << simpleCacheInfo.fAllocImageBytes << " / " << fAllocImageBytes
        << "; AllocBufferBytes : " << simpleCacheInfo.fAllocBufferBytes << " / " << fAllocBufferBytes;
    return cacheInfoComparison.str();
}
#endif // SKIA_OHOS_FOR_OHOS_TRACE

std::string GrResourceCache::cacheInfoPurgeableQueue()
{
    std::map<uint32_t, int> purgSizeInfoWid;
    std::map<uint32_t, int> purgCountInfoWid;
    std::map<uint32_t, std::string> purgNameInfoWid;
    std::map<uint32_t, int> purgPidInfoWid;

    std::map<uint32_t, int> purgSizeInfoPid;
    std::map<uint32_t, int> purgCountInfoPid;
    std::map<uint32_t, std::string> purgNameInfoPid;

    std::map<uint32_t, int> purgSizeInfoFid;
    std::map<uint32_t, int> purgCountInfoFid;
    std::map<uint32_t, std::string> purgNameInfoFid;

    int purgCountUnknown = 0;
    int purgSizeUnknown = 0;

    for (int i = 0; i < fPurgeableQueue.count(); i++) {
        auto resource = fPurgeableQueue.at(i);
        auto resourceTag = resource->getResourceTag();
        if (resourceTag.fWid != 0) {
            updatePurgeableWidMap(resource, purgNameInfoWid, purgSizeInfoWid, purgPidInfoWid, purgCountInfoWid);
        } else if (resourceTag.fPid != 0) {
            updatePurgeablePidMap(resource, purgNameInfoPid, purgSizeInfoPid, purgCountInfoPid);
        } else if (resourceTag.fFid != 0) {
            updatePurgeableFidMap(resource, purgNameInfoFid, purgSizeInfoFid, purgCountInfoFid);
        } else {
            purgCountUnknown++;
            purgSizeUnknown += resource->gpuMemorySize();
        }
    }

    std::string infoStr;
    if (purgSizeInfoWid.size() > 0) {
        infoStr += ";PurgeableInfo_Node:[";
        updatePurgeableWidInfo(infoStr, purgNameInfoWid, purgSizeInfoWid, purgPidInfoWid, purgCountInfoWid);
    }
    if (purgSizeInfoPid.size() > 0) {
        infoStr += ";PurgeableInfo_Pid:[";
        updatePurgeablePidInfo(infoStr, purgNameInfoWid, purgSizeInfoWid, purgCountInfoWid);
    }
    if (purgSizeInfoFid.size() > 0) {
        infoStr += ";PurgeableInfo_Fid:[";
        updatePurgeableFidInfo(infoStr, purgNameInfoFid, purgSizeInfoFid, purgCountInfoFid);
    }
    updatePurgeableUnknownInfo(infoStr, ";PurgeableInfo_Unknown:", purgCountUnknown, purgSizeUnknown);
    return infoStr;
}

std::string GrResourceCache::cacheInfoNoPurgeableQueue()
{
    std::map<uint32_t, int> noPurgSizeInfoWid;
    std::map<uint32_t, int> noPurgCountInfoWid;
    std::map<uint32_t, std::string> noPurgNameInfoWid;
    std::map<uint32_t, int> noPurgPidInfoWid;

    std::map<uint32_t, int> noPurgSizeInfoPid;
    std::map<uint32_t, int> noPurgCountInfoPid;
    std::map<uint32_t, std::string> noPurgNameInfoPid;

    std::map<uint32_t, int> noPurgSizeInfoFid;
    std::map<uint32_t, int> noPurgCountInfoFid;
    std::map<uint32_t, std::string> noPurgNameInfoFid;

    int noPurgCountUnknown = 0;
    int noPurgSizeUnknown = 0;

    for (int i = 0; i < fNonpurgeableResources.count(); i++) {
        auto resource = fNonpurgeableResources[i];
        if (resource == nullptr) {
            continue;
        }
        auto resourceTag = resource->getResourceTag();
        if (resourceTag.fWid != 0) {
            updatePurgeableWidMap(resource, noPurgNameInfoWid, noPurgSizeInfoWid, noPurgPidInfoWid, noPurgCountInfoWid);
        } else if (resourceTag.fPid != 0) {
            updatePurgeablePidMap(resource, noPurgNameInfoPid, noPurgSizeInfoPid, noPurgCountInfoPid);
        } else if (resourceTag.fFid != 0) {
            updatePurgeableFidMap(resource, noPurgNameInfoFid, noPurgSizeInfoFid, noPurgCountInfoFid);
        } else {
            noPurgCountUnknown++;
            noPurgSizeUnknown += resource->gpuMemorySize();
        }
    }

    std::string infoStr;
    if (noPurgSizeInfoWid.size() > 0) {
        infoStr += ";NonPurgeableInfo_Node:[";
        updatePurgeableWidInfo(infoStr, noPurgNameInfoWid, noPurgSizeInfoWid, noPurgPidInfoWid, noPurgCountInfoWid);
    }
    if (noPurgSizeInfoPid.size() > 0) {
        infoStr += ";NonPurgeableInfo_Pid:[";
        updatePurgeablePidInfo(infoStr, noPurgNameInfoPid, noPurgSizeInfoPid, noPurgCountInfoPid);
    }
    if (noPurgSizeInfoFid.size() > 0) {
        infoStr += ";NonPurgeableInfo_Fid:[";
        updatePurgeableFidInfo(infoStr, noPurgNameInfoFid, noPurgSizeInfoFid, noPurgCountInfoFid);
    }
    updatePurgeableUnknownInfo(infoStr, ";NonPurgeableInfo_Unknown:", noPurgCountUnknown, noPurgSizeUnknown);
    return infoStr;
}

size_t GrResourceCache::cacheInfoRealAllocSize()
{
    size_t realAllocImageSize = 0;
    for (int i = 0; i < fPurgeableQueue.count(); i++) {
        auto resource = fPurgeableQueue.at(i);
        if (resource == nullptr || !resource->isRealAlloc()) {
            continue;
        }
        realAllocImageSize += resource->getRealAllocSize();
    }
    for (int i = 0; i < fNonpurgeableResources.count(); i++) {
        auto resource = fNonpurgeableResources[i];
        if (resource == nullptr || !resource->isRealAlloc()) {
            continue;
        }
        realAllocImageSize += resource->getRealAllocSize();
    }
    return realAllocImageSize;
}

std::string GrResourceCache::cacheInfoRealAllocQueue()
{
    std::map<uint32_t, std::string> realAllocNameInfoWid;
    std::map<uint32_t, int> realAllocSizeInfoWid;
    std::map<uint32_t, int> realAllocPidInfoWid;
    std::map<uint32_t, int> realAllocCountInfoWid;

    std::map<uint32_t, std::string> realAllocNameInfoPid;
    std::map<uint32_t, int> realAllocSizeInfoPid;
    std::map<uint32_t, int> realAllocCountInfoPid;

    std::map<uint32_t, std::string> realAllocNameInfoFid;
    std::map<uint32_t, int> realAllocSizeInfoFid;
    std::map<uint32_t, int> realAllocCountInfoFid;

    int realAllocCountUnknown = 0;
    int realAllocSizeUnknown = 0;

    for (int i = 0; i < fNonpurgeableResources.count(); i++) {
        auto resource = fNonpurgeableResources[i];
        if (resource == nullptr || !resource->isRealAlloc()) {
            continue;
        }
        auto resourceTag = resource->getResourceTag();
        if (resourceTag.fWid != 0) {
            updateRealAllocWidMap(
                resource, realAllocNameInfoWid, realAllocSizeInfoWid, realAllocPidInfoWid, realAllocCountInfoWid);
        } else if (resourceTag.fPid != 0) {
            updateRealAllocPidMap(resource, realAllocNameInfoPid, realAllocSizeInfoPid, realAllocCountInfoPid);
        } else if (resourceTag.fFid != 0) {
            updateRealAllocFidMap(resource, realAllocNameInfoFid, realAllocSizeInfoFid, realAllocCountInfoFid);
        } else {
            realAllocCountUnknown++;
            realAllocSizeUnknown += resource->getRealAllocSize();
        }
    }

    for (int i = 0; i < fPurgeableQueue.count(); i++) {
        auto resource = fPurgeableQueue.at(i);
        if (resource == nullptr || !resource->isRealAlloc()) {
            continue;
        }
        auto resourceTag = resource->getResourceTag();
        if (resourceTag.fWid != 0) {
            updateRealAllocWidMap(
                resource, realAllocNameInfoWid, realAllocSizeInfoWid, realAllocPidInfoWid, realAllocCountInfoWid);
        } else if (resourceTag.fPid != 0) {
            updateRealAllocPidMap(resource, realAllocNameInfoPid, realAllocSizeInfoPid, realAllocCountInfoPid);
        } else if (resourceTag.fFid != 0) {
            updateRealAllocFidMap(resource, realAllocNameInfoFid, realAllocSizeInfoFid, realAllocCountInfoFid);
        } else {
            realAllocCountUnknown++;
            realAllocSizeUnknown += resource->getRealAllocSize();
        }
    }

    std::string infoStr;
    if (realAllocSizeInfoWid.size() > 0) {
        infoStr += ";RealAllocInfo_Node:[";
        updatePurgeableWidInfo(
            infoStr, realAllocNameInfoWid, realAllocSizeInfoWid, realAllocPidInfoWid, realAllocCountInfoWid);
    }
    if (realAllocSizeInfoPid.size() > 0) {
        infoStr += ";RealAllocInfo_Pid:[";
        updatePurgeablePidInfo(infoStr, realAllocNameInfoPid, realAllocSizeInfoPid, realAllocCountInfoPid);
    }
    if (realAllocSizeInfoFid.size() > 0) {
        infoStr += ";RealAllocInfo_Fid:[";
        updatePurgeableFidInfo(infoStr, realAllocNameInfoFid, realAllocSizeInfoFid, realAllocCountInfoFid);
    }
    updatePurgeableUnknownInfo(infoStr, ";RealAllocInfo_Unknown:", realAllocCountUnknown, realAllocSizeUnknown);
    return infoStr;
}

std::string GrResourceCache::realBytesOfPid()
{
    std::string infoStr;
    infoStr += ";fBytesOfPid : [";
    if (fBytesOfPid.size() > 0) {
        for (auto it = fBytesOfPid.begin(); it != fBytesOfPid.end(); it++) {
            infoStr += std::to_string(it->first) + ":" + std::to_string(it->second) + ", ";
        }
    }
    infoStr += "]";
    return infoStr;
}

void GrResourceCache::updatePurgeableWidMap(GrGpuResource* resource,
                                            std::map<uint32_t, std::string>& nameInfoWid,
                                            std::map<uint32_t, int>& sizeInfoWid,
                                            std::map<uint32_t, int>& pidInfoWid,
                                            std::map<uint32_t, int>& countInfoWid)
{
    auto resourceTag = resource->getResourceTag();
    auto it = sizeInfoWid.find(resourceTag.fWid);
    if (it != sizeInfoWid.end()) {
        sizeInfoWid[resourceTag.fWid] = it->second + resource->gpuMemorySize();
        countInfoWid[resourceTag.fWid]++;
    } else {
        sizeInfoWid[resourceTag.fWid] = resource->gpuMemorySize();
        nameInfoWid[resourceTag.fWid] = resourceTag.fName;
        pidInfoWid[resourceTag.fWid] = resourceTag.fPid;
        countInfoWid[resourceTag.fWid] = 1;
    }
}

void GrResourceCache::updatePurgeablePidMap(GrGpuResource* resource,
                                            std::map<uint32_t, std::string>& nameInfoPid,
                                            std::map<uint32_t, int>& sizeInfoPid,
                                            std::map<uint32_t, int>& countInfoPid)
{
    auto resourceTag = resource->getResourceTag();
    auto it = sizeInfoPid.find(resourceTag.fPid);
    if (it != sizeInfoPid.end()) {
        sizeInfoPid[resourceTag.fPid] = it->second + resource->gpuMemorySize();
        countInfoPid[resourceTag.fPid]++;
    } else {
        sizeInfoPid[resourceTag.fPid] = resource->gpuMemorySize();
        nameInfoPid[resourceTag.fPid] = resourceTag.fName;
        countInfoPid[resourceTag.fPid] = 1;
    }
}

void GrResourceCache::updatePurgeableFidMap(GrGpuResource* resource,
                                            std::map<uint32_t, std::string>& nameInfoFid,
                                            std::map<uint32_t, int>& sizeInfoFid,
                                            std::map<uint32_t, int>& countInfoFid)
{
    auto resourceTag = resource->getResourceTag();
    auto it = sizeInfoFid.find(resourceTag.fFid);
    if (it != sizeInfoFid.end()) {
        sizeInfoFid[resourceTag.fFid] = it->second + resource->gpuMemorySize();
        countInfoFid[resourceTag.fFid]++;
    } else {
        sizeInfoFid[resourceTag.fFid] = resource->gpuMemorySize();
        nameInfoFid[resourceTag.fFid] = resourceTag.fName;
        countInfoFid[resourceTag.fFid] = 1;
    }
}

void GrResourceCache::updateRealAllocWidMap(GrGpuResource* resource,
                                            std::map<uint32_t, std::string>& nameInfoWid,
                                            std::map<uint32_t, int>& sizeInfoWid,
                                            std::map<uint32_t, int>& pidInfoWid,
                                            std::map<uint32_t, int>& countInfoWid)
{
    size_t size = resource->getRealAllocSize();
    auto resourceTag = resource->getResourceTag();
    auto it = sizeInfoWid.find(resourceTag.fWid);
    if (it != sizeInfoWid.end()) {
        sizeInfoWid[resourceTag.fWid] = it->second + size;
        countInfoWid[resourceTag.fWid]++;
    } else {
        sizeInfoWid[resourceTag.fWid] = size;
        nameInfoWid[resourceTag.fWid] = resourceTag.fName;
        pidInfoWid[resourceTag.fWid] = resourceTag.fPid;
        countInfoWid[resourceTag.fWid] = 1;
    }
}

void GrResourceCache::updateRealAllocPidMap(GrGpuResource* resource,
                                            std::map<uint32_t, std::string>& nameInfoPid,
                                            std::map<uint32_t, int>& sizeInfoPid,
                                            std::map<uint32_t, int>& countInfoPid)
{
    size_t size = resource->getRealAllocSize();
    auto resourceTag = resource->getResourceTag();
    auto it = sizeInfoPid.find(resourceTag.fPid);
    if (it != sizeInfoPid.end()) {
        sizeInfoPid[resourceTag.fPid] = it->second + size;
        countInfoPid[resourceTag.fPid]++;
    } else {
        sizeInfoPid[resourceTag.fPid] = size;
        nameInfoPid[resourceTag.fPid] = resourceTag.fName;
        countInfoPid[resourceTag.fPid] = 1;
    }
}

void GrResourceCache::updateRealAllocFidMap(GrGpuResource* resource,
                                            std::map<uint32_t, std::string>& nameInfoFid,
                                            std::map<uint32_t, int>& sizeInfoFid,
                                            std::map<uint32_t, int>& countInfoFid)
{
    size_t size = resource->getRealAllocSize();
    auto resourceTag = resource->getResourceTag();
    auto it = sizeInfoFid.find(resourceTag.fFid);
    if (it != sizeInfoFid.end()) {
        sizeInfoFid[resourceTag.fFid] = it->second + size;
        countInfoFid[resourceTag.fFid]++;
    } else {
        sizeInfoFid[resourceTag.fFid] = size;
        nameInfoFid[resourceTag.fFid] = resourceTag.fName;
        countInfoFid[resourceTag.fFid] = 1;
    }
}

void GrResourceCache::updatePurgeableWidInfo(std::string& infoStr,
                                             std::map<uint32_t, std::string>& nameInfoWid,
                                             std::map<uint32_t, int>& sizeInfoWid,
                                             std::map<uint32_t, int>& pidInfoWid,
                                             std::map<uint32_t, int>& countInfoWid)
{
    for (auto it = sizeInfoWid.begin(); it != sizeInfoWid.end(); it++) {
        infoStr += "[" + nameInfoWid[it->first] +
            ",pid=" + std::to_string(pidInfoWid[it->first]) +
            ",NodeId=" + std::to_string(it->first & 0xFFFFFFFF) +
            ",count=" + std::to_string(countInfoWid[it->first]) +
            ",size=" + std::to_string(it->second) +
            "(" + std::to_string(it->second / MB) + " MB)],";
    }
    infoStr += ']';
}

void GrResourceCache::updatePurgeablePidInfo(std::string& infoStr,
                                             std::map<uint32_t, std::string>& nameInfoPid,
                                             std::map<uint32_t, int>& sizeInfoPid,
                                             std::map<uint32_t, int>& countInfoPid)
{
    for (auto it = sizeInfoPid.begin(); it != sizeInfoPid.end(); it++) {
        infoStr += "[" + nameInfoPid[it->first] +
            ",pid=" + std::to_string(it->first) +
            ",count=" + std::to_string(countInfoPid[it->first]) +
            ",size=" + std::to_string(it->second) +
            "(" + std::to_string(it->second / MB) + " MB)],";
    }
    infoStr += ']';
}

void GrResourceCache::updatePurgeableFidInfo(std::string& infoStr,
                                             std::map<uint32_t, std::string>& nameInfoFid,
                                             std::map<uint32_t, int>& sizeInfoFid,
                                             std::map<uint32_t, int>& countInfoFid)
{
    for (auto it = sizeInfoFid.begin(); it != sizeInfoFid.end(); it++) {
        infoStr += "[" + nameInfoFid[it->first] +
            ",typeid=" + std::to_string(it->first) +
            ",count=" + std::to_string(countInfoFid[it->first]) +
            ",size=" + std::to_string(it->second) +
            "(" + std::to_string(it->second / MB) + " MB)],";
    }
    infoStr += ']';
}

void GrResourceCache::updatePurgeableUnknownInfo(
    std::string& infoStr, const std::string& unknownPrefix, const int countUnknown, const int sizeUnknown)
{
    if (countUnknown > 0) {
        infoStr += unknownPrefix +
            "[count=" + std::to_string(countUnknown) +
            ",size=" + std::to_string(sizeUnknown) +
            "(" + std::to_string(sizeUnknown / MB) + "MB)]";
    }
}
#endif

void GrResourceCache::insertResource(GrGpuResource* resource)
{
    ASSERT_SINGLE_OWNER
    SkASSERT(resource);
    SkASSERT(!this->isInCache(resource));
    SkASSERT(!resource->wasDestroyed());
    SkASSERT(!resource->resourcePriv().isPurgeable());

    // We must set the timestamp before adding to the array in case the timestamp wraps and we wind
    // up iterating over all the resources that already have timestamps.
    resource->cacheAccess().setTimestamp(this->getNextTimestamp());

    this->addToNonpurgeableArray(resource);

    size_t size = resource->gpuMemorySize();
    SkDEBUGCODE(++fCount;)
    fBytes += size;

    // OH ISSUE: memory count
    auto pid = resource->getResourceTag().fPid;
    if (pid && resource->isRealAlloc()) {
        auto& pidSize = fBytesOfPid[pid];
        pidSize += size;
        fUpdatedBytesOfPid[pid] = pidSize;
        if (pidSize >= fMemoryControl_ && fExitedPid_.find(pid) == fExitedPid_.end() && fMemoryOverflowCallback_) {
            fMemoryOverflowCallback_(pid, pidSize, true);
            fExitedPid_.insert(pid);
            SkDebugf("OHOS resource overflow! pid[%{public}d], size[%{public}zu]", pid, pidSize);
#ifdef SKIA_OHOS_FOR_OHOS_TRACE
            HITRACE_METER_FMT(HITRACE_TAG_GRAPHIC_AGP, "OHOS gpu resource overflow: pid(%d), size:(%zu)",
                pid, pidSize);
#endif
        }
    }

#if GR_CACHE_STATS
    fHighWaterCount = std::max(this->getResourceCount(), fHighWaterCount);
    fHighWaterBytes = std::max(fBytes, fHighWaterBytes);
#endif
    if (GrBudgetedType::kBudgeted == resource->resourcePriv().budgetedType()) {
        ++fBudgetedCount;
        fBudgetedBytes += size;
        TRACE_COUNTER2("skia.gpu.cache", "skia budget", "used",
                       fBudgetedBytes, "free", fMaxBytes - fBudgetedBytes);
#if GR_CACHE_STATS
        fBudgetedHighWaterCount = std::max(fBudgetedCount, fBudgetedHighWaterCount);
        fBudgetedHighWaterBytes = std::max(fBudgetedBytes, fBudgetedHighWaterBytes);
#endif
    }
    SkASSERT(!resource->cacheAccess().isUsableAsScratch());
#ifdef SKIA_OHOS_FOR_OHOS_TRACE
    if (fBudgetedBytes >= fMaxBytes) {
        HITRACE_OHOS_NAME_FMT_ALWAYS("cache over fBudgetedBytes:(%u),fMaxBytes:(%u)", fBudgetedBytes, fMaxBytes);
#ifdef SKIA_DFX_FOR_OHOS
        SimpleCacheInfo simpleCacheInfo;
        traceBeforePurgeUnlockRes("insertResource", simpleCacheInfo);
#endif
        this->purgeAsNeeded();
#ifdef SKIA_DFX_FOR_OHOS
        traceAfterPurgeUnlockRes("insertResource", simpleCacheInfo);
#endif
    } else {
        this->purgeAsNeeded();
    }
#else
    this->purgeAsNeeded();
#endif
}

void GrResourceCache::removeResource(GrGpuResource* resource) {
    ASSERT_SINGLE_OWNER
    this->validate();
    SkASSERT(this->isInCache(resource));

    size_t size = resource->gpuMemorySize();
    if (resource->resourcePriv().isPurgeable()) {
        fPurgeableQueue.remove(resource);
        fPurgeableBytes -= size;
    } else {
        this->removeFromNonpurgeableArray(resource);
    }

    SkDEBUGCODE(--fCount;)
    fBytes -= size;

    // OH ISSUE: memory count
    auto pid = resource->getResourceTag().fPid;
    if (pid && resource->isRealAlloc()) {
        auto& pidSize = fBytesOfPid[pid];
        pidSize -= size;
        fUpdatedBytesOfPid[pid] = pidSize;
        if (pidSize == 0) {
            fBytesOfPid.erase(pid);
        }
    }

    if (GrBudgetedType::kBudgeted == resource->resourcePriv().budgetedType()) {
        --fBudgetedCount;
        fBudgetedBytes -= size;
        TRACE_COUNTER2("skia.gpu.cache", "skia budget", "used",
                       fBudgetedBytes, "free", fMaxBytes - fBudgetedBytes);
    }

    if (resource->cacheAccess().isUsableAsScratch()) {
        fScratchMap.remove(resource->resourcePriv().getScratchKey(), resource);
    }
    if (resource->getUniqueKey().isValid()) {
        fUniqueHash.remove(resource->getUniqueKey());
    }
    this->validate();
}

void GrResourceCache::abandonAll() {
    AutoValidate av(this);

    // We need to make sure to free any resources that were waiting on a free message but never
    // received one.
    fTexturesAwaitingUnref.reset();

    while (fNonpurgeableResources.count()) {
        GrGpuResource* back = *(fNonpurgeableResources.end() - 1);
        SkASSERT(!back->wasDestroyed());
        back->cacheAccess().abandon();
    }

    while (fPurgeableQueue.count()) {
        GrGpuResource* top = fPurgeableQueue.peek();
        SkASSERT(!top->wasDestroyed());
        top->cacheAccess().abandon();
    }

    fThreadSafeCache->dropAllRefs();

    SkASSERT(!fScratchMap.count());
    SkASSERT(!fUniqueHash.count());
    SkASSERT(!fCount);
    SkASSERT(!this->getResourceCount());
    SkASSERT(!fBytes);
    SkASSERT(!fBudgetedCount);
    SkASSERT(!fBudgetedBytes);
    SkASSERT(!fPurgeableBytes);
    SkASSERT(!fTexturesAwaitingUnref.count());
}

void GrResourceCache::releaseAll() {
    AutoValidate av(this);

    fThreadSafeCache->dropAllRefs();

    this->processFreedGpuResources();

    // We need to make sure to free any resources that were waiting on a free message but never
    // received one.
    fTexturesAwaitingUnref.reset();

    SkASSERT(fProxyProvider); // better have called setProxyProvider
    SkASSERT(fThreadSafeCache); // better have called setThreadSafeCache too

    // We must remove the uniqueKeys from the proxies here. While they possess a uniqueKey
    // they also have a raw pointer back to this class (which is presumably going away)!
    fProxyProvider->removeAllUniqueKeys();

    while (fNonpurgeableResources.count()) {
        GrGpuResource* back = *(fNonpurgeableResources.end() - 1);
        SkASSERT(!back->wasDestroyed());
        back->cacheAccess().release();
    }

    while (fPurgeableQueue.count()) {
        GrGpuResource* top = fPurgeableQueue.peek();
        SkASSERT(!top->wasDestroyed());
        top->cacheAccess().release();
    }

    SkASSERT(!fScratchMap.count());
    SkASSERT(!fUniqueHash.count());
    SkASSERT(!fCount);
    SkASSERT(!this->getResourceCount());
    SkASSERT(!fBytes);
    SkASSERT(!fBudgetedCount);
    SkASSERT(!fBudgetedBytes);
    SkASSERT(!fPurgeableBytes);
    SkASSERT(!fTexturesAwaitingUnref.count());
}

void GrResourceCache::releaseByTag(const GrGpuResourceTag& tag) {
    AutoValidate av(this);
    this->processFreedGpuResources();
    SkASSERT(fProxyProvider); // better have called setProxyProvider
    std::vector<GrGpuResource*> recycleVector;
    for (int i = 0; i < fNonpurgeableResources.count(); i++) {
        GrGpuResource* resource = fNonpurgeableResources[i];
        if (tag.filter(resource->getResourceTag())) {
            recycleVector.emplace_back(resource);
            if (resource->getUniqueKey().isValid()) {
                fProxyProvider->processInvalidUniqueKey(resource->getUniqueKey(), nullptr,
                    GrProxyProvider::InvalidateGPUResource::kNo);
            }
        }
    }

    for (int i = 0; i < fPurgeableQueue.count(); i++) {
        GrGpuResource* resource = fPurgeableQueue.at(i);
        if (tag.filter(resource->getResourceTag())) {
            recycleVector.emplace_back(resource);
            if (resource->getUniqueKey().isValid()) {
                fProxyProvider->processInvalidUniqueKey(resource->getUniqueKey(), nullptr,
                    GrProxyProvider::InvalidateGPUResource::kNo);
            }
        }
    }

    for (auto resource : recycleVector) {
        SkASSERT(!resource->wasDestroyed());
        resource->cacheAccess().release();
    }
}

void GrResourceCache::setCurrentGrResourceTag(const GrGpuResourceTag& tag) {
    if (tag.isGrTagValid()) {
        grResourceTagCacheStack.push(tag);
        return;
    }
    if (!grResourceTagCacheStack.empty()) {
        grResourceTagCacheStack.pop();
    }
}

void GrResourceCache::popGrResourceTag()
{
    if (!grResourceTagCacheStack.empty()) {
        grResourceTagCacheStack.pop();
    }
}

GrGpuResourceTag GrResourceCache::getCurrentGrResourceTag() const {
    if (grResourceTagCacheStack.empty()) {
        return{};
    }
    return grResourceTagCacheStack.top();
}

std::set<GrGpuResourceTag> GrResourceCache::getAllGrGpuResourceTags() const {
    std::set<GrGpuResourceTag> result;
    for (int i = 0; i < fNonpurgeableResources.count(); ++i) {
        auto tag = fNonpurgeableResources[i]->getResourceTag();
        result.insert(tag);
    }
    return result;
}

// OH ISSUE: get the memory information of the updated pid.
void GrResourceCache::getUpdatedMemoryMap(std::unordered_map<int32_t, size_t> &out)
{
    fUpdatedBytesOfPid.swap(out);
}

// OH ISSUE: init gpu memory limit.
void GrResourceCache::initGpuMemoryLimit(MemoryOverflowCalllback callback, uint64_t size)
{
    if (fMemoryOverflowCallback_ == nullptr) {
        fMemoryOverflowCallback_ = callback;
        fMemoryControl_ = size;
    }
}

// OH ISSUE: check whether the PID is abnormal.
bool GrResourceCache::isPidAbnormal() const
{
    return fExitedPid_.find(getCurrentGrResourceTag().fPid) != fExitedPid_.end();
}

// OH ISSUE: change the fbyte when the resource tag changes.
void GrResourceCache::changeByteOfPid(int32_t beforePid, int32_t afterPid,
    size_t bytes, bool beforeRealAlloc, bool afterRealAlloc)
{
    if (beforePid && beforeRealAlloc) {
        auto& pidSize = fBytesOfPid[beforePid];
        pidSize -= bytes;
        fUpdatedBytesOfPid[beforePid] = pidSize;
        if (pidSize == 0) {
            fBytesOfPid.erase(beforePid);
        }
    }
    if (afterPid && afterRealAlloc) {
        auto& size = fBytesOfPid[afterPid];
        size += bytes;
        fUpdatedBytesOfPid[afterPid] = size;
    }
}

void GrResourceCache::refResource(GrGpuResource* resource) {
    SkASSERT(resource);
    SkASSERT(resource->getContext()->priv().getResourceCache() == this);
    if (resource->cacheAccess().hasRef()) {
        resource->ref();
    } else {
        this->refAndMakeResourceMRU(resource);
    }
    this->validate();
}

class GrResourceCache::AvailableForScratchUse {
public:
    AvailableForScratchUse() { }

    bool operator()(const GrGpuResource* resource) const {
        // Everything that is in the scratch map should be usable as a
        // scratch resource.
        return true;
    }
};

GrGpuResource* GrResourceCache::findAndRefScratchResource(const GrScratchKey& scratchKey) {
    SkASSERT(scratchKey.isValid());

    GrGpuResource* resource = fScratchMap.find(scratchKey, AvailableForScratchUse());
    if (resource) {
        fScratchMap.remove(scratchKey, resource);
        this->refAndMakeResourceMRU(resource);
        this->validate();
    }
    return resource;
}

void GrResourceCache::willRemoveScratchKey(const GrGpuResource* resource) {
    ASSERT_SINGLE_OWNER
    SkASSERT(resource->resourcePriv().getScratchKey().isValid());
    if (resource->cacheAccess().isUsableAsScratch()) {
        fScratchMap.remove(resource->resourcePriv().getScratchKey(), resource);
    }
}

void GrResourceCache::removeUniqueKey(GrGpuResource* resource) {
    ASSERT_SINGLE_OWNER
    // Someone has a ref to this resource in order to have removed the key. When the ref count
    // reaches zero we will get a ref cnt notification and figure out what to do with it.
    if (resource->getUniqueKey().isValid()) {
        SkASSERT(resource == fUniqueHash.find(resource->getUniqueKey()));
        fUniqueHash.remove(resource->getUniqueKey());
    }
    resource->cacheAccess().removeUniqueKey();
    if (resource->cacheAccess().isUsableAsScratch()) {
        fScratchMap.insert(resource->resourcePriv().getScratchKey(), resource);
    }

    // Removing a unique key from a kUnbudgetedCacheable resource would make the resource
    // require purging. However, the resource must be ref'ed to get here and therefore can't
    // be purgeable. We'll purge it when the refs reach zero.
    SkASSERT(!resource->resourcePriv().isPurgeable());
    this->validate();
}

void GrResourceCache::changeUniqueKey(GrGpuResource* resource, const GrUniqueKey& newKey) {
    ASSERT_SINGLE_OWNER
    SkASSERT(resource);
    SkASSERT(this->isInCache(resource));

    // If another resource has the new key, remove its key then install the key on this resource.
    if (newKey.isValid()) {
        if (GrGpuResource* old = fUniqueHash.find(newKey)) {
            // If the old resource using the key is purgeable and is unreachable, then remove it.
            if (!old->resourcePriv().getScratchKey().isValid() &&
                old->resourcePriv().isPurgeable()) {
                old->cacheAccess().release();
            } else {
                // removeUniqueKey expects an external owner of the resource.
                this->removeUniqueKey(sk_ref_sp(old).get());
            }
        }
        SkASSERT(nullptr == fUniqueHash.find(newKey));

        // Remove the entry for this resource if it already has a unique key.
        if (resource->getUniqueKey().isValid()) {
            SkASSERT(resource == fUniqueHash.find(resource->getUniqueKey()));
            fUniqueHash.remove(resource->getUniqueKey());
            SkASSERT(nullptr == fUniqueHash.find(resource->getUniqueKey()));
        } else {
            // 'resource' didn't have a valid unique key before so it is switching sides. Remove it
            // from the ScratchMap. The isUsableAsScratch call depends on us not adding the new
            // unique key until after this check.
            if (resource->cacheAccess().isUsableAsScratch()) {
                fScratchMap.remove(resource->resourcePriv().getScratchKey(), resource);
            }
        }

        resource->cacheAccess().setUniqueKey(newKey);
        fUniqueHash.add(resource);
    } else {
        this->removeUniqueKey(resource);
    }

    this->validate();
}

void GrResourceCache::refAndMakeResourceMRU(GrGpuResource* resource) {
    ASSERT_SINGLE_OWNER
    SkASSERT(resource);
    SkASSERT(this->isInCache(resource));

    if (resource->resourcePriv().isPurgeable()) {
        // It's about to become unpurgeable.
        fPurgeableBytes -= resource->gpuMemorySize();
        fPurgeableQueue.remove(resource);
        this->addToNonpurgeableArray(resource);
    } else if (!resource->cacheAccess().hasRefOrCommandBufferUsage() &&
               resource->resourcePriv().budgetedType() == GrBudgetedType::kBudgeted) {
        SkASSERT(fNumBudgetedResourcesFlushWillMakePurgeable > 0);
        fNumBudgetedResourcesFlushWillMakePurgeable--;
    }
    resource->cacheAccess().ref();

    resource->cacheAccess().setTimestamp(this->getNextTimestamp());
    this->validate();
}

void GrResourceCache::notifyARefCntReachedZero(GrGpuResource* resource,
                                               GrGpuResource::LastRemovedRef removedRef) {
    ASSERT_SINGLE_OWNER
    SkASSERT(resource);
    SkASSERT(!resource->wasDestroyed());
    SkASSERT(this->isInCache(resource));
    // This resource should always be in the nonpurgeable array when this function is called. It
    // will be moved to the queue if it is newly purgeable.
    SkASSERT(fNonpurgeableResources[*resource->cacheAccess().accessCacheIndex()] == resource);

    if (removedRef == GrGpuResource::LastRemovedRef::kMainRef) {
        if (resource->cacheAccess().isUsableAsScratch()) {
            fScratchMap.insert(resource->resourcePriv().getScratchKey(), resource);
        }
    }

    if (resource->cacheAccess().hasRefOrCommandBufferUsage()) {
        this->validate();
        return;
    }

#ifdef SK_DEBUG
    // When the timestamp overflows validate() is called. validate() checks that resources in
    // the nonpurgeable array are indeed not purgeable. However, the movement from the array to
    // the purgeable queue happens just below in this function. So we mark it as an exception.
    if (resource->resourcePriv().isPurgeable()) {
        fNewlyPurgeableResourceForValidation = resource;
    }
#endif
    resource->cacheAccess().setTimestamp(this->getNextTimestamp());
    SkDEBUGCODE(fNewlyPurgeableResourceForValidation = nullptr);

    if (!resource->resourcePriv().isPurgeable() &&
        resource->resourcePriv().budgetedType() == GrBudgetedType::kBudgeted) {
        ++fNumBudgetedResourcesFlushWillMakePurgeable;
    }

    if (!resource->resourcePriv().isPurgeable()) {
        this->validate();
        return;
    }

    this->removeFromNonpurgeableArray(resource);
    fPurgeableQueue.insert(resource);
    resource->cacheAccess().setTimeWhenResourceBecomePurgeable();
    fPurgeableBytes += resource->gpuMemorySize();

    bool hasUniqueKey = resource->getUniqueKey().isValid();

    GrBudgetedType budgetedType = resource->resourcePriv().budgetedType();

    if (budgetedType == GrBudgetedType::kBudgeted) {
        // Purge the resource immediately if we're over budget
        // Also purge if the resource has neither a valid scratch key nor a unique key.
        bool hasKey = resource->resourcePriv().getScratchKey().isValid() || hasUniqueKey;
        if (!this->overBudget() && hasKey) {
            return;
        }
    } else {
        // We keep unbudgeted resources with a unique key in the purgeable queue of the cache so
        // they can be reused again by the image connected to the unique key.
        if (hasUniqueKey && budgetedType == GrBudgetedType::kUnbudgetedCacheable) {
            return;
        }
        // Check whether this resource could still be used as a scratch resource.
        if (!resource->resourcePriv().refsWrappedObjects() &&
            resource->resourcePriv().getScratchKey().isValid()) {
            // We won't purge an existing resource to make room for this one.
            if (this->wouldFit(resource->gpuMemorySize())) {
                resource->resourcePriv().makeBudgeted();
                return;
            }
        }
    }

    SkDEBUGCODE(int beforeCount = this->getResourceCount();)
    resource->cacheAccess().release();
    // We should at least free this resource, perhaps dependent resources as well.
    SkASSERT(this->getResourceCount() < beforeCount);
    this->validate();
}

void GrResourceCache::didChangeBudgetStatus(GrGpuResource* resource) {
    ASSERT_SINGLE_OWNER
    SkASSERT(resource);
    SkASSERT(this->isInCache(resource));

    size_t size = resource->gpuMemorySize();
    // Changing from BudgetedType::kUnbudgetedCacheable to another budgeted type could make
    // resource become purgeable. However, we should never allow that transition. Wrapped
    // resources are the only resources that can be in that state and they aren't allowed to
    // transition from one budgeted state to another.
    SkDEBUGCODE(bool wasPurgeable = resource->resourcePriv().isPurgeable());
    if (resource->resourcePriv().budgetedType() == GrBudgetedType::kBudgeted) {
        ++fBudgetedCount;
        fBudgetedBytes += size;
#if GR_CACHE_STATS
        fBudgetedHighWaterBytes = std::max(fBudgetedBytes, fBudgetedHighWaterBytes);
        fBudgetedHighWaterCount = std::max(fBudgetedCount, fBudgetedHighWaterCount);
#endif
        if (!resource->resourcePriv().isPurgeable() &&
            !resource->cacheAccess().hasRefOrCommandBufferUsage()) {
            ++fNumBudgetedResourcesFlushWillMakePurgeable;
        }
        if (resource->cacheAccess().isUsableAsScratch()) {
            fScratchMap.insert(resource->resourcePriv().getScratchKey(), resource);
        }
        this->purgeAsNeeded();
    } else {
        SkASSERT(resource->resourcePriv().budgetedType() != GrBudgetedType::kUnbudgetedCacheable);
        --fBudgetedCount;
        fBudgetedBytes -= size;
        if (!resource->resourcePriv().isPurgeable() &&
            !resource->cacheAccess().hasRefOrCommandBufferUsage()) {
            --fNumBudgetedResourcesFlushWillMakePurgeable;
        }
        if (!resource->cacheAccess().hasRef() && !resource->getUniqueKey().isValid() &&
            resource->resourcePriv().getScratchKey().isValid()) {
            fScratchMap.remove(resource->resourcePriv().getScratchKey(), resource);
        }
    }
    SkASSERT(wasPurgeable == resource->resourcePriv().isPurgeable());
    TRACE_COUNTER2("skia.gpu.cache", "skia budget", "used",
                   fBudgetedBytes, "free", fMaxBytes - fBudgetedBytes);

    this->validate();
}

static constexpr int timeUnit = 1000;

// OH ISSUE: allow access to release interface
bool GrResourceCache::allowToPurge(const std::function<bool(void)>& nextFrameHasArrived)
{
    if (!fEnabled) {
        return true;
    }
    if (fFrameInfo.duringFrame == 0) {
        if (nextFrameHasArrived && nextFrameHasArrived()) {
            return false;
        }
        return true;
    }
    if (fFrameInfo.frameCount != fLastFrameCount) { // the next frame arrives
        struct timespec startTime = {0, 0};
        if (clock_gettime(CLOCK_REALTIME, &startTime) == -1) {
            return true;
        }
        fStartTime = startTime.tv_sec * timeUnit * timeUnit + startTime.tv_nsec / timeUnit;
        fLastFrameCount = fFrameInfo.frameCount;
        return true;
    }
    struct timespec endTime = {0, 0};
    if (clock_gettime(CLOCK_REALTIME, &endTime) == -1) {
        return true;
    }
    if (((endTime.tv_sec * timeUnit * timeUnit + endTime.tv_nsec / timeUnit) - fStartTime) >= fOvertimeDuration) {
        return false;
    }
    return true;
}

void GrResourceCache::purgeAsNeeded(const std::function<bool(void)>& nextFrameHasArrived) {
    SkTArray<GrUniqueKeyInvalidatedMessage> invalidKeyMsgs;
    fInvalidUniqueKeyInbox.poll(&invalidKeyMsgs);
    if (invalidKeyMsgs.count()) {
        SkASSERT(fProxyProvider);

        for (int i = 0; i < invalidKeyMsgs.count(); ++i) {
            if (invalidKeyMsgs[i].inThreadSafeCache()) {
                fThreadSafeCache->remove(invalidKeyMsgs[i].key());
                SkASSERT(!fThreadSafeCache->has(invalidKeyMsgs[i].key()));
            } else {
                fProxyProvider->processInvalidUniqueKey(
                                                    invalidKeyMsgs[i].key(), nullptr,
                                                    GrProxyProvider::InvalidateGPUResource::kYes);
                SkASSERT(!this->findAndRefUniqueResource(invalidKeyMsgs[i].key()));
            }
        }
    }

    this->processFreedGpuResources();

    bool stillOverbudget = this->overBudget(nextFrameHasArrived);
    while (stillOverbudget && fPurgeableQueue.count() && this->allowToPurge(nextFrameHasArrived)) {
        GrGpuResource* resource = fPurgeableQueue.peek();
        SkASSERT(resource->resourcePriv().isPurgeable());
        resource->cacheAccess().release();
        stillOverbudget = this->overBudget(nextFrameHasArrived);
    }

    if (stillOverbudget) {
        fThreadSafeCache->dropUniqueRefs(this);

        stillOverbudget = this->overBudget(nextFrameHasArrived);
        while (stillOverbudget && fPurgeableQueue.count() && this->allowToPurge(nextFrameHasArrived)) {
            GrGpuResource* resource = fPurgeableQueue.peek();
            SkASSERT(resource->resourcePriv().isPurgeable());
            resource->cacheAccess().release();
            stillOverbudget = this->overBudget(nextFrameHasArrived);
        }
    }

    this->validate();
}

void GrResourceCache::purgeUnlockedResources(const GrStdSteadyClock::time_point* purgeTime,
                                             bool scratchResourcesOnly) {
#if defined (SKIA_OHOS_FOR_OHOS_TRACE) && defined (SKIA_DFX_FOR_OHOS)
    SimpleCacheInfo simpleCacheInfo;
    traceBeforePurgeUnlockRes("purgeUnlockedResources", simpleCacheInfo);
#endif
    if (!scratchResourcesOnly) {
        if (purgeTime) {
            fThreadSafeCache->dropUniqueRefsOlderThan(*purgeTime);
        } else {
            fThreadSafeCache->dropUniqueRefs(nullptr);
        }

        // We could disable maintaining the heap property here, but it would add a lot of
        // complexity. Moreover, this is rarely called.
        while (fPurgeableQueue.count()) {
            GrGpuResource* resource = fPurgeableQueue.peek();

            const GrStdSteadyClock::time_point resourceTime =
                    resource->cacheAccess().timeWhenResourceBecamePurgeable();
            if (purgeTime && resourceTime >= *purgeTime) {
                // Resources were given both LRU timestamps and tagged with a frame number when
                // they first became purgeable. The LRU timestamp won't change again until the
                // resource is made non-purgeable again. So, at this point all the remaining
                // resources in the timestamp-sorted queue will have a frame number >= to this
                // one.
                break;
            }

            SkASSERT(resource->resourcePriv().isPurgeable());
            resource->cacheAccess().release();
        }
    } else {
        // Early out if the very first item is too new to purge to avoid sorting the queue when
        // nothing will be deleted.
        if (purgeTime && fPurgeableQueue.count() &&
            fPurgeableQueue.peek()->cacheAccess().timeWhenResourceBecamePurgeable() >= *purgeTime) {
#if defined (SKIA_OHOS_FOR_OHOS_TRACE) && defined (SKIA_DFX_FOR_OHOS)
            traceAfterPurgeUnlockRes("purgeUnlockedResources", simpleCacheInfo);
#endif
            return;
        }

        // Sort the queue
        fPurgeableQueue.sort();

        // Make a list of the scratch resources to delete
        SkTDArray<GrGpuResource*> scratchResources;
        for (int i = 0; i < fPurgeableQueue.count(); i++) {
            GrGpuResource* resource = fPurgeableQueue.at(i);

            const GrStdSteadyClock::time_point resourceTime =
                    resource->cacheAccess().timeWhenResourceBecamePurgeable();
            if (purgeTime && resourceTime >= *purgeTime) {
                // scratch or not, all later iterations will be too recently used to purge.
                break;
            }
            SkASSERT(resource->resourcePriv().isPurgeable());
            if (!resource->getUniqueKey().isValid()) {
                *scratchResources.append() = resource;
            }
        }

        // Delete the scratch resources. This must be done as a separate pass
        // to avoid messing up the sorted order of the queue
        for (int i = 0; i < scratchResources.count(); i++) {
            scratchResources.getAt(i)->cacheAccess().release();
        }
    }

    this->validate();
#if defined (SKIA_OHOS_FOR_OHOS_TRACE) && defined (SKIA_DFX_FOR_OHOS)
    traceAfterPurgeUnlockRes("purgeUnlockedResources", simpleCacheInfo);
#endif
}

void GrResourceCache::purgeUnlockAndSafeCacheGpuResources() {
#if defined (SKIA_OHOS_FOR_OHOS_TRACE) && defined (SKIA_DFX_FOR_OHOS)
    SimpleCacheInfo simpleCacheInfo;
    traceBeforePurgeUnlockRes("purgeUnlockAndSafeCacheGpuResources", simpleCacheInfo);
#endif
    fThreadSafeCache->dropUniqueRefs(nullptr);
    // Sort the queue
    fPurgeableQueue.sort();

    //Make a list of the scratch resources to delete
    SkTDArray<GrGpuResource*> scratchResources;
    for (int i = 0; i < fPurgeableQueue.count(); i++) {
        GrGpuResource* resource = fPurgeableQueue.at(i);
        if (!resource) {
            continue;
        }
        SkASSERT(resource->resourcePriv().isPurgeable());
        if (!resource->getUniqueKey().isValid()) {
            *scratchResources.append() = resource;
        }
    }

    //Delete the scatch resource. This must be done as a separate pass
    //to avoid messing up the sorted order of the queue
    for (int i = 0; i <scratchResources.count(); i++) {
        scratchResources.getAt(i)->cacheAccess().release();
    }

    this->validate();
#if defined (SKIA_OHOS_FOR_OHOS_TRACE) && defined (SKIA_DFX_FOR_OHOS)
    traceAfterPurgeUnlockRes("purgeUnlockAndSafeCacheGpuResources", simpleCacheInfo);
#endif
}

// OH ISSUE: suppress release window
void GrResourceCache::suppressGpuCacheBelowCertainRatio(const std::function<bool(void)>& nextFrameHasArrived)
{
    if (!fEnabled) {
        return;
    }
    this->purgeAsNeeded(nextFrameHasArrived);
}

void GrResourceCache::purgeCacheBetweenFrames(bool scratchResourcesOnly, const std::set<int>& exitedPidSet,
        const std::set<int>& protectedPidSet) {
    HITRACE_OHOS_NAME_FMT_ALWAYS("PurgeGrResourceCache cur=%d, limit=%d", fBudgetedBytes, fMaxBytes);
    if (exitedPidSet.size() > 1) {
        for (int i = 1; i < fPurgeableQueue.count(); i++) {
            GrGpuResource* resource = fPurgeableQueue.at(i);
            SkASSERT(resource->resourcePriv().isPurgeable());
            if (exitedPidSet.find(resource->getResourceTag().fPid) != exitedPidSet.end()) {
                resource->cacheAccess().release();
                this->validate();
                return;
            }
        }
    }
    fPurgeableQueue.sort();
    const char* softLimitPercentage = "0.9";
    #ifdef NOT_BUILD_FOR_OHOS_SDK
    static int softLimit = 
        std::atof(OHOS::system::GetParameter("persist.sys.graphic.mem.soft_limit", 
        softLimitPercentage).c_str()) * fMaxBytes;
    #else
    static int softLimit = 0.9 * fMaxBytes;
    #endif
    if (fBudgetedBytes >= softLimit) {
        for (int i=0; i < fPurgeableQueue.count(); i++) {
            GrGpuResource* resource = fPurgeableQueue.at(i);
            SkASSERT(resource->resourcePriv().isPurgeable());
            if (protectedPidSet.find(resource->getResourceTag().fPid) == protectedPidSet.end()
                && (!scratchResourcesOnly || !resource->getUniqueKey().isValid())) {
                resource->cacheAccess().release();
                this->validate();
                return;
            }
        }
    }
}

void GrResourceCache::purgeUnlockedResourcesByPid(bool scratchResourceOnly, const std::set<int>& exitedPidSet) {
#if defined (SKIA_OHOS_FOR_OHOS_TRACE) && defined (SKIA_DFX_FOR_OHOS)
    SimpleCacheInfo simpleCacheInfo;
    traceBeforePurgeUnlockRes("purgeUnlockedResourcesByPid", simpleCacheInfo);
#endif
    // Sort the queue
    fPurgeableQueue.sort();

    //Make lists of the need purged resources to delete
    fThreadSafeCache->dropUniqueRefs(nullptr);
    SkTDArray<GrGpuResource*> exitPidResources;
    SkTDArray<GrGpuResource*> scratchResources;
    for (int i = 0; i < fPurgeableQueue.count(); i++) {
        GrGpuResource* resource = fPurgeableQueue.at(i);
        if (!resource) {
            continue;
        }
        SkASSERT(resource->resourcePriv().isPurgeable());
        if (exitedPidSet.count(resource->getResourceTag().fPid)) {
            *exitPidResources.append() = resource;
        } else if (!resource->getUniqueKey().isValid()) {
            *scratchResources.append() = resource;
        }
    }

    //Delete the exited pid and scatch resource. This must be done as a separate pass
    //to avoid messing up the sorted order of the queue
    for (int i = 0; i <exitPidResources.count(); i++) {
        exitPidResources.getAt(i)->cacheAccess().release();
    }
    for (int i = 0; i <scratchResources.count(); i++) {
        scratchResources.getAt(i)->cacheAccess().release();
    }

    for (auto pid : exitedPidSet) {
        fExitedPid_.erase(pid);
    }

    this->validate();
#if defined (SKIA_OHOS_FOR_OHOS_TRACE) && defined (SKIA_DFX_FOR_OHOS)
    traceAfterPurgeUnlockRes("purgeUnlockedResourcesByPid", simpleCacheInfo);
#endif
}

void GrResourceCache::purgeUnlockedResourcesByTag(bool scratchResourcesOnly, const GrGpuResourceTag& tag) {
    // Sort the queue
    fPurgeableQueue.sort();

    //Make a list of the scratch resources to delete
    SkTDArray<GrGpuResource*> scratchResources;
    for (int i = 0; i < fPurgeableQueue.count(); i++) {
        GrGpuResource* resource = fPurgeableQueue.at(i);
        SkASSERT(resource->resourcePriv().isPurgeable());
        if (tag.filter(resource->getResourceTag()) && (!scratchResourcesOnly || !resource->getUniqueKey().isValid())) {
            *scratchResources.append() = resource;
        }
    }

    //Delete the scatch resource. This must be done as a separate pass
    //to avoid messing up the sorted order of the queue
    for (int i = 0; i <scratchResources.count(); i++) {
        scratchResources.getAt(i)->cacheAccess().release();
    }

    this->validate();
}

bool GrResourceCache::purgeToMakeHeadroom(size_t desiredHeadroomBytes) {
    AutoValidate av(this);
    if (desiredHeadroomBytes > fMaxBytes) {
        return false;
    }
    if (this->wouldFit(desiredHeadroomBytes)) {
        return true;
    }
    fPurgeableQueue.sort();

    size_t projectedBudget = fBudgetedBytes;
    int purgeCnt = 0;
    for (int i = 0; i < fPurgeableQueue.count(); i++) {
        GrGpuResource* resource = fPurgeableQueue.at(i);
        if (GrBudgetedType::kBudgeted == resource->resourcePriv().budgetedType()) {
            projectedBudget -= resource->gpuMemorySize();
        }
        if (projectedBudget + desiredHeadroomBytes <= fMaxBytes) {
            purgeCnt = i + 1;
            break;
        }
    }
    if (purgeCnt == 0) {
        return false;
    }

    // Success! Release the resources.
    // Copy to array first so we don't mess with the queue.
    std::vector<GrGpuResource*> resources;
    resources.reserve(purgeCnt);
    for (int i = 0; i < purgeCnt; i++) {
        resources.push_back(fPurgeableQueue.at(i));
    }
    for (GrGpuResource* resource : resources) {
        resource->cacheAccess().release();
    }
    return true;
}

void GrResourceCache::purgeUnlockedResources(size_t bytesToPurge, bool preferScratchResources) {

    const size_t tmpByteBudget = std::max((size_t)0, fBytes - bytesToPurge);
    bool stillOverbudget = tmpByteBudget < fBytes;

    if (preferScratchResources && bytesToPurge < fPurgeableBytes) {
        // Sort the queue
        fPurgeableQueue.sort();

        // Make a list of the scratch resources to delete
        SkTDArray<GrGpuResource*> scratchResources;
        size_t scratchByteCount = 0;
        for (int i = 0; i < fPurgeableQueue.count() && stillOverbudget; i++) {
            GrGpuResource* resource = fPurgeableQueue.at(i);
            SkASSERT(resource->resourcePriv().isPurgeable());
            if (!resource->getUniqueKey().isValid()) {
                *scratchResources.append() = resource;
                scratchByteCount += resource->gpuMemorySize();
                stillOverbudget = tmpByteBudget < fBytes - scratchByteCount;
            }
        }

        // Delete the scratch resources. This must be done as a separate pass
        // to avoid messing up the sorted order of the queue
        for (int i = 0; i < scratchResources.count(); i++) {
            scratchResources.getAt(i)->cacheAccess().release();
        }
        stillOverbudget = tmpByteBudget < fBytes;

        this->validate();
    }

    // Purge any remaining resources in LRU order
    if (stillOverbudget) {
        const size_t cachedByteCount = fMaxBytes;
        fMaxBytes = tmpByteBudget;
        this->purgeAsNeeded();
        fMaxBytes = cachedByteCount;
    }
}

bool GrResourceCache::requestsFlush() const {
    return this->overBudget() && !fPurgeableQueue.count() &&
           fNumBudgetedResourcesFlushWillMakePurgeable > 0;
}

void GrResourceCache::insertDelayedTextureUnref(GrTexture* texture) {
    texture->ref();
    uint32_t id = texture->uniqueID().asUInt();
    if (auto* data = fTexturesAwaitingUnref.find(id)) {
        data->addRef();
    } else {
        fTexturesAwaitingUnref.set(id, {texture});
    }
}

void GrResourceCache::processFreedGpuResources() {
    if (!fTexturesAwaitingUnref.count()) {
        return;
    }

    SkTArray<GrTextureFreedMessage> msgs;
    fFreedTextureInbox.poll(&msgs);
    for (int i = 0; i < msgs.count(); ++i) {
        SkASSERT(msgs[i].fIntendedRecipient == fOwningContextID);
        uint32_t id = msgs[i].fTexture->uniqueID().asUInt();
        TextureAwaitingUnref* info = fTexturesAwaitingUnref.find(id);
        // If the GrContext was released or abandoned then fTexturesAwaitingUnref should have been
        // empty and we would have returned early above. Thus, any texture from a message should be
        // in the list of fTexturesAwaitingUnref.
        SkASSERT(info);
        info->unref();
        if (info->finished()) {
            fTexturesAwaitingUnref.remove(id);
        }
    }
}

void GrResourceCache::addToNonpurgeableArray(GrGpuResource* resource) {
    int index = fNonpurgeableResources.count();
    *fNonpurgeableResources.append() = resource;
    *resource->cacheAccess().accessCacheIndex() = index;
}

void GrResourceCache::removeFromNonpurgeableArray(GrGpuResource* resource) {
    int* index = resource->cacheAccess().accessCacheIndex();
    // Fill the hole we will create in the array with the tail object, adjust its index, and
    // then pop the array
    GrGpuResource* tail = *(fNonpurgeableResources.end() - 1);
    SkASSERT(fNonpurgeableResources[*index] == resource);
    fNonpurgeableResources[*index] = tail;
    *tail->cacheAccess().accessCacheIndex() = *index;
    fNonpurgeableResources.pop();
    SkDEBUGCODE(*index = -1);
}

uint32_t GrResourceCache::getNextTimestamp() {
    // If we wrap then all the existing resources will appear older than any resources that get
    // a timestamp after the wrap.
    if (0 == fTimestamp) {
        int count = this->getResourceCount();
        if (count) {
            // Reset all the timestamps. We sort the resources by timestamp and then assign
            // sequential timestamps beginning with 0. This is O(n*lg(n)) but it should be extremely
            // rare.
            SkTDArray<GrGpuResource*> sortedPurgeableResources;
            sortedPurgeableResources.setReserve(fPurgeableQueue.count());

            while (fPurgeableQueue.count()) {
                *sortedPurgeableResources.append() = fPurgeableQueue.peek();
                fPurgeableQueue.pop();
            }

            SkTQSort(fNonpurgeableResources.begin(), fNonpurgeableResources.end(),
                     CompareTimestamp);

            // Pick resources out of the purgeable and non-purgeable arrays based on lowest
            // timestamp and assign new timestamps.
            int currP = 0;
            int currNP = 0;
            while (currP < sortedPurgeableResources.count() &&
                   currNP < fNonpurgeableResources.count()) {
                uint32_t tsP = sortedPurgeableResources[currP]->cacheAccess().timestamp();
                uint32_t tsNP = fNonpurgeableResources[currNP]->cacheAccess().timestamp();
                SkASSERT(tsP != tsNP);
                if (tsP < tsNP) {
                    sortedPurgeableResources[currP++]->cacheAccess().setTimestamp(fTimestamp++);
                } else {
                    // Correct the index in the nonpurgeable array stored on the resource post-sort.
                    *fNonpurgeableResources[currNP]->cacheAccess().accessCacheIndex() = currNP;
                    fNonpurgeableResources[currNP++]->cacheAccess().setTimestamp(fTimestamp++);
                }
            }

            // The above loop ended when we hit the end of one array. Finish the other one.
            while (currP < sortedPurgeableResources.count()) {
                sortedPurgeableResources[currP++]->cacheAccess().setTimestamp(fTimestamp++);
            }
            while (currNP < fNonpurgeableResources.count()) {
                *fNonpurgeableResources[currNP]->cacheAccess().accessCacheIndex() = currNP;
                fNonpurgeableResources[currNP++]->cacheAccess().setTimestamp(fTimestamp++);
            }

            // Rebuild the queue.
            for (int i = 0; i < sortedPurgeableResources.count(); ++i) {
                fPurgeableQueue.insert(sortedPurgeableResources[i]);
            }

            this->validate();
            SkASSERT(count == this->getResourceCount());

            // count should be the next timestamp we return.
            SkASSERT(fTimestamp == SkToU32(count));
        }
    }
    return fTimestamp++;
}

void GrResourceCache::dumpMemoryStatistics(SkTraceMemoryDump* traceMemoryDump) const {
    SkTDArray<GrGpuResource*> resources;
    for (int i = 0; i < fNonpurgeableResources.count(); ++i) {
        *resources.append() = fNonpurgeableResources[i];
    }
    for (int i = 0; i < fPurgeableQueue.count(); ++i) {
        *resources.append() = fPurgeableQueue.at(i);
    }
    for (int i = 0; i < resources.count(); i++) {
        auto resource = resources.getAt(i);
        if (!resource || resource->wasDestroyed()) {
            continue;
        }
        resource->dumpMemoryStatistics(traceMemoryDump);
    }
}

void GrResourceCache::dumpMemoryStatistics(SkTraceMemoryDump* traceMemoryDump, const GrGpuResourceTag& tag) const {
    for (int i = 0; i < fNonpurgeableResources.count(); ++i) {
        if (tag.filter(fNonpurgeableResources[i]->getResourceTag())) {
            fNonpurgeableResources[i]->dumpMemoryStatistics(traceMemoryDump);
        }
    }
    for (int i = 0; i < fPurgeableQueue.count(); ++i) {
        if (tag.filter(fPurgeableQueue.at(i)->getResourceTag())) {
            fPurgeableQueue.at(i)->dumpMemoryStatistics(traceMemoryDump);
        }
    }
}

#if GR_CACHE_STATS
void GrResourceCache::getStats(Stats* stats) const {
    stats->reset();

    stats->fTotal = this->getResourceCount();
    stats->fNumNonPurgeable = fNonpurgeableResources.count();
    stats->fNumPurgeable = fPurgeableQueue.count();

    for (int i = 0; i < fNonpurgeableResources.count(); ++i) {
        stats->update(fNonpurgeableResources[i]);
    }
    for (int i = 0; i < fPurgeableQueue.count(); ++i) {
        stats->update(fPurgeableQueue.at(i));
    }
}

#if GR_TEST_UTILS
void GrResourceCache::dumpStats(SkString* out) const {
    this->validate();

    Stats stats;

    this->getStats(&stats);

    float byteUtilization = (100.f * fBudgetedBytes) / fMaxBytes;

    out->appendf("Budget: %d bytes\n", (int)fMaxBytes);
    out->appendf("\t\tEntry Count: current %d"
                 " (%d budgeted, %d wrapped, %d locked, %d scratch), high %d\n",
                 stats.fTotal, fBudgetedCount, stats.fWrapped, stats.fNumNonPurgeable,
                 stats.fScratch, fHighWaterCount);
    out->appendf("\t\tEntry Bytes: current %d (budgeted %d, %.2g%% full, %d unbudgeted) high %d\n",
                 SkToInt(fBytes), SkToInt(fBudgetedBytes), byteUtilization,
                 SkToInt(stats.fUnbudgetedSize), SkToInt(fHighWaterBytes));
}

void GrResourceCache::dumpStatsKeyValuePairs(SkTArray<SkString>* keys,
                                             SkTArray<double>* values) const {
    this->validate();

    Stats stats;
    this->getStats(&stats);

    keys->push_back(SkString("gpu_cache_purgable_entries")); values->push_back(stats.fNumPurgeable);
}
#endif // GR_TEST_UTILS
#endif // GR_CACHE_STATS

#ifdef SK_DEBUG
void GrResourceCache::validate() const {
    // Reduce the frequency of validations for large resource counts.
    static SkRandom gRandom;
    int mask = (SkNextPow2(fCount + 1) >> 5) - 1;
    if (~mask && (gRandom.nextU() & mask)) {
        return;
    }

    struct Stats {
        size_t fBytes;
        int fBudgetedCount;
        size_t fBudgetedBytes;
        int fLocked;
        int fScratch;
        int fCouldBeScratch;
        int fContent;
        const ScratchMap* fScratchMap;
        const UniqueHash* fUniqueHash;

        Stats(const GrResourceCache* cache) {
            memset(this, 0, sizeof(*this));
            fScratchMap = &cache->fScratchMap;
            fUniqueHash = &cache->fUniqueHash;
        }

        void update(GrGpuResource* resource) {
            fBytes += resource->gpuMemorySize();

            if (!resource->resourcePriv().isPurgeable()) {
                ++fLocked;
            }

            const GrScratchKey& scratchKey = resource->resourcePriv().getScratchKey();
            const GrUniqueKey& uniqueKey = resource->getUniqueKey();

            if (resource->cacheAccess().isUsableAsScratch()) {
                SkASSERT(!uniqueKey.isValid());
                SkASSERT(GrBudgetedType::kBudgeted == resource->resourcePriv().budgetedType());
                SkASSERT(!resource->cacheAccess().hasRef());
                ++fScratch;
                SkASSERT(fScratchMap->countForKey(scratchKey));
                SkASSERT(!resource->resourcePriv().refsWrappedObjects());
            } else if (scratchKey.isValid()) {
                SkASSERT(GrBudgetedType::kBudgeted != resource->resourcePriv().budgetedType() ||
                         uniqueKey.isValid() || resource->cacheAccess().hasRef());
                SkASSERT(!resource->resourcePriv().refsWrappedObjects());
                SkASSERT(!fScratchMap->has(resource, scratchKey));
            }
            if (uniqueKey.isValid()) {
                ++fContent;
                SkASSERT(fUniqueHash->find(uniqueKey) == resource);
                SkASSERT(GrBudgetedType::kBudgeted == resource->resourcePriv().budgetedType() ||
                         resource->resourcePriv().refsWrappedObjects());
            }

            if (GrBudgetedType::kBudgeted == resource->resourcePriv().budgetedType()) {
                ++fBudgetedCount;
                fBudgetedBytes += resource->gpuMemorySize();
            }
        }
    };

    {
        int count = 0;
        fScratchMap.foreach([&](const GrGpuResource& resource) {
            SkASSERT(resource.cacheAccess().isUsableAsScratch());
            count++;
        });
        SkASSERT(count == fScratchMap.count());
    }

    Stats stats(this);
    size_t purgeableBytes = 0;
    int numBudgetedResourcesFlushWillMakePurgeable = 0;

    for (int i = 0; i < fNonpurgeableResources.count(); ++i) {
        SkASSERT(!fNonpurgeableResources[i]->resourcePriv().isPurgeable() ||
                 fNewlyPurgeableResourceForValidation == fNonpurgeableResources[i]);
        SkASSERT(*fNonpurgeableResources[i]->cacheAccess().accessCacheIndex() == i);
        SkASSERT(!fNonpurgeableResources[i]->wasDestroyed());
        if (fNonpurgeableResources[i]->resourcePriv().budgetedType() == GrBudgetedType::kBudgeted &&
            !fNonpurgeableResources[i]->cacheAccess().hasRefOrCommandBufferUsage() &&
            fNewlyPurgeableResourceForValidation != fNonpurgeableResources[i]) {
            ++numBudgetedResourcesFlushWillMakePurgeable;
        }
        stats.update(fNonpurgeableResources[i]);
    }
    for (int i = 0; i < fPurgeableQueue.count(); ++i) {
        SkASSERT(fPurgeableQueue.at(i)->resourcePriv().isPurgeable());
        SkASSERT(*fPurgeableQueue.at(i)->cacheAccess().accessCacheIndex() == i);
        SkASSERT(!fPurgeableQueue.at(i)->wasDestroyed());
        stats.update(fPurgeableQueue.at(i));
        purgeableBytes += fPurgeableQueue.at(i)->gpuMemorySize();
    }

    SkASSERT(fCount == this->getResourceCount());
    SkASSERT(fBudgetedCount <= fCount);
    SkASSERT(fBudgetedBytes <= fBytes);
    SkASSERT(stats.fBytes == fBytes);
    SkASSERT(fNumBudgetedResourcesFlushWillMakePurgeable ==
             numBudgetedResourcesFlushWillMakePurgeable);
    SkASSERT(stats.fBudgetedBytes == fBudgetedBytes);
    SkASSERT(stats.fBudgetedCount == fBudgetedCount);
    SkASSERT(purgeableBytes == fPurgeableBytes);
#if GR_CACHE_STATS
    SkASSERT(fBudgetedHighWaterCount <= fHighWaterCount);
    SkASSERT(fBudgetedHighWaterBytes <= fHighWaterBytes);
    SkASSERT(fBytes <= fHighWaterBytes);
    SkASSERT(fCount <= fHighWaterCount);
    SkASSERT(fBudgetedBytes <= fBudgetedHighWaterBytes);
    SkASSERT(fBudgetedCount <= fBudgetedHighWaterCount);
#endif
    SkASSERT(stats.fContent == fUniqueHash.count());
    SkASSERT(stats.fScratch == fScratchMap.count());

    // This assertion is not currently valid because we can be in recursive notifyCntReachedZero()
    // calls. This will be fixed when subresource registration is explicit.
    // bool overBudget = budgetedBytes > fMaxBytes || budgetedCount > fMaxCount;
    // SkASSERT(!overBudget || locked == count || fPurging);
}

bool GrResourceCache::isInCache(const GrGpuResource* resource) const {
    int index = *resource->cacheAccess().accessCacheIndex();
    if (index < 0) {
        return false;
    }
    if (index < fPurgeableQueue.count() && fPurgeableQueue.at(index) == resource) {
        return true;
    }
    if (index < fNonpurgeableResources.count() && fNonpurgeableResources[index] == resource) {
        return true;
    }
    SkDEBUGFAIL("Resource index should be -1 or the resource should be in the cache.");
    return false;
}

#endif // SK_DEBUG

#if GR_TEST_UTILS

int GrResourceCache::countUniqueKeysWithTag(const char* tag) const {
    int count = 0;
    fUniqueHash.foreach([&](const GrGpuResource& resource){
        if (0 == strcmp(tag, resource.getUniqueKey().tag())) {
            ++count;
        }
    });
    return count;
}

void GrResourceCache::changeTimestamp(uint32_t newTimestamp) {
    fTimestamp = newTimestamp;
}

#endif // GR_TEST_UTILS
