#pragma once

#include "vk_types.h"

#include <vulkan/vk_enum_string_helper.h>
#include <GLFW/glfw3.h>

#include <functional>

constexpr uint32_t FRAME_OVERLAP = 2;

class Vk_Backend {
public:
	VkDevice _device;
	VmaAllocator _allocator;
	VkInstance _instance;
	VkPhysicalDevice _chosenGPU;
	
	VkQueue _graphicsQueue;
	uint32_t _graphicsQueueFamily;
	
	VkDebugUtilsMessengerEXT _debug_messenger;
	
	VkSurfaceKHR _surface;
	VkSwapchainKHR _swapchain;
	VkExtent2D _swapchainExtent;
	VkFormat _swapchainImageFormat;
	std::vector<VkImage> _swapchainImages;
	std::vector<VkImageView> _swapchainImageViews;
	uint32_t _swapchainImageCount;

	FrameData _frames[FRAME_OVERLAP];
	std::vector<VkSemaphore> _readyForPresentSemaphores;
	uint32_t _swapchainImageIndex;
	uint32_t _frameNumber = 0;

	AllocatedImage _drawImage;
	AllocatedImage _depthImage;
	VkExtent2D _drawExtent;

	DescriptorAllocator globalDescriptorAllocator;
	VkDescriptorSet _drawImageDescriptors;
	VkDescriptorSetLayout _drawImageDescriptorLayout;

	VkFence _immFence;
	VkCommandBuffer _immCommandBuffer;
	VkCommandPool _immCommandPool;

	DeletionQueue _mainDeletionQueue;

	bool resize_requested = false;
	uint32_t width, height;

	// drawing stuff
	VkPipelineLayout draw_pipeline_layout;
	VkPipeline opaque_pipeline;
	VkPipeline transparent_pipeline;

	GPU_Push_Constants gpu_push_constants;
	Allocated_Buffer opaque_command_buffer;
	Allocated_Buffer transparent_command_buffer;
	uint32_t opaque_count;
	uint32_t transparent_count;
	GPU_Mesh_Buffers geometry_buffer;

	int init(GLFWwindow* window, uint32_t width, uint32_t height, bool validation_layers);
	int init_vulkan(GLFWwindow* window, bool validation_layers);
	void create_swapchain(uint32_t width, uint32_t height);
	void init_swapchain(uint32_t width, uint32_t height);
	void destroy_swapchain();
	void resize_swapchain(GLFWwindow* window);
	void init_sync_structures();
	void init_commands();
	void init_descriptors();
	void init_bindless_descriptors();
	void init_background_pipelines();
	void init_draw_pipeline();
	void init_pipelines();
	void init_imgui(GLFWwindow* window);
	void cleanup();

	void render(const mat4& projection, const mat4& view);
	void draw_background(VkCommandBuffer cmd);
	void draw_geometry(VkCommandBuffer cmd, const mat4& projection, const mat4& view);

	Allocated_Buffer create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
	void destroy_buffer(const Allocated_Buffer& buffer);
	void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function);

	FrameData& get_current_frame();
};