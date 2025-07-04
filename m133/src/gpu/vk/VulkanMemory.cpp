/*
* Copyright 2015 Google Inc.
*
* Use of this source code is governed by a BSD-style license that can be
* found in the LICENSE file.
*/

#include "include/core/SkTypes.h"
#include "include/gpu/GpuTypes.h"
#include "include/gpu/vk/VulkanMemoryAllocator.h"
#include "include/gpu/vk/VulkanTypes.h"
#include "src/gpu/ganesh/vk/GrVkUtil.h"
#include "src/gpu/vk/VulkanMemory.h"
#include "src/base/SkUtils.h"

#include <cstdint>
#include <cstring>

#define VK_CALL(GPU, X) GR_VK_CALL((GPU)->vkInterface(), X)

namespace skgpu {

using BufferUsage = VulkanMemoryAllocator::BufferUsage;

static bool FindMemoryType(GrVkGpu *gpu, uint32_t typeFilter, VkMemoryPropertyFlags properties, uint32_t &typeIndex)
{
    VkPhysicalDevice physicalDevice = gpu->physicalDevice();
    VkPhysicalDeviceMemoryProperties memProperties{};
    VK_CALL(gpu, GetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties));

    bool hasFound = false;
    for (uint32_t i = 0; i < memProperties.memoryTypeCount && !hasFound; ++i) {
        if (typeFilter & (1 << i)) {
            uint32_t supportedFlags = memProperties.memoryTypes[i].propertyFlags & properties;
            if (supportedFlags == properties) {
                typeIndex = i;
                hasFound = true;
            }
        }
    }

    return hasFound;
}

bool VulkanMemory::AllocBufferMemory(VulkanMemoryAllocator* allocator,
                                     VkBuffer buffer,
                                     skgpu::Protected isProtected,
                                     BufferUsage usage,
                                     bool shouldPersistentlyMapCpuToGpu,
                                     const std::function<CheckResult>& checkResult,
#ifdef SKIA_DFX_FOR_OHOS
                                     VulkanAlloc* alloc,
                                     size_t size) {
#else
                                     VulkanAlloc* alloc) {
#endif
    VulkanBackendMemory memory = 0;
    uint32_t propFlags;
    if (usage == BufferUsage::kTransfersFromCpuToGpu ||
        (usage == BufferUsage::kCpuWritesGpuReads && shouldPersistentlyMapCpuToGpu)) {
        // In general it is always fine (and often better) to keep buffers always mapped that we are
        // writing to on the cpu.
        propFlags = VulkanMemoryAllocator::kPersistentlyMapped_AllocationPropertyFlag;
    } else {
        propFlags = VulkanMemoryAllocator::kNone_AllocationPropertyFlag;
    }

    if (isProtected == Protected::kYes) {
        propFlags = propFlags | VulkanMemoryAllocator::kProtected_AllocationPropertyFlag;
    }

    VkResult result = allocator->allocateBufferMemory(buffer, usage, propFlags, &memory);
    if (!checkResult(result)) {
        return false;
    }
    allocator->getAllocInfo(memory, alloc);
    return true;
}

