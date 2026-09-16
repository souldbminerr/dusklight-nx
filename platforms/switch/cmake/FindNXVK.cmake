# FindNXVK
# NXVK_ROOT or DEVKITPRO/portlibs/switch

set(NXVK_ROOT "" CACHE PATH "NXVK root (portlib or source tree)")

set(_nxvk_roots "")
if(NXVK_ROOT)
  list(APPEND _nxvk_roots "${NXVK_ROOT}")
endif()
if(DEFINED ENV{DEVKITPRO})
  list(APPEND _nxvk_roots "$ENV{DEVKITPRO}/portlibs/switch")
endif()

find_path(NXVK_INCLUDE_DIR
  NAMES vulkan/vulkan.h
  PATHS ${_nxvk_roots}
  PATH_SUFFIXES include
  NO_DEFAULT_PATH
)

find_library(NXVK_LIB
  NAMES nvk
  PATHS ${_nxvk_roots}
  PATH_SUFFIXES lib switch/build/cross/src/nouveau/vulkan
  NO_DEFAULT_PATH
)

include(FindPackageHandleStandardArgs)
find_library(NXVK_SUPPORT_LIB
  NAMES nvk_support
  PATHS ${_nxvk_roots}
  PATH_SUFFIXES lib
  NO_DEFAULT_PATH
)

find_package_handle_standard_args(NXVK
  REQUIRED_VARS NXVK_LIB NXVK_SUPPORT_LIB NXVK_INCLUDE_DIR
)

