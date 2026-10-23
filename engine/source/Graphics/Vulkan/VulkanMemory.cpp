#include "Vulkan.h"

enum buffer_memory
{
    Buffer_GpuOnly = 0,
    Buffer_GpuShared,
    Buffer_Upload,

    Buffer_MemoryCount,
};

struct gpu_buffer
{
    VkBuffer        Buffer;
    VkDeviceMemory  Memory;
    VkDeviceAddress Address;
    void           *CpuBase;

    VkDeviceSize Size;
    VkDeviceSize AllocationSize;
    VkDeviceSize Used;

    uint32                MemoryTypeIndex;
    VkMemoryPropertyFlags MemoryFlags;
};

struct gpu_buffer_slice
{
    void           *Cpu;
    VkDeviceAddress Gpu;
    VkDeviceSize    Offset;
    VkDeviceSize    Size;
};

enum image_kind
{
    Image_Texture = 0,
    Image_Cubemap,
    Image_ColorTarget,
    Image_DepthTarget,

    Image_KindCount,
};

struct gpu_image
{
    VkImage        Image;
    VkImageView    View;
    VkDeviceMemory Memory;

    uint32 Width;
    uint32 Height;
    uint32 Depth;
    uint32 MipLevels;

    VkFormat   Format;
    image_kind Kind;
};

#define MEMORY_DEVICE VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
#define MEMORY_HOST   (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
#define IMAGE_TRANSFER (VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)

struct buffer_memory_desc
{
    VkMemoryPropertyFlags Wanted;
    VkMemoryPropertyFlags Accepted;

    bool32 Mapped;
};

global_variable buffer_memory_desc BufferMemoryDescs[] =
{
    { MEMORY_DEVICE,               MEMORY_DEVICE, false }, // Локальная память GPU: быстрая для GPU, недоступна CPU.
    { MEMORY_DEVICE | MEMORY_HOST, MEMORY_HOST,   true  }, // Предпочтительно BAR-память GPU, отображённая в адресное пространство CPU; иначе память хоста.
    { MEMORY_HOST,                 MEMORY_HOST,   true  }, // Когерентная память хоста: staging для загрузки с CPU с последующим копированием в ресурсы GPU.
};

static_assert(ArrayCount(BufferMemoryDescs) == Buffer_MemoryCount, "BufferMemoryDescs must describe every buffer_memory");

struct image_kind_desc
{
    VkImageType        Type;
    VkImageViewType    ViewType;
    VkImageCreateFlags Flags;
    VkImageUsageFlags  Usage;
    VkImageAspectFlags Aspect;

    uint32 Layers;
};

global_variable image_kind_desc ImageKindDescs[] =
{
    { VK_IMAGE_TYPE_2D, VK_IMAGE_VIEW_TYPE_2D,   0,  IMAGE_TRANSFER | VK_IMAGE_USAGE_SAMPLED_BIT,  VK_IMAGE_ASPECT_COLOR_BIT, 1 }, // Обычная 2D-текстура: загрузка через transfer и чтение в шейдере.
    { VK_IMAGE_TYPE_2D, VK_IMAGE_VIEW_TYPE_CUBE, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,  IMAGE_TRANSFER | VK_IMAGE_USAGE_SAMPLED_BIT,  VK_IMAGE_ASPECT_COLOR_BIT, 6 }, // Cubemap из шести 2D-слоёв: загрузка, mip-уровни и чтение в шейдере.
    { VK_IMAGE_TYPE_2D, VK_IMAGE_VIEW_TYPE_2D,   0,  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, 1 }, // Цветовой target рендера: запись как attachment
    { VK_IMAGE_TYPE_2D, VK_IMAGE_VIEW_TYPE_2D,   0,  VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, 1 }, // Depth target.
};

static_assert(ArrayCount(ImageKindDescs) == Image_KindCount, "ImageKindDescs must describe every image_kind");