bool VulkanMemory::ImportAndBindBufferMemory(GrVkGpu* gpu,
                                           OH_NativeBuffer *nativeBuffer,
                                           VkBuffer buffer,
                                           VulkanAlloc* alloc) {
#ifdef SKIA_OHOS_FOR_OHOS_TRACE
    HITRACE_METER_FMT(HITRACE_TAG_GRAPHIC_AGP, "ImportAndBindBufferMemory");
#endif
    VkDevice device = gpu->device();
    VkMemoryRequirements memReqs{};
    VK_CALL(gpu, GetBufferMemoryRequirements(device, buffer, &memReqs));

    uint32_t typeIndex = 0;
    bool hasFound = FindMemoryType(gpu, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, typeIndex);
    if (!hasFound) {
        return false;
    }

    // Import external memory
    VkImportNativeBufferInfoOHOS importInfo{};
    importInfo.sType = VK_STRUCTURE_TYPE_IMPORT_NATIVE_BUFFER_INFO_OHOS;
    importInfo.pNext = nullptr;
    importInfo.buffer = nativeBuffer;

    VkMemoryDedicatedAllocateInfo dedicatedAllocInfo{};
    dedicatedAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    dedicatedAllocInfo.pNext = &importInfo;
    dedicatedAllocInfo.image = VK_NULL_HANDLE;
    dedicatedAllocInfo.buffer = buffer;

    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.pNext = &dedicatedAllocInfo;
    allocateInfo.allocationSize = memReqs.size;
    allocateInfo.memoryTypeIndex = typeIndex;

    VkResult err;
    VkDeviceMemory memory;
    GR_VK_CALL_RESULT(gpu, err, AllocateMemory(device, &allocateInfo, nullptr, &memory));
    if (err) {
        return false;
    }

    // Bind buffer
    GR_VK_CALL_RESULT(gpu, err, BindBufferMemory(device, buffer, memory, 0));
    if (err) {
        VK_CALL(gpu, FreeMemory(device, memory, nullptr));
        return false;
    }

    alloc->fMemory = memory;
    alloc->fOffset = 0;
    alloc->fSize = memReqs.size;
    alloc->fFlags = 0;
    alloc->fIsExternalMemory = true;

    return true;
}

void VulkanMemory::FreeBufferMemory(VulkanMemoryAllocator* allocator, const VulkanAlloc& alloc) {
    SkASSERT(alloc.fBackendMemory);
    allocator->freeMemory(alloc.fBackendMemory);
}

void VulkanMemory::FreeBufferMemory(const GrVkGpu* gpu, const VulkanAlloc& alloc) {
    if (alloc.fIsExternalMemory) {
        VK_CALL(gpu, FreeMemory(gpu->device(), alloc.fMemory, nullptr));
    } else {
        SkASSERT(alloc.fBackendMemory);
        VulkanMemoryAllocator* allocator = gpu->memoryAllocator();
        if (alloc.fAllocator != nullptr) {
            allocator = alloc.fAllocator;
        }
        allocator->freeMemory(alloc.fBackendMemory);
    }
}

bool VulkanMemory::AllocImageMemory(VulkanMemoryAllocator* allocator,
                                    VulkanMemoryAllocator* allocatorCacheImage,
                                    VkImage image,
                                    Protected isProtected,
                                    bool forceDedicatedMemory,
                                    bool useLazyAllocation,
                                    const std::function<CheckResult>& checkResult,
                                    VulkanAlloc* alloc,
                                    int memorySize) {
    VulkanBackendMemory memory = 0;

    bool vmaFlag = SkGetVmaCacheFlag();
    bool vmaCacheFlag = vmaFlag && memorySize > SkGetNeedCachedMemroySize();
    if (vmaCacheFlag && allocatorCacheImage) {
        allocator = allocatorCacheImage;
    }
    uint32_t propFlags;
    // If we ever find that our allocator is not aggressive enough in using dedicated image
    // memory we can add a size check here to force the use of dedicate memory. However for now,
    // we let the allocators decide. The allocator can query the GPU for each image to see if the
    // GPU recommends or requires the use of dedicated memory.
    if (vmaCacheFlag) {
        propFlags = VulkanMemoryAllocator::kNone_AllocationPropertyFlag;
    } else if (forceDedicatedMemory) {
        propFlags = VulkanMemoryAllocator::kDedicatedAllocation_AllocationPropertyFlag;
    } else {
        propFlags = VulkanMemoryAllocator::kNone_AllocationPropertyFlag;
    }

    if (isProtected == Protected::kYes) {
        propFlags = propFlags | VulkanMemoryAllocator::kProtected_AllocationPropertyFlag;
    }

    if (useLazyAllocation) {
        propFlags = propFlags | VulkanMemoryAllocator::kLazyAllocation_AllocationPropertyFlag;
    }

    { // OH ISSUE: add trace for vulkan interface
#ifdef SKIA_OHOS_FOR_OHOS_TRACE
        HITRACE_METER_FMT(HITRACE_TAG_GRAPHIC_AGP, "allocateImageMemory");
#endif
        VkResult result = allocator->allocateImageMemory(image, propFlags, &memory);
        if (!checkResult(result)) {
            return false;
        }
    }

    allocator->getAllocInfo(memory, alloc);
    return true;
}

