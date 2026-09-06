#include "Strings.h"
#include "Vulkan.h"

#define MAX_SURFACE_FORMATS   64
#define MAX_PRESENT_MODES     8

struct queue_family_indices
{
    uint32 graphicsIndex;
    uint32 presentIndex;
    bool32 graphicsSupported;
    bool32 presentSupported;
};

struct swapchain_support_details
{
    VkSurfaceCapabilitiesKHR capabilities;
    VkSurfaceFormatKHR formats[MAX_SURFACE_FORMATS];
    uint32 formatCount;
    VkPresentModeKHR presentModes[MAX_PRESENT_MODES];
    uint32 presentModeCount;
};

global_variable const char *RequiredInstanceExtensions[] =
{
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
};

#if ENGINE_INTERNAL
global_variable const char *DebugInstanceExtensions[] =
{
    VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
};

global_variable const char *ValidationLayers[] =
{
    "VK_LAYER_KHRONOS_validation",
};
#endif

global_variable const char *RequiredDeviceExtensions[] =
{
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    VK_KHR_UNIFIED_IMAGE_LAYOUTS_EXTENSION_NAME,
    VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME,
    VK_EXT_SHADER_OBJECT_EXTENSION_NAME,
};

#define REQUIRED_API_VERSION VK_API_VERSION_1_3

#define REQUIRED_DEVICE_TYPE VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU

#define MAX_EXTENSIONS        256
#define MAX_DEVICES           4
#define MAX_FAMILY_COUNT      8
#define MAX_DEVICE_EXTENSIONS 256

#define PREFERRED_SURFACE_FORMAT VK_FORMAT_B8G8R8A8_SRGB
#define FALLBACK_SURFACE_FORMAT  VK_FORMAT_R8G8B8A8_SRGB
#define REQUIRED_COLOR_SPACE     VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
#define PREFERRED_PRESENT_MODE   VK_PRESENT_MODE_MAILBOX_KHR
#define FALLBACK_PRESENT_MODE    VK_PRESENT_MODE_FIFO_KHR

global_variable vulkan_context GlobalVulkan;

internal uint32 GetExtensions(VkExtensionProperties *props, uint32 maxCount)
{
    uint32 extensionCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);

    Assert(extensionCount <= maxCount);

    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, props);

    return extensionCount;
}

internal bool32 CheckInstanceExtensionSupport(const char **required, uint32 requiredCount)
{
    VkExtensionProperties available[MAX_EXTENSIONS];
    uint32 availableCount = GetExtensions(available, ArrayCount(available));

    for (uint32 i = 0; i < requiredCount; ++i)
    {
        bool32 found = false;
        for (uint32 j = 0; j < availableCount; ++j)
        {
            if (StringsAreEqual(required[i], available[j].extensionName))
            {
                found = true;
                break;
            }
        }

        if (!found)
        {
            DebugLog("Instance extension %s not supported\n", required[i]);
            return false;
        }
    }

    return true;
}

internal bool32 CheckInstanceVersion()
{
    PFN_vkEnumerateInstanceVersion enumerateVersion =
        (PFN_vkEnumerateInstanceVersion)vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion");

    uint32 version = VK_API_VERSION_1_0;
    if (enumerateVersion && enumerateVersion(&version) != VK_SUCCESS)
    {
        version = VK_API_VERSION_1_0;
    }

    if (version < REQUIRED_API_VERSION)
    {
        DebugLog("Vulkan loader is %u.%u, need 1.3 for core dynamic state\n", VK_API_VERSION_MAJOR(version), VK_API_VERSION_MINOR(version));
        return false;
    }

    return true;
}

internal VkApplicationInfo AppInfo()
{
    VkApplicationInfo appInfo = {};

    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName =  "Vulkan App";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = REQUIRED_API_VERSION;

    return appInfo;
}

internal VkInstanceCreateInfo InstanceInfo(VkApplicationInfo *appInfo, const char **extensions, uint32 extensionCount)
{
    VkInstanceCreateInfo createInfo{};

    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = appInfo;
    createInfo.enabledExtensionCount = extensionCount;
    createInfo.ppEnabledExtensionNames = extensions;

    return createInfo;
}

