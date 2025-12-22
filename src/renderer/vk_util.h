#pragma once

#include <vulkan/vk_enum_string_helper.h>

#include <deque>
#include <functional>
#include <span>

#define VK_CHECK(x)																  \
    do {																		  \
        VkResult err = x;														  \
        if (err) {																  \
            fprintf(stderr, "Detected Vulkan error: %s\n", string_VkResult(err)); \
            abort();															  \
        }																		  \
    } while (0)

void check_vk_result(VkResult err);

VkImageCreateInfo image_create_info(VkFormat format, VkImageUsageFlags usageFlags, VkExtent3D extent);
VkImageViewCreateInfo imageview_create_info(VkFormat format, VkImage image, VkImageAspectFlags aspectFlags);
VkCommandBufferAllocateInfo command_buffer_allocate_info(VkCommandPool pool, uint32_t count /*= 1*/, VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);
VkFenceCreateInfo fence_create_info(VkFenceCreateFlags flags /*= 0*/);
VkSemaphoreCreateInfo semaphore_create_info(VkSemaphoreCreateFlags flags = 0);
VkCommandBufferBeginInfo command_buffer_begin_info(VkCommandBufferUsageFlags flags /*= 0*/);
VkImageSubresourceRange image_subresource_range(VkImageAspectFlags aspectMask);
VkSemaphoreSubmitInfo semaphore_submit_info(VkPipelineStageFlags2 stageMask, VkSemaphore semaphore);
VkCommandBufferSubmitInfo command_buffer_submit_info(VkCommandBuffer cmd);
VkSubmitInfo2 submit_info(VkCommandBufferSubmitInfo* cmd, VkSemaphoreSubmitInfo* signalSemaphoreInfo, VkSemaphoreSubmitInfo* waitSemaphoreInfo);
VkPipelineLayoutCreateInfo pipeline_layout_create_info();
VkRenderingAttachmentInfo attachment_info(VkImageView view, VkClearValue* clear, VkImageLayout layout /*= VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL*/);
VkRenderingAttachmentInfo depth_attachment_info(VkImageView view, VkImageLayout layout /*= VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL*/);
VkRenderingInfo rendering_info(VkExtent2D renderExtent, VkRenderingAttachmentInfo* colorAttachment, VkRenderingAttachmentInfo* depthAttachment);

void transition_image(VkCommandBuffer cmd, VkImage image, VkImageLayout currentLayout, VkImageLayout newLayout);
void copy_image_to_image(VkCommandBuffer cmd, VkImage source, VkImage destination, VkExtent2D srcSize, VkExtent2D dstSize);
bool load_shader_module(const char* filePath, VkDevice device, VkShaderModule * outShaderModule);