internal bool32 AllocateMemory(vulkan_context *context, VkMemoryRequirements *memReq, VkMemoryPropertyFlags properties, bool32 deviceAddress, VkDeviceMemory *outMemory, uint32 *outMemoryType)
{
    VkPhysicalDeviceMemoryProperties *memProps = &context->MemoryProps;

    VkMemoryAllocateFlagsInfo flagsInfo{};
    flagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    flagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    for (uint32 i = 0; i < memProps->memoryTypeCount; ++i)
    {
        if ((memReq->memoryTypeBits & (1u << i)) == 0)
        {
            continue;
        }

        if ((memProps->memoryTypes[i].propertyFlags & properties) != properties)
        {
            continue;
        }

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize  = memReq->size;
        allocInfo.memoryTypeIndex = i;
        allocInfo.pNext           = deviceAddress ? &flagsInfo : nullptr;

        if (vkAllocateMemory(context->device, &allocInfo, nullptr, outMemory) == VK_SUCCESS)
        {
            *outMemoryType = i;

            return true;
        }
    }

    return false;
}

internal VkBuffer CreateBufferObject(vulkan_context *context, VkBufferUsageFlags usage, VkDeviceSize size)
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size        = size;
    bufferInfo.usage       = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer buffer = VK_NULL_HANDLE;

    VkResult created = vkCreateBuffer(context->device, &bufferInfo, nullptr, &buffer);
    Assert(created == VK_SUCCESS);

    return buffer;
}

internal VkDeviceAddress BufferAddress(vulkan_context *context, VkBuffer buffer)
{
    VkBufferDeviceAddressInfo addressInfo{};
    addressInfo.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addressInfo.buffer = buffer;

    return vkGetBufferDeviceAddress(context->device, &addressInfo);
}