internal uint32 GatherInstanceExtensions(const char **out, uint32 maxCount)
{
    uint32 count = 0;

    for (uint32 i = 0; i < ArrayCount(RequiredInstanceExtensions); ++i)
    {
        Assert(count < maxCount);
        out[count++] = RequiredInstanceExtensions[i];
    }

#if ENGINE_INTERNAL
    if (CheckInstanceExtensionSupport(DebugInstanceExtensions, (uint32)ArrayCount(DebugInstanceExtensions)))
    {
        Assert(count < maxCount);
        out[count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    }
#endif

    return count;
}

#if ENGINE_INTERNAL

internal bool32 CheckInstanceLayerSupport(const char **required, uint32 requiredCount)
{
    uint32 availableCount = 0;
    vkEnumerateInstanceLayerProperties(&availableCount, nullptr);

    Assert(availableCount <= MAX_EXTENSIONS);

    VkLayerProperties available[MAX_EXTENSIONS];
    vkEnumerateInstanceLayerProperties(&availableCount, available);

    for (uint32 i = 0; i < requiredCount; ++i)
    {
        bool32 found = false;
        for (uint32 j = 0; j < availableCount; ++j)
        {
            if (StringsAreEqual(required[i], available[j].layerName))
            {
                found = true;
                break;
            }
        }

        if (!found)
        {
            DebugLog("Instance layer %s not available\n", required[i]);
            return false;
        }
    }

    return true;
}

internal VKAPI_ATTR VkBool32 VKAPI_CALL DebugMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT types, const VkDebugUtilsMessengerCallbackDataEXT *data, void *userData)
{
    DebugLog("[vulkan] %s\n", data->pMessage);

    return VK_FALSE;
}

internal VkDebugUtilsMessengerCreateInfoEXT DebugMessengerInfo()
{
    VkDebugUtilsMessengerCreateInfoEXT info{};
    info.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = DebugMessengerCallback;

    return info;
}

internal void CreateDebugMessenger(vulkan_context *context)
{
    PFN_vkCreateDebugUtilsMessengerEXT create =
        (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(context->instance, "vkCreateDebugUtilsMessengerEXT");

    if (!create)
    {
        DebugLog("Debug messenger entry point missing\n");
        return;
    }

    VkDebugUtilsMessengerCreateInfoEXT info = DebugMessengerInfo();

    VkResult result = create(context->instance, &info, nullptr, &context->debugMessenger);
    Assert(result == VK_SUCCESS);

    DebugLog("Validation layer active\n");
}
#endif

internal void CreateSurface(vulkan_context *context, HINSTANCE hinstance, HWND hwnd)
{
    VkWin32SurfaceCreateInfoKHR surfaceInfo{};
    surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surfaceInfo.hinstance = hinstance;
    surfaceInfo.hwnd = hwnd;

    VkResult result = vkCreateWin32SurfaceKHR(context->instance, &surfaceInfo, nullptr, &context->surface);
    Assert(result == VK_SUCCESS);

    DebugLog("Window surface created\n");
}

internal uint32 GetDevices(const VkInstance *instance, VkPhysicalDevice *devices, uint32 maxCount)
{
    uint32_t devicesCount = 0;
    VkResult result = vkEnumeratePhysicalDevices(*instance, &devicesCount, nullptr);
    if (result != VK_SUCCESS)
    {
        return 0;
    }

    if (devicesCount > maxCount)
    {
        devicesCount = maxCount;
    }

    if (!devicesCount)
    {
        DebugLog("No vulkan devices support\n");
    }
    else
    {
        DebugLog("vulkan devices support %d\n", devicesCount);
    }

    vkEnumeratePhysicalDevices(*instance, &devicesCount, devices);

    return devicesCount;
}

internal queue_family_indices SelectQueueFamilyIndices(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    queue_family_indices result = {};

    uint32 familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, nullptr);
    Assert(familyCount);

    if (familyCount > MAX_FAMILY_COUNT)
    {
        familyCount = MAX_FAMILY_COUNT;
    }

    VkQueueFamilyProperties families[MAX_FAMILY_COUNT];
    vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, families);

    for (uint32 i = 0; i < familyCount; ++i)
    {

        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            result.graphicsSupported = true;
            result.graphicsIndex = i;
        }

        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
        if (presentSupport)
        {
            result.presentSupported = true;
            result.presentIndex = i;
        }

        if (result.graphicsSupported && result.presentSupported)
        {
            break;
        }
    }

    return result;
}

