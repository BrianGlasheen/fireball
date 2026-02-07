#pragma once

#include "asset/model_manager.h" // Vertex
#include "renderer/vk_debug_backend.h"
#include "renderer/vk_types.h"
#include "scene/entity.h"
#include "util/types.h"

#include <vulkan/vk_enum_string_helper.h>
#include <GLFW/glfw3.h>

#include <functional>
#include <span>

constexpr uint32_t FRAME_OVERLAP = 2;
constexpr uint32_t MAX_DRAW_COMMANDS = 100'000;
constexpr uint32_t MAX_LIGHTS = 4096;

struct Command_Counts {
	uint32_t opaque;
	uint32_t transparent;
	// CSM
};

struct GPU_Light {
	vec4 position_radius; // x, y ,z, radius
	vec4 color_strength; // r g b intensity
	vec4 direction_type; // x y z type
	vec4 params; // inner cone, outer cone, shadow map idx, enabled 
};

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
	//DescriptorAllocatorGrowable globalDescriptorAllocator;
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
	//Allocated_Buffer csm_command_buffer; multiple of these?
	Allocated_Buffer command_count_buffer;

	Allocated_Buffer light_buffer; // TODO per fif
	uint32_t num_lights;
	std::vector<uint32_t> light_free_list;
	std::unordered_map<ecs_entity_t, uint32_t> light_allocations;

	VkPipeline mesh_cull_pipeline;
	VkPipelineLayout mesh_cull_pipeline_layout;
	VkDescriptorSetLayout mesh_cull_descriptor_layout;
	VkDescriptorSet mesh_cull_descriptor_set;

	Allocated_Buffer vertex_buffer;
	Allocated_Buffer index_buffer;

	Range_Allocator mesh_allocator { MAX_DRAW_COMMANDS };
	std::unordered_map<ecs_entity_t, Range_Allocation> mesh_allocations;
	Allocated_Buffer mesh_buffer;
	Allocated_Buffer mesh_render_info_buffer;
	Allocated_Buffer transform_buffer; // TODO per fif
	Allocated_Buffer material_buffer;

	Vk_Debug_Backend debug_renderer;

	vector<Render_Graph_Node> render_graph;
	mat4 frame_proj; // TODO HACK
	mat4 frame_view; // TODO RM
	uint32_t current_swapchain_index; // same

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
	void init_draw_buffers();
	void init_light_buffer();

	void init_background_pipelines();
	void init_draw_pipeline();

	void init_mesh_cull_pipeline();
	void init_mesh_cull_descriptors();
	
	void init_pipelines();
	
	void init_imgui(GLFWwindow* window);

	void init_render_graph();

	void upload_geometry(std::span<uint32_t> indices, std::span<Vertex> vertices);
	void allocate_model(Entity e, Model_Handle handle);
	void update_meshes(Entity e, Model_Handle handle);
	void deallocate_model(Entity e);
	void allocate_light(Entity e, GPU_Light light);
	void update_light(Entity e, GPU_Light light);
	void deallocate_light(Entity e);

	void update_buffer_range(Allocated_Buffer& buffer, size_t element_size, uint32_t start_index, const void* data, uint32_t count);

	void cleanup();

	void render(const mat4& projection, const mat4& view);
	void generate_draw_commands(VkCommandBuffer cmd);
	void draw_background(VkCommandBuffer cmd);
	void draw_geometry(VkCommandBuffer cmd, const mat4& projection, const mat4& view);

	Allocated_Buffer create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
	void destroy_buffer(const Allocated_Buffer& buffer);
	void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function);

	FrameData& get_current_frame();
};