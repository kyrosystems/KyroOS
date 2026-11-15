#include "vulkan/vulkan.h"
#include <kyroos_gfx.h>
#include <string.h>

struct VkInstance_T {
    char name[64];
};

struct VkPhysicalDevice_T {
    char name[64];
    uint32_t device_id;
};

struct VkDevice_T {
    VkPhysicalDevice phys_dev;
};

struct VkQueue_T {
    VkDevice device;
};

struct VkCommandPool_T {
    VkDevice device;
};

struct VkCommandBuffer_T {
    VkCommandPool pool;
};

struct VkSwapchainKHR_T {
    VkDevice device;
    uint32_t width, height;
};

static struct VkInstance_T g_instance;
static struct VkPhysicalDevice_T g_phys_dev = {
    .name = "KyroOS Software Rasterizer (Vulkan 1.1)",
    .device_id = 0x1002 // AMD / Generic
};
static struct VkDevice_T g_device;
static struct VkQueue_T g_queue;
static struct VkCommandPool_T g_cmd_pool;
static struct VkCommandBuffer_T g_cmd_buf;
static struct VkSwapchainKHR_T g_swapchain;

VkResult vkCreateInstance(const VkInstanceCreateInfo* pCreateInfo, const void* pAllocator, VkInstance* pInstance) {
    (void)pAllocator;
    if (!pCreateInfo || !pInstance) return VK_ERROR_INITIALIZATION_FAILED;
    strncpy(g_instance.name, "KyroOS-Vulkan", 63);
    *pInstance = &g_instance;
    return VK_SUCCESS;
}

void vkDestroyInstance(VkInstance instance, const void* pAllocator) {
    (void)instance; (void)pAllocator;
}

VkResult vkEnumeratePhysicalDevices(VkInstance instance, uint32_t* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices) {
    (void)instance;
    if (!pPhysicalDeviceCount) return VK_ERROR_INITIALIZATION_FAILED;
    if (!pPhysicalDevices) {
        *pPhysicalDeviceCount = 1;
        return VK_SUCCESS;
    }
    if (*pPhysicalDeviceCount < 1) return VK_INCOMPLETE;
    pPhysicalDevices[0] = &g_phys_dev;
    *pPhysicalDeviceCount = 1;
    return VK_SUCCESS;
}

void vkGetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties* pProperties) {
    (void)physicalDevice;
    if (!pProperties) return;
    memset(pProperties, 0, sizeof(VkPhysicalDeviceProperties));
    pProperties->apiVersion = 0x00401000; // Vulkan 1.1
    pProperties->driverVersion = 1;
    pProperties->vendorID = 0x1002;
    pProperties->deviceID = 0x9999;
    pProperties->deviceType = 1; // Integrated / CPU
    strncpy(pProperties->deviceName, g_phys_dev.name, 255);
}

void vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice, uint32_t* pCount, VkQueueFamilyProperties* pProperties) {
    (void)physicalDevice;
    if (!pCount) return;
    if (!pProperties) {
        *pCount = 1;
        return;
    }
    pProperties[0].queueFlags = 0x07; // Graphics | Compute | Transfer
    pProperties[0].queueCount = 1;
    pProperties[0].timestampValidBits = 64;
    *pCount = 1;
}

VkResult vkCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo, const void* pAllocator, VkDevice* pDevice) {
    (void)pCreateInfo; (void)pAllocator;
    if (!pDevice) return VK_ERROR_INITIALIZATION_FAILED;
    g_device.phys_dev = physicalDevice;
    *pDevice = &g_device;
    return VK_SUCCESS;
}

void vkDestroyDevice(VkDevice device, const void* pAllocator) {
    (void)device; (void)pAllocator;
}

void vkGetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue* pQueue) {
    (void)queueFamilyIndex; (void)queueIndex;
    g_queue.device = device;
    *pQueue = &g_queue;
}

VkResult vkCreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo* pCreateInfo, const void* pAllocator, VkCommandPool* pCommandPool) {
    (void)pCreateInfo; (void)pAllocator;
    g_cmd_pool.device = device;
    *pCommandPool = &g_cmd_pool;
    return VK_SUCCESS;
}

void vkDestroyCommandPool(VkDevice device, VkCommandPool commandPool, const void* pAllocator) {
    (void)device; (void)commandPool; (void)pAllocator;
}

VkResult vkAllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo* pAllocateInfo, VkCommandBuffer* pCommandBuffers) {
    (void)device; (void)pAllocateInfo;
    g_cmd_buf.pool = &g_cmd_pool;
    pCommandBuffers[0] = &g_cmd_buf;
    return VK_SUCCESS;
}

VkResult vkBeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo* pBeginInfo) {
    (void)commandBuffer; (void)pBeginInfo;
    return VK_SUCCESS;
}

VkResult vkEndCommandBuffer(VkCommandBuffer commandBuffer) {
    (void)commandBuffer;
    return VK_SUCCESS;
}

VkResult vkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo, const void* pAllocator, VkSwapchainKHR* pSwapchain) {
    (void)pAllocator;
    if (gfx_init() != 0) return VK_ERROR_INITIALIZATION_FAILED;
    g_swapchain.device = device;
    g_swapchain.width = pCreateInfo ? pCreateInfo->imageExtent.width : gfx_get_width();
    g_swapchain.height = pCreateInfo ? pCreateInfo->imageExtent.height : gfx_get_height();
    *pSwapchain = &g_swapchain;
    return VK_SUCCESS;
}

void vkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const void* pAllocator) {
    (void)device; (void)swapchain; (void)pAllocator;
}

VkResult vkQueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits, void* fence) {
    (void)queue; (void)submitCount; (void)pSubmits; (void)fence;
    return VK_SUCCESS;
}

VkResult vkQueueWaitIdle(VkQueue queue) {
    (void)queue;
    return VK_SUCCESS;
}

VkResult vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo) {
    (void)queue; (void)pPresentInfo;
    gfx_flip();
    return VK_SUCCESS;
}