internal bool32 CheckDeviceExtensionSupport(VkPhysicalDevice device, const char **required, uint32 requiredCount)
{
    uint32 availableCount = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &availableCount, nullptr);

    if (availableCount > MAX_DEVICE_EXTENSIONS)
    {
        availableCount = MAX_DEVICE_EXTENSIONS;
    }

    VkExtensionProperties available[MAX_DEVICE_EXTENSIONS];
    vkEnumerateDeviceExtensionProperties(device, nullptr, &availableCount, available);

    for (uint32 i = 0; i < requiredCount; ++i)
    {
        bool32 found = false;
        for (uint32 j = 0; j < availableCount; ++j)
        {
            if (StringsAreEqual(required[i], available[j].extensionName))
            {
                found = true;
                break;
            }
        }

        if (!found)
        {
            DebugLog("Device extension %s not supported\n", required[i]);
            return false;
        }
    }

    return true;
}

internal swapchain_support_details QuerySwapchainSupport(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    swapchain_support_details details = {};

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

    uint32 formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
    Assert(formatCount <= MAX_SURFACE_FORMATS);
    details.formatCount = formatCount;
    if (formatCount)
    {
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats);
    }

    uint32 presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
    if (presentModeCount > MAX_PRESENT_MODES)
    {
        presentModeCount = MAX_PRESENT_MODES;
    }
    details.presentModeCount = presentModeCount;
    if (presentModeCount)
    {
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes);
    }

    return details;
}

internal bool32 CheckDeviceFeatures(VkPhysicalDevice device)
{
    VkPhysicalDeviceShaderObjectFeaturesEXT shaderObject{};
    shaderObject.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT;

    VkPhysicalDeviceVulkan12Features vulkan12{};
    vulkan12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;

    VkPhysicalDeviceVulkan13Features vulkan13{};
    vulkan13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

    VkPhysicalDeviceUnifiedImageLayoutsFeaturesKHR unifiedLayouts{};
    unifiedLayouts.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFIED_IMAGE_LAYOUTS_FEATURES_KHR;

    VkPhysicalDeviceDescriptorBufferFeaturesEXT descriptorBuffer{};
    descriptorBuffer.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT;

    shaderObject.pNext   = &vulkan12;
    vulkan12.pNext       = &vulkan13;
    vulkan13.pNext       = &unifiedLayouts;
    unifiedLayouts.pNext = &descriptorBuffer;

    VkPhysicalDeviceFeatures2 available{};
    available.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    available.pNext = &shaderObject;
    vkGetPhysicalDeviceFeatures2(device, &available);

    bool32 supported = shaderObject.shaderObject && descriptorBuffer.descriptorBuffer && unifiedLayouts.unifiedImageLayouts &&
                       vulkan12.bufferDeviceAddress && vulkan12.scalarBlockLayout && vulkan12.timelineSemaphore &&
                       vulkan13.dynamicRendering && vulkan13.synchronization2 &&
                       available.features.shaderInt64 && available.features.shaderSampledImageArrayDynamicIndexing &&
                       available.features.shaderStorageImageArrayDynamicIndexing;

    if (!supported)
    {
        DebugLog("Device features: shaderObject %d, descriptorBuffer %d, unifiedImageLayouts %d, bufferDeviceAddress %d, scalarBlockLayout %d, timelineSemaphore %d, dynamicRendering %d, synchronization2 %d, shaderInt64 %d, sampledImageArrayDynamicIndexing %d\n",
                 (int)shaderObject.shaderObject, (int)descriptorBuffer.descriptorBuffer, (int)unifiedLayouts.unifiedImageLayouts,
                 (int)vulkan12.bufferDeviceAddress, (int)vulkan12.scalarBlockLayout, (int)vulkan12.timelineSemaphore,
                 (int)vulkan13.dynamicRendering, (int)vulkan13.synchronization2,
                 (int)available.features.shaderInt64, (int)available.features.shaderSampledImageArrayDynamicIndexing);
        DebugLog("Device features: storageImageArrayDynamicIndexing %d\n", (int)available.features.shaderStorageImageArrayDynamicIndexing);
    }

    return supported;
}

