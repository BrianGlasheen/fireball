#pragma once

#include "vk_types.h"

#include <vulkan/vk_enum_string_helper.h>

#include <functional>

class Vk_Backend {
public:
	VkDevice _device; // Vulkan device for commands
	VmaAllocator _allocator;

	VkFence _immFence;
	VkCommandBuffer _immCommandBuffer;
	VkCommandPool _immCommandPool;

	VkQueue _graphicsQueue;
	uint32_t _graphicsQueueFamily;

	void init(VkDevice device, VmaAllocator allocator);

	// texutre maanger
	AllocatedImage create_image(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
	AllocatedImage create_image(void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
	void destroy_image(const AllocatedImage& img);
	//////////////////////////////

	AllocatedBuffer create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
	void destroy_buffer(const AllocatedBuffer& buffer);

	void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function);
};