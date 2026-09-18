#include "switch/nxvk.hpp"

#include <cstdio>
#include <cstring>
#include <cinttypes>

#ifdef __SWITCH__
#include <vulkan/vulkan.h>
#include "vk_dispatch_table.h"
#include "vk_common_entrypoints.h"
#include "wsi_common_entrypoints.h"
#endif

#ifdef __SWITCH__
extern "C" {

struct vk_physical_device_entrypoint_table __wrap_nvk_physical_device_entrypoints;
struct vk_device_entrypoint_table __wrap_nvk_device_entrypoints;
}
#endif

namespace dusk::sw {

#ifdef __SWITCH__

extern "C" {

extern const struct vk_physical_device_entrypoint_table __real_nvk_physical_device_entrypoints;
extern const struct vk_device_entrypoint_table __real_nvk_device_entrypoints;

}

static struct vk_physical_device_dispatch_table g_pdDispatch;
static struct vk_device_dispatch_table g_devDispatch;

PFN_vkVoidFunction nvk_lookup_physical_device_proc(const char* name) {
  if (name == nullptr) {
    return nullptr;
  }
  return vk_physical_device_dispatch_table_get(&g_pdDispatch, name);
}

PFN_vkVoidFunction nvk_lookup_device_proc(const char* name) {
  if (name == nullptr) {
    return nullptr;
  }
  return vk_device_dispatch_table_get(&g_devDispatch, name);
}

PFN_vkVoidFunction nvk_lookup_instance_proc(const char* name) {
  if (name == nullptr) {
    return nullptr;
  }
  PFN_vkVoidFunction f = nvk_lookup_physical_device_proc(name);
  if (f != nullptr) {
    return f;
  }
  return nvk_lookup_device_proc(name);
}
#endif

void nvk_dispatch_fixup() {
#ifdef __SWITCH__
  static bool done = false;
  if (done) {
    return;
  }
  done = true;

  vk_physical_device_dispatch_table_from_entrypoints(
      &g_pdDispatch, &__real_nvk_physical_device_entrypoints, true);
  vk_physical_device_dispatch_table_from_entrypoints(
      &g_pdDispatch, &wsi_physical_device_entrypoints, false);
  vk_physical_device_dispatch_table_from_entrypoints(
      &g_pdDispatch, &vk_common_physical_device_entrypoints, false);
  vk_device_dispatch_table_from_entrypoints(
      &g_devDispatch, &__real_nvk_device_entrypoints, true);
  vk_device_dispatch_table_from_entrypoints(
      &g_devDispatch, &wsi_device_entrypoints, false);
  vk_device_dispatch_table_from_entrypoints(
      &g_devDispatch, &vk_common_device_entrypoints, false);

  struct vk_physical_device_entrypoint_table* pdEntry = &__wrap_nvk_physical_device_entrypoints;
  struct vk_device_entrypoint_table* devEntry = &__wrap_nvk_device_entrypoints;

  memcpy(pdEntry, &__real_nvk_physical_device_entrypoints, sizeof(*pdEntry));
  memcpy(devEntry, &__real_nvk_device_entrypoints, sizeof(*devEntry));
#define WRITEBACK_PD(member, pfn) \
    do { \
      if (pdEntry->member == NULL) { \
        pdEntry->member = reinterpret_cast<pfn>( \
            vk_physical_device_dispatch_table_get(&g_pdDispatch, "vk" #member)); \
      } \
    } while (0)
  WRITEBACK_PD(GetPhysicalDeviceProperties, PFN_vkGetPhysicalDeviceProperties);
  WRITEBACK_PD(GetPhysicalDeviceQueueFamilyProperties, PFN_vkGetPhysicalDeviceQueueFamilyProperties);
  WRITEBACK_PD(GetPhysicalDeviceMemoryProperties, PFN_vkGetPhysicalDeviceMemoryProperties);
  WRITEBACK_PD(GetPhysicalDeviceFeatures, PFN_vkGetPhysicalDeviceFeatures);
  WRITEBACK_PD(GetPhysicalDeviceFormatProperties, PFN_vkGetPhysicalDeviceFormatProperties);
  WRITEBACK_PD(GetPhysicalDeviceImageFormatProperties, PFN_vkGetPhysicalDeviceImageFormatProperties);
  WRITEBACK_PD(EnumerateDeviceExtensionProperties, PFN_vkEnumerateDeviceExtensionProperties);
  WRITEBACK_PD(GetPhysicalDeviceFeatures2, PFN_vkGetPhysicalDeviceFeatures2);
  WRITEBACK_PD(GetPhysicalDeviceFeatures2KHR, PFN_vkGetPhysicalDeviceFeatures2KHR);
  WRITEBACK_PD(GetPhysicalDeviceExternalSemaphoreProperties, PFN_vkGetPhysicalDeviceExternalSemaphoreProperties);
  WRITEBACK_PD(GetPhysicalDeviceExternalSemaphorePropertiesKHR, PFN_vkGetPhysicalDeviceExternalSemaphorePropertiesKHR);
  WRITEBACK_PD(GetPhysicalDeviceExternalFenceProperties, PFN_vkGetPhysicalDeviceExternalFenceProperties);
  WRITEBACK_PD(GetPhysicalDeviceExternalFencePropertiesKHR, PFN_vkGetPhysicalDeviceExternalFencePropertiesKHR);
  WRITEBACK_PD(GetPhysicalDeviceToolProperties, PFN_vkGetPhysicalDeviceToolProperties);
  WRITEBACK_PD(GetPhysicalDeviceProperties2, PFN_vkGetPhysicalDeviceProperties2);
  WRITEBACK_PD(GetPhysicalDeviceProperties2KHR, PFN_vkGetPhysicalDeviceProperties2KHR);
  WRITEBACK_PD(GetPhysicalDeviceToolPropertiesEXT, PFN_vkGetPhysicalDeviceToolPropertiesEXT);
#undef WRITEBACK_PD
  if (devEntry->CmdBlitImage == NULL) {
    devEntry->CmdBlitImage = reinterpret_cast<PFN_vkCmdBlitImage>(vk_device_dispatch_table_get(&g_devDispatch, "vkCmdBlitImage"));
  }
  if (devEntry->CmdBlitImage == NULL) {
    // Mesa 26.2+ removed the driver v1 blit entry and the generated dispatch
    // tables cannot be relied on to carry it (header/lib skew). The common
    // v1->v2 converter (vk_cmd_copy.c) is still linked into libnvk, so serve
    // it directly. Dawn requires vkCmdBlitImage at device creation.
    devEntry->CmdBlitImage = vk_common_CmdBlitImage;
  }
#else
  (void)0;
#endif
}

}

#ifdef __SWITCH__
extern "C" {

VkResult __real_nvk_enumerate_physical_device(void* vk_instance);
VkResult __wrap_nvk_enumerate_physical_device(void* vk_instance) {
  VkResult r = __real_nvk_enumerate_physical_device(vk_instance);
  return r;
}

VkResult __real_nvkmd_try_create_pdev(void* log_obj, int debug_flags,
                                      void** pdev_out);
VkResult __wrap_nvkmd_try_create_pdev(void* log_obj, int debug_flags,
                                      void** pdev_out) {
  VkResult r = __real_nvkmd_try_create_pdev(log_obj, debug_flags, pdev_out);
  return r;
}

void* __real_nak_compiler_create(const void* dev_info);
void* __wrap_nak_compiler_create(const void* dev_info) {
  void* p = __real_nak_compiler_create(dev_info);
  return p;
}

VkResult __real_nvk_init_wsi(void* pdev);
VkResult __wrap_nvk_init_wsi(void* pdev) {
  VkResult r = __real_nvk_init_wsi(pdev);
  return r;
}

VkResult __real_wsi_device_init(void* wsi, uint64_t pdevice, void* proc_addr,
                                const void* alloc, int display_fd,
                                const void* dri_options,
                                const void* device_options);
VkResult __wrap_wsi_device_init(void* wsi, uint64_t pdevice, void* proc_addr,
                                const void* alloc, int display_fd,
                                const void* dri_options,
                                const void* device_options) {
  VkResult r = __real_wsi_device_init(wsi, pdevice, proc_addr, alloc,
                                      display_fd, dri_options, device_options);
  return r;
}

VKAPI_ATTR VkResult VKAPI_CALL
dusk_nvk_EnumerateDeviceLayerProperties(VkPhysicalDevice physicalDevice,
                                        uint32_t* pPropertyCount,
                                        VkLayerProperties* pProperties) {
  (void)physicalDevice;
  (void)pProperties;
  if (pPropertyCount != nullptr) {
    *pPropertyCount = 0;
  }
  return VK_SUCCESS;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
__real_vk_icdGetInstanceProcAddr(VkInstance instance, const char* pName);

typedef PFN_vkVoidFunction(VKAPI_PTR* PFN_DuskGetDeviceProcAddr)(VkDevice,
                                                                 const char*);
static PFN_DuskGetDeviceProcAddr g_real_GetDeviceProcAddr = nullptr;
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
dusk_nvk_GetDeviceProcAddr(VkDevice device, const char* pName);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
__wrap_vk_icdGetInstanceProcAddr(VkInstance instance, const char* pName) {
  if (pName != nullptr) {
    if (strcmp(pName, "vkEnumerateDeviceLayerProperties") == 0) {
      return reinterpret_cast<PFN_vkVoidFunction>(
          &dusk_nvk_EnumerateDeviceLayerProperties);
    }
    if (strcmp(pName, "vkGetDeviceProcAddr") == 0) {
      g_real_GetDeviceProcAddr = reinterpret_cast<PFN_DuskGetDeviceProcAddr>(
          __real_vk_icdGetInstanceProcAddr(instance, pName));
      return reinterpret_cast<PFN_vkVoidFunction>(&dusk_nvk_GetDeviceProcAddr);
    }
  }
  PFN_vkVoidFunction r = __real_vk_icdGetInstanceProcAddr(instance, pName);
  if (r == nullptr && pName != nullptr) {
    r = dusk::sw::nvk_lookup_instance_proc(pName);
  }
  return r;
}

void __real_util_gpuvis_init(void);
void __wrap_util_gpuvis_init(void) {
  __real_util_gpuvis_init();
}

const char* __real_os_get_option(const char* name);
const char* __wrap_os_get_option(const char* name) {
  return __real_os_get_option(name);
}

PFN_vkVoidFunction __real_vk_instance_get_proc_addr_unchecked(const void* instance,
                                                                  const char* name);
PFN_vkVoidFunction __wrap_vk_instance_get_proc_addr_unchecked(
    const void* instance, const char* name) {
  return __real_vk_instance_get_proc_addr_unchecked(instance, name);
}

uint64_t __real_parse_debug_string(const char* str, const void* control);
uint64_t __wrap_parse_debug_string(const char* str, const void* control) {
  uint64_t r = __real_parse_debug_string(str, control);
  return r;
}

typedef PFN_vkVoidFunction(VKAPI_PTR* PFN_DuskGetDeviceProcAddr)(VkDevice,
                                                                 const char*);

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
dusk_nvk_GetDeviceProcAddr(VkDevice device, const char* pName) {
  PFN_vkVoidFunction r = nullptr;
  if (g_real_GetDeviceProcAddr != nullptr) {
    r = g_real_GetDeviceProcAddr(device, pName);
  }
  if (r == nullptr && pName != nullptr) {
    r = dusk::sw::nvk_lookup_device_proc(pName);
  }
  if (r == nullptr && pName != nullptr && strcmp(pName, "vkCmdBlitImage") == 0) {
    // Same fallback as nvk_dispatch_fixup(): Mesa 26.2+ dropped the driver v1
    // blit entry; serve the linked common v1->v2 converter directly.
    r = reinterpret_cast<PFN_vkVoidFunction>(vk_common_CmdBlitImage);
  }
  return r;
}

}
#endif