internal void SelectDevice(vulkan_context *context)
{
    VkPhysicalDevice devices[MAX_DEVICES];
    uint32 devicesCount = GetDevices(&context->instance, devices, ArrayCount(devices));

    for (uint32 i = 0; i < devicesCount; i++)
    {
        VkPhysicalDeviceProperties deviceProperties;
        vkGetPhysicalDeviceProperties(devices[i], &deviceProperties);

        if (deviceProperties.deviceType != REQUIRED_DEVICE_TYPE)
        {
            continue;
        }

        if (deviceProperties.apiVersion < REQUIRED_API_VERSION)
        {
            DebugLog("Device '%s' is Vulkan %u.%u, need 1.3\n", deviceProperties.deviceName, VK_API_VERSION_MAJOR(deviceProperties.apiVersion), VK_API_VERSION_MINOR(deviceProperties.apiVersion));
            continue;
        }

        if (!CheckDeviceExtensionSupport(devices[i], RequiredDeviceExtensions, ArrayCount(RequiredDeviceExtensions)))
        {
            continue;
        }

        if (!CheckDeviceFeatures(devices[i]))
        {
            continue;
        }

        swapchain_support_details swapchain = QuerySwapchainSupport(devices[i], context->surface);
        if (!swapchain.formatCount || !swapchain.presentModeCount)
        {
            continue;
        }

        queue_family_indices indices = SelectQueueFamilyIndices(devices[i], context->surface);
        if (!indices.graphicsSupported || !indices.presentSupported)
        {
            continue;
        }

        DebugLog("Device '%s' selected\n", deviceProperties.deviceName);

        context->physicalDevice = devices[i];
        context->graphicsFamilyIndex = indices.graphicsIndex;
        context->presentFamilyIndex = indices.presentIndex;
        return;
    }
}

internal VkQueue GetQueue(VkDevice device, uint32 queueFamilyIndex)
{
    Assert(device != VK_NULL_HANDLE);

    VkQueue queue;
    vkGetDeviceQueue(device, queueFamilyIndex, 0, &queue);
    return queue;
}