internal gpu_buffer CreateBuffer(vulkan_context *context, buffer_memory kind, VkBufferUsageFlags usage, VkDeviceSize size)
{
    Assert(kind < Buffer_MemoryCount);

    buffer_memory_desc *desc = &BufferMemoryDescs[kind];

    gpu_buffer buffer = {};
    buffer.Buffer = CreateBufferObject(context, usage, size);
    buffer.Size   = size;

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(context->device, buffer.Buffer, &memReq);

    uint32 memoryType = 0;

    bool32 allocated = AllocateMemory(context, &memReq, desc->Wanted, true, &buffer.Memory, &memoryType);

    if (!allocated && desc->Accepted != desc->Wanted)
    {
        allocated = AllocateMemory(context, &memReq, desc->Accepted, true, &buffer.Memory, &memoryType);
    }

    Assert(allocated);

    VkResult bound = vkBindBufferMemory(context->device, buffer.Buffer, buffer.Memory, 0);
    Assert(bound == VK_SUCCESS);

    buffer.AllocationSize  = memReq.size;
    buffer.MemoryTypeIndex = memoryType;
    buffer.MemoryFlags     = context->MemoryProps.memoryTypes[memoryType].propertyFlags;
    buffer.Address         = BufferAddress(context, buffer.Buffer);

    if (desc->Mapped)
    {
        Assert((buffer.MemoryFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0);

        VkResult mapped = vkMapMemory(context->device, buffer.Memory, 0, size, 0, &buffer.CpuBase);
        Assert(mapped == VK_SUCCESS);
    }

    DebugLog("Buffer %llu bytes: memory type %u, device-local %u, host-visible %u\n", (uint64)size, memoryType, (buffer.MemoryFlags & MEMORY_DEVICE) != 0, (buffer.MemoryFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0);

    return buffer;
}

internal void DestroyBuffer(vulkan_context *context, gpu_buffer *buffer)
{
    if (buffer->CpuBase)
    {
        vkUnmapMemory(context->device, buffer->Memory);
    }

    vkDestroyBuffer(context->device, buffer->Buffer, nullptr);
    vkFreeMemory(context->device, buffer->Memory, nullptr);

    *buffer = {};
}

internal VkImageView CreateImageView(VkDevice device, VkImage image, VkFormat format, VkImageViewType viewType, VkImageAspectFlags aspect, uint32 layers, uint32 mipLevels)
{
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image    = image;
    viewInfo.viewType = viewType;
    viewInfo.format   = format;

    viewInfo.subresourceRange.aspectMask     = aspect;
    viewInfo.subresourceRange.baseMipLevel   = 0;
    viewInfo.subresourceRange.levelCount     = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount     = layers;

    VkImageView view = VK_NULL_HANDLE;

    VkResult created = vkCreateImageView(device, &viewInfo, nullptr, &view);
    Assert(created == VK_SUCCESS);

    return view;
}

internal gpu_image CreateImage(vulkan_context *context, image_kind kind, VkFormat format, uint32 width, uint32 height, uint32 depth, uint32 mipLevels)
{
    Assert(kind < Image_KindCount);

    image_kind_desc *desc = &ImageKindDescs[kind];

    gpu_image image = {};
    image.Width     = width;
    image.Height    = height;
    image.Depth     = depth;
    image.MipLevels = mipLevels;
    image.Format    = format;
    image.Kind      = kind;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.flags         = desc->Flags;
    imageInfo.imageType     = desc->Type;
    imageInfo.extent.width  = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth  = depth;
    imageInfo.mipLevels     = mipLevels;
    imageInfo.arrayLayers   = desc->Layers;
    imageInfo.format        = format;
    imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage         = desc->Usage;
    imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;

    VkResult created = vkCreateImage(context->device, &imageInfo, nullptr, &image.Image);
    Assert(created == VK_SUCCESS);

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(context->device, image.Image, &memReq);

    uint32 memoryType = 0;

    bool32 allocated = AllocateMemory(context, &memReq, MEMORY_DEVICE, false, &image.Memory, &memoryType);
    Assert(allocated);

    VkResult bound = vkBindImageMemory(context->device, image.Image, image.Memory, 0);
    Assert(bound == VK_SUCCESS);

    image.View = CreateImageView(context->device, image.Image, format, desc->ViewType, desc->Aspect, desc->Layers, mipLevels);

    return image;
}

internal void DestroyImage(vulkan_context *context, gpu_image *image)
{
    vkDestroyImageView(context->device, image->View, nullptr);
    vkDestroyImage(context->device, image->Image, nullptr);
    vkFreeMemory(context->device, image->Memory, nullptr);

    *image = {};
}

internal gpu_buffer_slice BufferSlice(const gpu_buffer *buffer, VkDeviceSize offset, VkDeviceSize size)
{
    Assert(offset <= buffer->Size);
    Assert(size <= buffer->Size - offset);

    gpu_buffer_slice result;
    result.Cpu    = buffer->CpuBase ? (uint8 *)buffer->CpuBase + offset : nullptr;
    result.Gpu    = buffer->Address + offset;
    result.Offset = offset;
    result.Size   = size;

    return result;
}

internal gpu_buffer_slice BufferSlot(const gpu_buffer *buffer, uint32 slot, VkDeviceSize size)
{
    Assert(size > 0);
    Assert(slot < buffer->Size / size);

    return BufferSlice(buffer, (VkDeviceSize)slot * size, size);
}

internal void ResetBuffer(gpu_buffer *buffer)
{
    buffer->Used = 0;
}

internal gpu_buffer_slice BufferAlloc(gpu_buffer *buffer, VkDeviceSize size, VkDeviceSize alignment)
{
    Assert(alignment && (alignment & (alignment - 1)) == 0);
    VkDeviceSize offset = AlignPow2(buffer->Used, alignment);

    Assert(offset >= buffer->Used);
    Assert(offset <= buffer->Size);
    Assert(size <= buffer->Size - offset);

    gpu_buffer_slice result = BufferSlice(buffer, offset, size);
    buffer->Used = offset + size;

    return result;
}

internal gpu_buffer_slice BufferWrite(gpu_buffer *buffer, const void *data, VkDeviceSize size, VkDeviceSize alignment)
{
    gpu_buffer_slice result = BufferAlloc(buffer, size, alignment);

    Assert(result.Cpu);

    CopySize(size, (void *)data, result.Cpu);

    return result;
}
