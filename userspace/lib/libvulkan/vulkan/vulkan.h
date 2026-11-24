#ifndef VULKAN_H_
#define VULKAN_H_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VKAPI_PTR
#define VK_DEFINE_HANDLE(object) typedef struct object##_T* object;

typedef uint32_t VkFlags;
typedef uint32_t VkBool32;
typedef uint64_t VkDeviceSize;

#define VK_TRUE  1
#define VK_FALSE 0

typedef enum VkResult {
    VK_SUCCESS = 0,
    VK_NOT_READY = 1,
    VK_TIMEOUT = 2,
    VK_EVENT_SET = 3,
    VK_EVENT_RESET = 4,
    VK_INCOMPLETE = 5,
    VK_ERROR_OUT_OF_HOST_MEMORY = -1,
    VK_ERROR_OUT_OF_DEVICE_MEMORY = -2,
    VK_ERROR_INITIALIZATION_FAILED = -3,
    VK_ERROR_DEVICE_LOST = -4,
    VK_ERROR_EXTENSION_NOT_PRESENT = -7,
    VK_ERROR_FEATURE_NOT_PRESENT = -8
} VkResult;

typedef enum VkStructureType {
    VK_STRUCTURE_TYPE_APPLICATION_INFO = 0,
    VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO = 1,
    VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO = 2,
    VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO = 3,
    VK_STRUCTURE_TYPE_SUBMIT_INFO = 4,
    VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO = 5,
    VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO = 12,
    VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO = 14,
    VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO = 39,
    VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO = 40,
    VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO = 42,
    VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO = 43,
    VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR = 1000001000,
    VK_STRUCTURE_TYPE_PRESENT_INFO_KHR = 1000001001
} VkStructureType;

typedef enum VkFormat {
    VK_FORMAT_UNDEFINED = 0,
    VK_FORMAT_R8G8B8A8_UNORM = 37,
    VK_FORMAT_B8G8R8A8_UNORM = 44,
    VK_FORMAT_D32_SFLOAT = 126
} VkFormat;

VK_DEFINE_HANDLE(VkInstance)
VK_DEFINE_HANDLE(VkPhysicalDevice)
VK_DEFINE_HANDLE(VkDevice)
VK_DEFINE_HANDLE(VkQueue)
VK_DEFINE_HANDLE(VkCommandBuffer)
VK_DEFINE_HANDLE(VkCommandPool)
VK_DEFINE_HANDLE(VkBuffer)
VK_DEFINE_HANDLE(VkImage)
VK_DEFINE_HANDLE(VkSwapchainKHR)
VK_DEFINE_HANDLE(VkRenderPass)
VK_DEFINE_HANDLE(VkFramebuffer)
VK_DEFINE_HANDLE(VkPipeline)

typedef struct VkApplicationInfo {
    VkStructureType sType;
    const void*     pNext;
    const char*     pApplicationName;
    uint32_t        applicationVersion;
    const char*     pEngineName;
    uint32_t        engineVersion;
    uint32_t        apiVersion;
} VkApplicationInfo;

typedef struct VkInstanceCreateInfo {
    VkStructureType             sType;
    const void*                 pNext;
    VkFlags                     flags;
    const VkApplicationInfo*    pApplicationInfo;
    uint32_t                    enabledLayerCount;
    const char* const*          ppEnabledLayerNames;
    uint32_t                    enabledExtensionCount;
    const char* const*          ppEnabledExtensionNames;
} VkInstanceCreateInfo;

typedef struct VkPhysicalDeviceProperties {
    uint32_t apiVersion;
    uint32_t driverVersion;
    uint32_t vendorID;
    uint32_t deviceID;
    uint32_t deviceType;
    char     deviceName[256];
    uint8_t  pipelineCacheUUID[16];
    uint32_t limits[16];
} VkPhysicalDeviceProperties;

typedef struct VkQueueFamilyProperties {
    VkFlags  queueFlags;
    uint32_t queueCount;
    uint32_t timestampValidBits;
    uint32_t minImageTransferGranularity[3];
} VkQueueFamilyProperties;

typedef struct VkDeviceQueueCreateInfo {
    VkStructureType sType;
    const void*     pNext;
    VkFlags         flags;
    uint32_t        queueFamilyIndex;
    uint32_t        queueCount;
    const float*    pQueuePriorities;
} VkDeviceQueueCreateInfo;

typedef struct VkDeviceCreateInfo {
    VkStructureType                 sType;
    const void*                     pNext;
    VkFlags                         flags;
    uint32_t                        queueCreateInfoCount;
    const VkDeviceQueueCreateInfo*  pQueueCreateInfos;
    uint32_t                        enabledLayerCount;
    const char* const*              ppEnabledLayerNames;
    uint32_t                        enabledExtensionCount;
    const char* const*              ppEnabledExtensionNames;
    const void*                     pEnabledFeatures;
} VkDeviceCreateInfo;