internal void CreateLogicalDevice(vulkan_context *context)
{
    float queuePriority = 1.0f;

    uint32 uniqueFamilies[2];
    uint32 uniqueCount = 0;
    uniqueFamilies[uniqueCount++] = context->graphicsFamilyIndex;
    if (context->presentFamilyIndex != context->graphicsFamilyIndex)
    {
        uniqueFamilies[uniqueCount++] = context->presentFamilyIndex;
    }

    VkDeviceQueueCreateInfo queueInfos[2] = {};
    for (uint32 i = 0; i < uniqueCount; ++i)
    {
        queueInfos[i].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfos[i].queueFamilyIndex = uniqueFamilies[i];
        queueInfos[i].queueCount = 1;
        queueInfos[i].pQueuePriorities = &queuePriority;
    }

    VkPhysicalDeviceShaderObjectFeaturesEXT shaderObject{};
    shaderObject.sType        = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT;
    shaderObject.shaderObject = VK_TRUE;

    VkPhysicalDeviceDescriptorBufferFeaturesEXT descriptorBuffer{};
    descriptorBuffer.sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT;
    descriptorBuffer.descriptorBuffer = VK_TRUE;
    descriptorBuffer.pNext            = &shaderObject;

    VkPhysicalDeviceUnifiedImageLayoutsFeaturesKHR unifiedLayouts{};
    unifiedLayouts.sType                = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFIED_IMAGE_LAYOUTS_FEATURES_KHR;
    unifiedLayouts.unifiedImageLayouts  = VK_TRUE;
    unifiedLayouts.pNext                = &descriptorBuffer;

    VkPhysicalDeviceVulkan13Features vulkan13{};
    vulkan13.sType             = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    vulkan13.dynamicRendering  = VK_TRUE;
    vulkan13.synchronization2  = VK_TRUE;
    vulkan13.pNext             = &unifiedLayouts;

    VkPhysicalDeviceVulkan12Features vulkan12{};
    vulkan12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    vulkan12.bufferDeviceAddress                          = VK_TRUE;
    vulkan12.scalarBlockLayout                            = VK_TRUE;
    vulkan12.timelineSemaphore                            = VK_TRUE;
    vulkan12.pNext = &vulkan13;

    VkPhysicalDeviceFeatures2 features{};
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features.features.shaderSampledImageArrayDynamicIndexing = VK_TRUE;
    features.features.shaderStorageImageArrayDynamicIndexing = VK_TRUE;
    features.features.shaderInt64                            = VK_TRUE;
    features.pNext = &vulkan12;

    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.pNext = &features;
    deviceInfo.queueCreateInfoCount = uniqueCount;
    deviceInfo.pQueueCreateInfos = queueInfos;
    deviceInfo.pEnabledFeatures = nullptr;
    deviceInfo.enabledExtensionCount = (uint32)ArrayCount(RequiredDeviceExtensions);
    deviceInfo.ppEnabledExtensionNames = RequiredDeviceExtensions;

    VkResult result = vkCreateDevice(context->physicalDevice, &deviceInfo, nullptr, &context->device);
    Assert(result == VK_SUCCESS);

    context->CreateShadersEXT               = (PFN_vkCreateShadersEXT)vkGetDeviceProcAddr(context->device, "vkCreateShadersEXT");
    context->DestroyShaderEXT               = (PFN_vkDestroyShaderEXT)vkGetDeviceProcAddr(context->device, "vkDestroyShaderEXT");
    context->CmdBindShadersEXT              = (PFN_vkCmdBindShadersEXT)vkGetDeviceProcAddr(context->device, "vkCmdBindShadersEXT");
    context->CmdSetVertexInputEXT           = (PFN_vkCmdSetVertexInputEXT)vkGetDeviceProcAddr(context->device, "vkCmdSetVertexInputEXT");
    context->CmdSetRasterizationSamplesEXT  = (PFN_vkCmdSetRasterizationSamplesEXT)vkGetDeviceProcAddr(context->device, "vkCmdSetRasterizationSamplesEXT");
    context->CmdSetSampleMaskEXT            = (PFN_vkCmdSetSampleMaskEXT)vkGetDeviceProcAddr(context->device, "vkCmdSetSampleMaskEXT");
    context->CmdSetAlphaToCoverageEnableEXT = (PFN_vkCmdSetAlphaToCoverageEnableEXT)vkGetDeviceProcAddr(context->device, "vkCmdSetAlphaToCoverageEnableEXT");
    context->CmdSetPolygonModeEXT           = (PFN_vkCmdSetPolygonModeEXT)vkGetDeviceProcAddr(context->device, "vkCmdSetPolygonModeEXT");
    context->CmdSetDepthClampEnableEXT      = (PFN_vkCmdSetDepthClampEnableEXT)vkGetDeviceProcAddr(context->device, "vkCmdSetDepthClampEnableEXT");
    context->CmdSetLogicOpEnableEXT         = (PFN_vkCmdSetLogicOpEnableEXT)vkGetDeviceProcAddr(context->device, "vkCmdSetLogicOpEnableEXT");
    context->CmdSetColorBlendEnableEXT      = (PFN_vkCmdSetColorBlendEnableEXT)vkGetDeviceProcAddr(context->device, "vkCmdSetColorBlendEnableEXT");
    context->CmdSetColorBlendEquationEXT    = (PFN_vkCmdSetColorBlendEquationEXT)vkGetDeviceProcAddr(context->device, "vkCmdSetColorBlendEquationEXT");
    context->CmdSetColorWriteMaskEXT        = (PFN_vkCmdSetColorWriteMaskEXT)vkGetDeviceProcAddr(context->device, "vkCmdSetColorWriteMaskEXT");

    Assert(context->CreateShadersEXT && context->DestroyShaderEXT && context->CmdBindShadersEXT && context->CmdSetVertexInputEXT &&
           context->CmdSetRasterizationSamplesEXT && context->CmdSetSampleMaskEXT && context->CmdSetAlphaToCoverageEnableEXT &&
           context->CmdSetPolygonModeEXT && context->CmdSetDepthClampEnableEXT && context->CmdSetLogicOpEnableEXT &&
           context->CmdSetColorBlendEnableEXT && context->CmdSetColorBlendEquationEXT && context->CmdSetColorWriteMaskEXT);

    context->GetDescriptorSetLayoutSizeEXT         = (PFN_vkGetDescriptorSetLayoutSizeEXT)vkGetDeviceProcAddr(context->device, "vkGetDescriptorSetLayoutSizeEXT");
    context->GetDescriptorSetLayoutBindingOffsetEXT = (PFN_vkGetDescriptorSetLayoutBindingOffsetEXT)vkGetDeviceProcAddr(context->device, "vkGetDescriptorSetLayoutBindingOffsetEXT");
    context->GetDescriptorEXT                       = (PFN_vkGetDescriptorEXT)vkGetDeviceProcAddr(context->device, "vkGetDescriptorEXT");
    context->CmdBindDescriptorBuffersEXT            = (PFN_vkCmdBindDescriptorBuffersEXT)vkGetDeviceProcAddr(context->device, "vkCmdBindDescriptorBuffersEXT");
    context->CmdSetDescriptorBufferOffsetsEXT       = (PFN_vkCmdSetDescriptorBufferOffsetsEXT)vkGetDeviceProcAddr(context->device, "vkCmdSetDescriptorBufferOffsetsEXT");

    Assert(context->GetDescriptorSetLayoutSizeEXT && context->GetDescriptorSetLayoutBindingOffsetEXT && context->GetDescriptorEXT &&
           context->CmdBindDescriptorBuffersEXT && context->CmdSetDescriptorBufferOffsetsEXT);

    context->DescriptorProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT;

    VkPhysicalDeviceProperties2 deviceProps{};
    deviceProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    deviceProps.pNext = &context->DescriptorProps;
    vkGetPhysicalDeviceProperties2(context->physicalDevice, &deviceProps);

    vkGetPhysicalDeviceMemoryProperties(context->physicalDevice, &context->MemoryProps);

    context->graphicsQueue = GetQueue(context->device, context->graphicsFamilyIndex);
    context->presentQueue  = GetQueue(context->device, context->presentFamilyIndex);

    DebugLog("Logical device created\n");
}

