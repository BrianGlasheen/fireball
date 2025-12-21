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

	Allocated_Buffer create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
	void destroy_buffer(const Allocated_Buffer& buffer);

	void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function);
};