void VulkanMemory::FreeImageMemory(VulkanMemoryAllocator* allocator,
                                   const VulkanAlloc& alloc) {
    SkASSERT(alloc.fBackendMemory);
    if (alloc.fAllocator != nullptr) {
        allocator = alloc.fAllocator;
    }
    allocator->freeMemory(alloc.fBackendMemory);
}

void* VulkanMemory::MapAlloc(VulkanMemoryAllocator* allocator,
                             const VulkanAlloc& alloc,
                             const std::function<CheckResult>& checkResult) {
    SkASSERT(VulkanAlloc::kMappable_Flag & alloc.fFlags);
    SkASSERT(alloc.fBackendMemory);
    if (alloc.fAllocator != nullptr) {
        allocator = alloc.fAllocator;
    }
    void* mapPtr;
    VkResult result = allocator->mapMemory(alloc.fBackendMemory, &mapPtr);
    if (!checkResult(result)) {
        return nullptr;
    }
    return mapPtr;
}

void VulkanMemory::UnmapAlloc(VulkanMemoryAllocator* allocator,
                              const VulkanAlloc& alloc) {
    SkASSERT(alloc.fBackendMemory);
    if (alloc.fAllocator != nullptr) {
        allocator = alloc.fAllocator;
    }
    allocator->unmapMemory(alloc.fBackendMemory);
}

void VulkanMemory::GetNonCoherentMappedMemoryRange(const VulkanAlloc& alloc,
                                                   VkDeviceSize offset,
                                                   VkDeviceSize size,
                                                   VkDeviceSize alignment,
                                                   VkMappedMemoryRange* range) {
    SkASSERT(alloc.fFlags & VulkanAlloc::kNoncoherent_Flag);
    offset = offset + alloc.fOffset;
    VkDeviceSize offsetDiff = offset & (alignment -1);
    offset = offset - offsetDiff;
    size = (size + alignment - 1) & ~(alignment - 1);
#ifdef SK_DEBUG
    SkASSERT(offset >= alloc.fOffset);
    SkASSERT(offset + size <= alloc.fOffset + alloc.fSize);
    SkASSERT(0 == (offset & (alignment-1)));
    SkASSERT(size > 0);
    SkASSERT(0 == (size & (alignment-1)));
#endif

    std::memset(range, 0, sizeof(VkMappedMemoryRange));
    range->sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range->memory = alloc.fMemory;
    range->offset = offset;
    range->size = size;
}

void VulkanMemory::FlushMappedAlloc(VulkanMemoryAllocator* allocator,
                                    const VulkanAlloc& alloc,
                                    VkDeviceSize offset,
                                    VkDeviceSize size,
                                    const std::function<CheckResult>& checkResult) {
    if (alloc.fFlags & VulkanAlloc::kNoncoherent_Flag) {
        SkASSERT(offset == 0);
        SkASSERT(size <= alloc.fSize);
        SkASSERT(alloc.fBackendMemory);
        if (alloc.fAllocator != nullptr) {
            allocator = alloc.fAllocator;
        }
        VkResult result = allocator->flushMemory(alloc.fBackendMemory, offset, size);
        checkResult(result);
    }
}

void VulkanMemory::InvalidateMappedAlloc(VulkanMemoryAllocator* allocator,
                                         const VulkanAlloc& alloc,
                                         VkDeviceSize offset,
                                         VkDeviceSize size,
                                         const std::function<CheckResult>& checkResult) {
    if (alloc.fFlags & VulkanAlloc::kNoncoherent_Flag) {
        SkASSERT(offset == 0);
        SkASSERT(size <= alloc.fSize);
        SkASSERT(alloc.fBackendMemory);
        if (alloc.fAllocator != nullptr) {
            allocator = alloc.fAllocator;
        }
        VkResult result = allocator->invalidateMemory(alloc.fBackendMemory, offset, size);
        checkResult(result);
    }
}

}  // namespace skgpu