internal VkSurfaceFormatKHR ChooseSwapSurfaceFormat(swapchain_support_details *support)
{

    for (uint32 i = 0; i < support->formatCount; ++i)
    {
        if (support->formats[i].format == PREFERRED_SURFACE_FORMAT
            && support->formats[i].colorSpace == REQUIRED_COLOR_SPACE)
        {
            return support->formats[i];
        }
    }

    for (uint32 i = 0; i < support->formatCount; ++i)
    {
        if (support->formats[i].format == FALLBACK_SURFACE_FORMAT
            && support->formats[i].colorSpace == REQUIRED_COLOR_SPACE)
        {
            return support->formats[i];
        }
    }

    DebugLog("No sRGB surface format available, colours will be too dark\n");

    return support->formats[0];
}

internal VkPresentModeKHR ChooseSwapPresentMode(swapchain_support_details *support)
{

    for (uint32 i = 0; i < support->presentModeCount; ++i)
    {
        if (support->presentModes[i] == PREFERRED_PRESENT_MODE)
        {
            return PREFERRED_PRESENT_MODE;
        }
    }

    return FALLBACK_PRESENT_MODE;
}

internal VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR *capabilities, HWND hwnd)
{

    if (capabilities->currentExtent.width != UINT32_MAX)
    {
        return capabilities->currentExtent;
    }

    RECT rect;
    GetClientRect(hwnd, &rect);
    uint32 width  = (uint32)(rect.right - rect.left);
    uint32 height = (uint32)(rect.bottom - rect.top);

    if (width  < capabilities->minImageExtent.width)  width  = capabilities->minImageExtent.width;
    if (width  > capabilities->maxImageExtent.width)  width  = capabilities->maxImageExtent.width;
    if (height < capabilities->minImageExtent.height) height = capabilities->minImageExtent.height;
    if (height > capabilities->maxImageExtent.height) height = capabilities->maxImageExtent.height;

    VkExtent2D extent;
    extent.width  = width;
    extent.height = height;
    return extent;
}