typedef struct VkCommandPoolCreateInfo {
    VkStructureType sType;
    const void*     pNext;
    VkFlags         flags;
    uint32_t        queueFamilyIndex;
} VkCommandPoolCreateInfo;

typedef struct VkCommandBufferAllocateInfo {
    VkStructureType sType;
    const void*     pNext;
    VkCommandPool   commandPool;
    uint32_t        level;
    uint32_t        commandBufferCount;
} VkCommandBufferAllocateInfo;

typedef struct VkCommandBufferBeginInfo {
    VkStructureType sType;
    const void*     pNext;
    VkFlags         flags;
    const void*     pInheritanceInfo;
} VkCommandBufferBeginInfo;

typedef struct VkExtent2D {
    uint32_t width;
    uint32_t height;
} VkExtent2D;

typedef struct VkSwapchainCreateInfoKHR {
    VkStructureType sType;
    const void*     pNext;
    VkFlags         flags;
    void*           surface;
    uint32_t        minImageCount;
    VkFormat        imageFormat;
    uint32_t        imageColorSpace;
    VkExtent2D      imageExtent;
    uint32_t        imageArrayLayers;
    uint32_t        imageUsage;
} VkSwapchainCreateInfoKHR;

typedef struct VkSubmitInfo {
    VkStructureType sType;
    const void*     pNext;
    uint32_t        waitSemaphoreCount;
    const void*     pWaitSemaphores;
    const VkFlags*  pWaitDstStageMask;
    uint32_t        commandBufferCount;
    const VkCommandBuffer* pCommandBuffers;
    uint32_t        signalSemaphoreCount;
    const void*     pSignalSemaphores;
} VkSubmitInfo;

typedef struct VkPresentInfoKHR {
    VkStructureType          sType;
    const void*              pNext;
    uint32_t                 waitSemaphoreCount;
    const void*              pWaitSemaphores;
    uint32_t                 swapchainCount;
    const VkSwapchainKHR*    pSwapchains;
    const uint32_t*          pImageIndices;
    VkResult*                pResults;
} VkPresentInfoKHR;

typedef struct VkClearValue {
    union {
        float float32[4];
        int32_t int32[4];
        uint32_t uint32[4];
    } color;
    struct {
        float depth;
        uint32_t stencil;
    } depthStencil;
} VkClearValue;

typedef struct VkRenderPassBeginInfo {
    VkStructureType sType;
    const void*     pNext;
    VkRenderPass    renderPass;
    VkFramebuffer   framebuffer;
    struct {
        int32_t x, y;
        uint32_t width, height;
    } renderArea;
    uint32_t        clearValueCount;
    const VkClearValue* pClearValues;
} VkRenderPassBeginInfo;

typedef struct VkViewport {
    float x, y, width, height, minDepth, maxDepth;
} VkViewport;

typedef struct VkRect2D {
    struct { int32_t x, y; } offset;
    VkExtent2D extent;
} VkRect2D;

// Core Functions
VkResult vkCreateInstance(const VkInstanceCreateInfo* pCreateInfo, const void* pAllocator, VkInstance* pInstance);
void     vkDestroyInstance(VkInstance instance, const void* pAllocator);

VkResult vkEnumeratePhysicalDevices(VkInstance instance, uint32_t* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices);
void     vkGetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties* pProperties);
void     vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice, uint32_t* pCount, VkQueueFamilyProperties* pProperties);

VkResult vkCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo, const void* pAllocator, VkDevice* pDevice);
void     vkDestroyDevice(VkDevice device, const void* pAllocator);
void     vkGetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue* pQueue);

VkResult vkCreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo* pCreateInfo, const void* pAllocator, VkCommandPool* pCommandPool);
void     vkDestroyCommandPool(VkDevice device, VkCommandPool commandPool, const void* pAllocator);
VkResult vkAllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo* pAllocateInfo, VkCommandBuffer* pCommandBuffers);

VkResult vkBeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo* pBeginInfo);
VkResult vkEndCommandBuffer(VkCommandBuffer commandBuffer);

VkResult vkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo, const void* pAllocator, VkSwapchainKHR* pSwapchain);
void     vkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const void* pAllocator);

VkResult vkQueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits, void* fence);
VkResult vkQueueWaitIdle(VkQueue queue);
VkResult vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo);

void vkCmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* pRenderPassBegin, uint32_t contents);
void vkCmdEndRenderPass(VkCommandBuffer commandBuffer);
void vkCmdBindPipeline(VkCommandBuffer commandBuffer, uint32_t pipelineBindPoint, VkPipeline pipeline);
void vkCmdSetViewport(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewport* pViewports);
void vkCmdSetScissor(VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const VkRect2D* pScissors);
void vkCmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance);

// KyroVK Software Rasterizer Triangle API
typedef struct {
    float x, y, z;
    uint32_t color;
} VkVertex3D;

void vkClearDepthBuffer(void);
void vkRasterizeTriangle(const VkVertex3D *v0, const VkVertex3D *v1, const VkVertex3D *v2);

#ifdef __cplusplus
}
#endif

#endif // VULKAN_H_
