/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/gpu/vk/vulkanmemoryallocator/VulkanAMDMemoryAllocator.h"

#include "include/gpu/vk/VulkanBackendContext.h"
#include "include/gpu/vk/VulkanExtensions.h"
#include "include/private/base/SkAssert.h"
#include "include/private/base/SkTo.h"
#include "src/core/SkTraceEvent.h"
#include "src/gpu/GpuTypesPriv.h"
#include "src/gpu/vk/VulkanInterface.h"
#include "src/gpu/vk/VulkanUtilsPriv.h"
#include "src/gpu/vk/vulkanmemoryallocator/VulkanMemoryAllocatorPriv.h"
#include "src/base/SkUtils.h"

#include <algorithm>
#include <cstring>

namespace skgpu {

sk_sp<VulkanMemoryAllocator> VulkanAMDMemoryAllocator::Make(VkInstance instance,
                                                            VkPhysicalDevice physicalDevice,
                                                            VkDevice device,
                                                            uint32_t physicalDeviceVersion,
                                                            const VulkanExtensions* extensions,
                                                            const VulkanInterface* interface,
                                                            ThreadSafe threadSafe,
                                                            std::optional<VkDeviceSize> blockSize,
                                                            bool cacheFlag,
                                                            size_t maxBlockCount) {
#define SKGPU_COPY_FUNCTION(NAME) functions.vk##NAME = interface->fFunctions.f##NAME
#define SKGPU_COPY_FUNCTION_KHR(NAME) functions.vk##NAME##KHR = interface->fFunctions.f##NAME

    VmaVulkanFunctions functions;
    // We should be setting all the required functions (at least through vulkan 1.1), but this is
    // just extra belt and suspenders to make sure there isn't unitialized values here.
    std::memset(&functions, 0, sizeof(VmaVulkanFunctions));

    // We don't use dynamic function getting in the allocator so we set the getProc functions to
    // null.
    functions.vkGetInstanceProcAddr = nullptr;
    functions.vkGetDeviceProcAddr = nullptr;
    SKGPU_COPY_FUNCTION(GetPhysicalDeviceProperties);
    SKGPU_COPY_FUNCTION(GetPhysicalDeviceMemoryProperties);
    SKGPU_COPY_FUNCTION(AllocateMemory);
    SKGPU_COPY_FUNCTION(FreeMemory);
    SKGPU_COPY_FUNCTION(MapMemory);
    SKGPU_COPY_FUNCTION(UnmapMemory);
    SKGPU_COPY_FUNCTION(FlushMappedMemoryRanges);
    SKGPU_COPY_FUNCTION(InvalidateMappedMemoryRanges);
    SKGPU_COPY_FUNCTION(BindBufferMemory);
    SKGPU_COPY_FUNCTION(BindImageMemory);
    SKGPU_COPY_FUNCTION(GetBufferMemoryRequirements);
    SKGPU_COPY_FUNCTION(GetImageMemoryRequirements);
    SKGPU_COPY_FUNCTION(CreateBuffer);
    SKGPU_COPY_FUNCTION(DestroyBuffer);
    SKGPU_COPY_FUNCTION(CreateImage);
    SKGPU_COPY_FUNCTION(DestroyImage);
    SKGPU_COPY_FUNCTION(CmdCopyBuffer);
    SKGPU_COPY_FUNCTION_KHR(GetBufferMemoryRequirements2);
    SKGPU_COPY_FUNCTION_KHR(GetImageMemoryRequirements2);
    SKGPU_COPY_FUNCTION_KHR(BindBufferMemory2);
    SKGPU_COPY_FUNCTION_KHR(BindImageMemory2);
    SKGPU_COPY_FUNCTION_KHR(GetPhysicalDeviceMemoryProperties2);

    VmaAllocatorCreateInfo info;
    info.flags = 0;
    if (threadSafe == ThreadSafe::kNo) {
        info.flags |= VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT;
    }
    if (physicalDeviceVersion >= VK_MAKE_VERSION(1, 1, 0) ||
        (extensions->hasExtension(VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME, 1) &&
         extensions->hasExtension(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME, 1))) {
        info.flags |= VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT;
    }

    info.physicalDevice = physicalDevice;
    info.device = device;
    // 4MB was picked for the size here by looking at memory usage of Android apps and runs of DM.
    // It seems to be a good compromise of not wasting unused allocated space and not making too
    // many small allocations. The AMD allocator will start making blocks at 1/8 the max size and
    // builds up block size as needed before capping at the max set here.
    if (cacheFlag) {
        info.preferredLargeHeapBlockSize = SkGetVmaBlockSizeMB() * 1024 * 1024; // 1024 = 1K
    } else {
        info.preferredLargeHeapBlockSize = blockSize.value_or(4 * 1024 * 1024);
    }
    info.maxBlockCount = maxBlockCount;
    info.pAllocationCallbacks = nullptr;
    info.pDeviceMemoryCallbacks = nullptr;
    info.pHeapSizeLimit = nullptr;
    info.pVulkanFunctions = &functions;
    info.instance = instance;
    // TODO: Update our interface and headers to support vulkan 1.3 and add in the new required
    // functions for 1.3 that the allocator needs. Until then we just clamp the version to 1.1.
    info.vulkanApiVersion = std::min(physicalDeviceVersion, VK_MAKE_VERSION(1, 1, 0));
    info.pTypeExternalMemoryHandleTypes = nullptr;

    VmaAllocator allocator;
    vmaCreateAllocator(&info, &allocator);

    return sk_sp<VulkanAMDMemoryAllocator>(new VulkanAMDMemoryAllocator(allocator));
}

VulkanAMDMemoryAllocator::VulkanAMDMemoryAllocator(VmaAllocator allocator)
        : fAllocator(allocator) {}

VulkanAMDMemoryAllocator::~VulkanAMDMemoryAllocator() {
    vmaDestroyAllocator(fAllocator);
    fAllocator = VK_NULL_HANDLE;
}

VkResult VulkanAMDMemoryAllocator::allocateImageMemory(VkImage image,
                                                       uint32_t allocationPropertyFlags,
                                                       skgpu::VulkanBackendMemory* backendMemory) {
    TRACE_EVENT0_ALWAYS("skia.gpu", TRACE_FUNC);
    VmaAllocationCreateInfo info;
    info.flags = 0;
    info.usage = VMA_MEMORY_USAGE_UNKNOWN;
    info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    info.preferredFlags = 0;
    info.memoryTypeBits = 0;
    info.pool = VK_NULL_HANDLE;
    info.pUserData = nullptr;

    if (kDedicatedAllocation_AllocationPropertyFlag & allocationPropertyFlags) {
        info.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    }
    if (kLazyAllocation_AllocationPropertyFlag & allocationPropertyFlags) {
        info.requiredFlags |= VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;
    }
    if (kProtected_AllocationPropertyFlag & allocationPropertyFlags) {
        info.requiredFlags |= VK_MEMORY_PROPERTY_PROTECTED_BIT;
    }

    VmaAllocation allocation;
    VkResult result = vmaAllocateMemoryForImage(fAllocator, image, &info, &allocation, nullptr);
    if (VK_SUCCESS == result) {
        *backendMemory = (VulkanBackendMemory)allocation;
    }
    return result;
}

VkResult VulkanAMDMemoryAllocator::allocateBufferMemory(VkBuffer buffer,
                                                        BufferUsage usage,
                                                        uint32_t allocationPropertyFlags,
                                                        skgpu::VulkanBackendMemory* backendMemory) {
    TRACE_EVENT0("skia.gpu", TRACE_FUNC);
    VmaAllocationCreateInfo info;
    info.flags = 0;
    info.usage = VMA_MEMORY_USAGE_UNKNOWN;
    info.memoryTypeBits = 0;
    info.pool = VK_NULL_HANDLE;
    info.pUserData = nullptr;

    switch (usage) {
        case BufferUsage::kGpuOnly:
            info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            info.preferredFlags = 0;
            break;
        case BufferUsage::kCpuWritesGpuReads:
            // When doing cpu writes and gpu reads the general rule of thumb is to use coherent
            // memory. Though this depends on the fact that we are not doing any cpu reads and the
            // cpu writes are sequential. For sparse writes we'd want cpu cached memory, however we
            // don't do these types of writes in Skia.
            //
            // TODO: In the future there may be times where specific types of memory could benefit
            // from a coherent and cached memory. Typically these allow for the gpu to read cpu
            // writes from the cache without needing to flush the writes throughout the cache. The
            // reverse is not true and GPU writes tend to invalidate the cache regardless. Also
            // these gpu cache read access are typically lower bandwidth than non-cached memory.
            // For now Skia doesn't really have a need or want of this type of memory. But if we
            // ever do we could pass in an AllocationPropertyFlag that requests the cached property.
            info.requiredFlags =
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            info.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            break;
        case BufferUsage::kTransfersFromCpuToGpu:
            info.requiredFlags =
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            info.preferredFlags = 0;
            break;
        case BufferUsage::kTransfersFromGpuToCpu:
            info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
            info.preferredFlags = VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
            break;
    }

    if (kDedicatedAllocation_AllocationPropertyFlag & allocationPropertyFlags) {
        info.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    }
    if ((kLazyAllocation_AllocationPropertyFlag & allocationPropertyFlags) &&
        BufferUsage::kGpuOnly == usage) {
        info.preferredFlags |= VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;
    }

    if (kPersistentlyMapped_AllocationPropertyFlag & allocationPropertyFlags) {
        SkASSERT(BufferUsage::kGpuOnly != usage);
        info.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }

    if (kProtected_AllocationPropertyFlag & allocationPropertyFlags) {
        info.requiredFlags |= VK_MEMORY_PROPERTY_PROTECTED_BIT;
    }

    VmaAllocation allocation;
    VkResult result = vmaAllocateMemoryForBuffer(fAllocator, buffer, &info, &allocation, nullptr);
    if (VK_SUCCESS == result) {
        *backendMemory = (VulkanBackendMemory)allocation;
    }

    return result;
}

void VulkanAMDMemoryAllocator::freeMemory(const VulkanBackendMemory& memoryHandle) {
    TRACE_EVENT0("skia.gpu", TRACE_FUNC);
    const VmaAllocation allocation = (VmaAllocation)memoryHandle;
    vmaFreeMemory(fAllocator, allocation);
}

void VulkanAMDMemoryAllocator::getAllocInfo(const VulkanBackendMemory& memoryHandle,
                                            VulkanAlloc* alloc) const {
    const VmaAllocation allocation = (VmaAllocation)memoryHandle;
    VmaAllocationInfo vmaInfo;
    vmaGetAllocationInfo(fAllocator, allocation, &vmaInfo);

    VkMemoryPropertyFlags memFlags;
    vmaGetMemoryTypeProperties(fAllocator, vmaInfo.memoryType, &memFlags);

    uint32_t flags = 0;
    if (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT & memFlags) {
        flags |= VulkanAlloc::kMappable_Flag;
    }
    if (!SkToBool(VK_MEMORY_PROPERTY_HOST_COHERENT_BIT & memFlags)) {
        flags |= VulkanAlloc::kNoncoherent_Flag;
    }
    if (VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT & memFlags) {
        flags |= VulkanAlloc::kLazilyAllocated_Flag;
    }

    alloc->fMemory        = vmaInfo.deviceMemory;
    alloc->fOffset        = vmaInfo.offset;
    alloc->fSize          = vmaInfo.size;
    alloc->fFlags         = flags;
    alloc->fBackendMemory = memoryHandle;
    alloc->fAllocator     = (VulkanMemoryAllocator *)this;
}

VkResult VulkanAMDMemoryAllocator::mapMemory(const VulkanBackendMemory& memoryHandle, void** data) {
    TRACE_EVENT0("skia.gpu", TRACE_FUNC);
    const VmaAllocation allocation = (VmaAllocation)memoryHandle;
    return vmaMapMemory(fAllocator, allocation, data);
}

void VulkanAMDMemoryAllocator::unmapMemory(const VulkanBackendMemory& memoryHandle) {
    TRACE_EVENT0("skia.gpu", TRACE_FUNC);
    const VmaAllocation allocation = (VmaAllocation)memoryHandle;
    vmaUnmapMemory(fAllocator, allocation);
}

VkResult VulkanAMDMemoryAllocator::flushMemory(const VulkanBackendMemory& memoryHandle,
                                               VkDeviceSize offset,
                                               VkDeviceSize size) {
    TRACE_EVENT0("skia.gpu", TRACE_FUNC);
    const VmaAllocation allocation = (VmaAllocation)memoryHandle;
    return vmaFlushAllocation(fAllocator, allocation, offset, size);
}

VkResult VulkanAMDMemoryAllocator::invalidateMemory(const VulkanBackendMemory& memoryHandle,
                                                    VkDeviceSize offset,
                                                    VkDeviceSize size) {
    TRACE_EVENT0("skia.gpu", TRACE_FUNC);
    const VmaAllocation allocation = (VmaAllocation)memoryHandle;
    return vmaInvalidateAllocation(fAllocator, allocation, offset, size);
}

std::pair<uint64_t, uint64_t> VulkanAMDMemoryAllocator::totalAllocatedAndUsedMemory() const {
    VmaTotalStatistics stats;
    vmaCalculateStatistics(fAllocator, &stats);
    return {stats.total.statistics.blockBytes, stats.total.statistics.allocationBytes};
}

void VulkanAMDMemoryAllocator::dumpVmaStats(SkString *out, const char *sep) const
{
    constexpr int MB = 1024 * 1024;
    if (out == nullptr || sep == nullptr) {
        return;
    }
    bool flag = SkGetMemoryOptimizedFlag();
    out->appendf("vma_flag: %d %s", flag, sep);
    if (!flag) {
        return;
    }
    VmaTotalStatistics stats;
    vmaCalculateStatistics(fAllocator, &stats);
    uint64_t used = stats.total.statistics.allocationBytes;
    uint64_t total = stats.total.statistics.blockBytes;
    uint64_t free = total - used;
    auto maxBlockCount = SkGetVmaBlockCountMax();
    out->appendf("vma_free: %llu (%d MB)%s", free, free / MB, sep);
    out->appendf("vma_used: %llu (%d MB)%s", used, used / MB, sep);
    out->appendf("vma_total: %llu (%d MB)%s", total, total / MB, sep);
    out->appendf("vma_cacheBlockSize: %d MB%s", SkGetVmaBlockSizeMB(), sep);
    out->appendf("vma_cacheBlockCount: %llu / %llu%s",
        stats.total.statistics.blockCount <= maxBlockCount ? stats.total.statistics.blockCount : maxBlockCount,
        maxBlockCount, sep);
    out->appendf("vma_dedicatedBlockCount: %llu%s",
        stats.total.statistics.blockCount <= maxBlockCount ? 0 : stats.total.statistics.blockCount - maxBlockCount, sep);
    out->appendf("vma_allocationCount: %u%s", stats.total.statistics.allocationCount, sep);
    out->appendf("vma_unusedRangeCount: %u%s", stats.total.unusedRangeCount, sep);
    out->appendf("vma_allocationSize: %llu / %llu%s",
        stats.total.allocationSizeMin, stats.total.allocationSizeMax, sep);
    out->appendf("vma_unusedRangeSize: %llu / %llu%s",
        stats.total.unusedRangeSizeMin, stats.total.unusedRangeSizeMax, sep);
}

void VulkanAMDMemoryAllocator::vmaDefragment()
{
    bool flag = SkGetVmaDefragmentOn();
    if (!flag) {
        return;
    }
    bool debugFlag = SkGetVmaDebugFlag();
    if (!debugFlag) {
        vmaFreeEmptyBlock(fAllocator);
        return;
    }

    // dfx
    SkString debugInfo;
    dumpVmaStats(&debugInfo);
    SkDebugf("GrVkAMDMemoryAllocator::vmaDefragment() before: %s",
        debugInfo.c_str());
#ifdef SKIA_OHOS_FOR_OHOS_TRACE
    HITRACE_OHOS_NAME_FMT_ALWAYS("GrVkAMDMemoryAllocator::vmaDefragment() before: %s", debugInfo.c_str());
#endif

    {
        vmaFreeEmptyBlock(fAllocator);
    }

    // dfx
    debugInfo = "";
    dumpVmaStats(&debugInfo);
    SkDebugf("GrVkAMDMemoryAllocator::vmaDefragment() after: %s",
        debugInfo.c_str());
#ifdef SKIA_OHOS_FOR_OHOS_TRACE
    HITRACE_OHOS_NAME_FMT_ALWAYS("GrVkAMDMemoryAllocator::vmaDefragment() after: %s", debugInfo.c_str());
#endif
}

namespace VulkanMemoryAllocators {
sk_sp<VulkanMemoryAllocator> Make(const skgpu::VulkanBackendContext& backendContext,
                                  ThreadSafe threadSafe,
                                  std::optional<VkDeviceSize> blockSize,
                                  size_t maxBlockCount) {
    SkASSERT(backendContext.fInstance != VK_NULL_HANDLE);
    SkASSERT(backendContext.fPhysicalDevice != VK_NULL_HANDLE);
    SkASSERT(backendContext.fDevice != VK_NULL_HANDLE);
    SkASSERT(backendContext.fQueue != VK_NULL_HANDLE);
    SkASSERT(backendContext.fGetProc);

    skgpu::VulkanExtensions ext;
    const skgpu::VulkanExtensions* extensions = &ext;
    if (backendContext.fVkExtensions) {
        extensions = backendContext.fVkExtensions;
    }

    // It is a bit superfluous to create a VulkanInterface here just to create a memory allocator
    // given that Ganesh and Graphite will create their own. However, there's not a clean way to
    // have the interface created here persist for potential re-use without refactoring
    // VulkanMemoryAllocator to hold onto its interface as opposed to "borrowing" it.
    // Such a refactor could get messy without much actual benefit since interface creation is
    // not too expensive and this cost is only paid once during initialization.
    uint32_t physDevVersion = 0;
    sk_sp<const skgpu::VulkanInterface> interface =
            skgpu::MakeInterface(backendContext, extensions, &physDevVersion, nullptr);
    if (!interface) {
        return nullptr;
    }

    return VulkanAMDMemoryAllocator::Make(backendContext.fInstance,
                                          backendContext.fPhysicalDevice,
                                          backendContext.fDevice,
                                          physDevVersion,
                                          extensions,
                                          interface.get(),
                                          threadSafe,
                                          blockSize,
                                          maxBlockCount);
}

}  // namespace VulkanMemoryAllocators
}  // namespace skgpu