internal void CreateSwapchain(vulkan_context *context, HWND hwnd)
{
    swapchain_support_details support = QuerySwapchainSupport(context->physicalDevice, context->surface);

    VkSurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(&support);
    VkPresentModeKHR   presentMode   = ChooseSwapPresentMode(&support);
    VkExtent2D         extent        = ChooseSwapExtent(&support.capabilities, hwnd);

    uint32 imageCount = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0 && imageCount > support.capabilities.maxImageCount)
    {
        imageCount = support.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = context->surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    uint32 familyIndices[] = { context->graphicsFamilyIndex, context->presentFamilyIndex };
    if (context->graphicsFamilyIndex != context->presentFamilyIndex)
    {

        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = familyIndices;
    }
    else
    {

        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = support.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = context->swapchain;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;

    VkResult result = vkCreateSwapchainKHR(context->device, &createInfo, nullptr, &swapchain);
    Assert(result == VK_SUCCESS);

    context->swapchain = swapchain;

    uint32 count = 0;
    vkGetSwapchainImagesKHR(context->device, context->swapchain, &count, nullptr);
    Assert(count <= MAX_SWAPCHAIN_IMAGES);
    context->swapchainImageCount = count;
    vkGetSwapchainImagesKHR(context->device, context->swapchain, &count, context->swapchainImages);

    context->swapchainImageFormat = surfaceFormat.format;
    context->swapchainExtent = extent;

    DebugLog("Swapchain created (%u images, %ux%u, format %d)\n", count, extent.width, extent.height, surfaceFormat.format);
}

internal void CreateSwapchainImageViews(vulkan_context *context)
{
    for (uint32 i = 0; i < context->swapchainImageCount; ++i)
    {
        context->swapchainImageViews[i] = CreateImageView(context->device, context->swapchainImages[i], context->swapchainImageFormat, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1);
    }

    DebugLog("Image views created (%u)\n", context->swapchainImageCount);
}

internal void DestroySwapchainResources(vulkan_context *context)
{
    for (uint32 i = 0; i < context->swapchainImageCount; ++i)
    {
        vkDestroyImageView(context->device, context->swapchainImageViews[i], nullptr);
    }
}

internal bool32 RecreateSwapchain(vulkan_context *context)
{

    RECT rect;
    GetClientRect(context->windowHandle, &rect);
    if ((rect.right - rect.left) <= 0 || (rect.bottom - rect.top) <= 0)
    {
        return false;
    }

    vkDeviceWaitIdle(context->device);

    DestroySwapchainResources(context);

    VkSwapchainKHR old = context->swapchain;

    CreateSwapchain(context, context->windowHandle);

    vkDestroySwapchainKHR(context->device, old, nullptr);

    CreateSwapchainImageViews(context);

    return true;
}

internal void CreateCommandPool(vulkan_context *context)
{
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = context->graphicsFamilyIndex;

    VkResult result = vkCreateCommandPool(context->device, &poolInfo, nullptr, &context->commandPool);
    Assert(result == VK_SUCCESS);

    DebugLog("Command pool created\n");
}

internal void CreateCommandBuffer(vulkan_context *context)
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = context->commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = MAX_FRAMES_IN_FLIGHT;

    VkResult result = vkAllocateCommandBuffers(context->device, &allocInfo, context->commandBuffers);
    Assert(result == VK_SUCCESS);

    DebugLog("Command buffers allocated (%d)\n", MAX_FRAMES_IN_FLIGHT);
}

internal void CreateSyncObjects(vulkan_context *context)
{
    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for (uint32 i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        VkResult result = vkCreateSemaphore(context->device, &semInfo, nullptr, &context->imageAvailableSemaphores[i]);
        Assert(result == VK_SUCCESS);
    }

    for (uint32 i = 0; i < MAX_SWAPCHAIN_IMAGES; ++i)
    {
        VkResult result = vkCreateSemaphore(context->device, &semInfo, nullptr, &context->renderFinishedSemaphores[i]);
        Assert(result == VK_SUCCESS);
    }

    VkSemaphoreTypeCreateInfo timelineInfo{};
    timelineInfo.sType         = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    timelineInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    timelineInfo.initialValue  = 0;

    VkSemaphoreCreateInfo timelineSemInfo{};
    timelineSemInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    timelineSemInfo.pNext = &timelineInfo;

    VkResult timelineResult = vkCreateSemaphore(context->device, &timelineSemInfo, nullptr, &context->frameTimeline);
    Assert(timelineResult == VK_SUCCESS);

    DebugLog("Sync objects created\n");
}