if(NXVK_FOUND)
  set(NXVK_INCLUDE_DIRS "${NXVK_INCLUDE_DIR}")
  set(NXVK_LIBRARIES "${NXVK_LIB}")
  if(NXVK_SUPPORT_LIB)
    list(APPEND NXVK_LIBRARIES "${NXVK_SUPPORT_LIB}")
  endif()
  set(_switch_mesa_keep -Wl,-u,vk_icdGetInstanceProcAddr -Wl,-u,vk_common_AllocateCommandBuffers -Wl,-u,vk_common_BindBufferMemory -Wl,-u,vk_common_BindImageMemory -Wl,-u,vk_common_CmdBeginQuery -Wl,-u,vk_common_CmdBeginRenderPass -Wl,-u,vk_common_CmdBindDescriptorSets -Wl,-u,vk_common_CmdBindIndexBuffer -Wl,-u,vk_common_CmdBindPipeline -Wl,-u,vk_common_CmdBindVertexBuffers -Wl,-u,vk_common_CmdBlitImage -Wl,-u,vk_common_CmdCopyBuffer -Wl,-u,vk_common_CmdCopyBufferToImage -Wl,-u,vk_common_CmdCopyImage -Wl,-u,vk_common_CmdCopyImageToBuffer -Wl,-u,vk_common_CmdDispatch -Wl,-u,vk_common_CmdDispatchIndirect -Wl,-u,vk_common_CmdDrawIndexedIndirect -Wl,-u,vk_common_CmdDrawIndexedIndirectCount -Wl,-u,vk_common_CmdDrawIndirect -Wl,-u,vk_common_CmdDrawIndirectCount -Wl,-u,vk_common_CmdEndQuery -Wl,-u,vk_common_CmdEndRenderPass -Wl,-u,vk_common_CmdEndRendering -Wl,-u,vk_common_CmdNextSubpass -Wl,-u,vk_common_CmdPipelineBarrier -Wl,-u,vk_common_CmdPushConstants -Wl,-u,vk_common_CmdResetEvent -Wl,-u,vk_common_CmdResolveImage -Wl,-u,vk_common_CmdSetBlendConstants -Wl,-u,vk_common_CmdSetCullMode -Wl,-u,vk_common_CmdSetDepthBias -Wl,-u,vk_common_CmdSetDepthBounds -Wl,-u,vk_common_CmdSetDepthCompareOp -Wl,-u,vk_common_CmdSetDepthTestEnable -Wl,-u,vk_common_CmdSetDepthWriteEnable -Wl,-u,vk_common_CmdSetEvent -Wl,-u,vk_common_CmdSetFrontFace -Wl,-u,vk_common_CmdSetLineWidth -Wl,-u,vk_common_CmdSetPrimitiveTopology -Wl,-u,vk_common_CmdSetScissor -Wl,-u,vk_common_CmdSetStencilCompareMask -Wl,-u,vk_common_CmdSetStencilOp -Wl,-u,vk_common_CmdSetStencilReference -Wl,-u,vk_common_CmdSetStencilTestEnable -Wl,-u,vk_common_CmdSetStencilWriteMask -Wl,-u,vk_common_CmdSetViewport -Wl,-u,vk_common_CmdWaitEvents -Wl,-u,vk_common_CmdWriteTimestamp -Wl,-u,vk_common_CreateComputePipelines -Wl,-u,vk_common_CreateFence -Wl,-u,vk_common_CreateFramebuffer -Wl,-u,vk_common_CreateGraphicsPipelines -Wl,-u,vk_common_CreatePipelineCache -Wl,-u,vk_common_CreatePipelineLayout -Wl,-u,vk_common_CreateRenderPass -Wl,-u,vk_common_CreateRenderPass2 -Wl,-u,vk_common_CreateSemaphore -Wl,-u,vk_common_CreateShaderModule -Wl,-u,vk_common_DestroyDescriptorSetLayout -Wl,-u,vk_common_DestroyFence -Wl,-u,vk_common_DestroyFramebuffer -Wl,-u,vk_common_DestroyPipeline -Wl,-u,vk_common_DestroyPipelineCache -Wl,-u,vk_common_DestroyPipelineLayout -Wl,-u,vk_common_DeviceWaitIdle -Wl,-u,vk_common_EnumerateDeviceExtensionProperties -Wl,-u,vk_common_EnumerateDeviceLayerProperties -Wl,-u,vk_common_EnumeratePhysicalDeviceGroups -Wl,-u,vk_common_EnumeratePhysicalDevices -Wl,-u,vk_common_FreeCommandBuffers -Wl,-u,vk_common_GetBufferMemoryRequirements -Wl,-u,vk_common_GetDeviceQueue -Wl,-u,vk_common_GetDeviceQueue2 -Wl,-u,vk_common_GetFenceStatus -Wl,-u,vk_common_GetImageMemoryRequirements -Wl,-u,vk_common_GetImageSparseMemoryRequirements -Wl,-u,vk_common_GetImageSubresourceLayout -Wl,-u,vk_common_GetPhysicalDeviceExternalFenceProperties -Wl,-u,vk_common_GetPhysicalDeviceExternalSemaphoreProperties -Wl,-u,vk_common_GetPhysicalDeviceFeatures -Wl,-u,vk_common_GetPhysicalDeviceFeatures2 -Wl,-u,vk_common_GetPhysicalDeviceFormatProperties -Wl,-u,vk_common_GetPhysicalDeviceImageFormatProperties -Wl,-u,vk_common_GetPhysicalDeviceMemoryProperties -Wl,-u,vk_common_GetPhysicalDeviceProperties -Wl,-u,vk_common_GetPhysicalDeviceProperties2 -Wl,-u,vk_common_GetPhysicalDeviceQueueFamilyProperties -Wl,-u,vk_common_GetPhysicalDeviceSparseImageFormatProperties -Wl,-u,vk_common_GetPipelineCacheData -Wl,-u,vk_common_GetRenderAreaGranularity -Wl,-u,vk_common_GetSemaphoreFdKHR -Wl,-u,vk_common_ImportSemaphoreFdKHR -Wl,-u,vk_common_MapMemory -Wl,-u,vk_common_MergePipelineCaches -Wl,-u,vk_common_QueueBindSparse -Wl,-u,vk_common_QueueSubmit -Wl,-u,vk_common_QueueWaitIdle -Wl,-u,vk_common_ResetCommandBuffer -Wl,-u,vk_common_ResetCommandPool -Wl,-u,vk_common_ResetFences -Wl,-u,vk_common_UnmapMemory -Wl,-u,vk_common_WaitForFences)
  if(NOT TARGET NXVK::nvk)
    add_library(NXVK::nvk UNKNOWN IMPORTED)
    set_target_properties(NXVK::nvk PROPERTIES
      IMPORTED_LOCATION "${NXVK_LIB}"
      INTERFACE_INCLUDE_DIRECTORIES "${NXVK_INCLUDE_DIR}"
      INTERFACE_COMPILE_DEFINITIONS "__SWITCH__;VK_USE_PLATFORM_VI_NN"
      INTERFACE_LINK_OPTIONS "-Wl,--whole-archive;${_switch_mesa_keep};-Wl,--no-whole-archive;-Wl,--gc-sections"
    )
    # Mesa support libs (nir/util/blake3/ralloc/...) that libnvk needs.
    # Mesa xmlconfig (in nvk_support) needs expat (XML_*).
    set(_switch_expat "$ENV{DEVKITPRO}/portlibs/switch/lib/libexpat.a")
    if(EXISTS "${_switch_expat}")
      set_target_properties(NXVK::nvk PROPERTIES
        INTERFACE_LINK_LIBRARIES "${NXVK_SUPPORT_LIB};${_switch_expat}"
      )
    else()
      set_target_properties(NXVK::nvk PROPERTIES
        INTERFACE_LINK_LIBRARIES "${NXVK_SUPPORT_LIB}"
      )
    endif()
    unset(_switch_expat)
  endif()
endif()

mark_as_advanced(NXVK_INCLUDE_DIR NXVK_LIB)
