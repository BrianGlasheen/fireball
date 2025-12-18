#include "camera.h"
#include "math.h"
#include "model_manager.h"
#include "pipeline.h"
#include "texture_manager.h"

#include "renderer/vk_util.h"
#include "renderer/vk_backend.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include <vulkan/vulkan.h>
#include <vulkan/vk_enum_string_helper.h>
#include <VkBootstrap.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <cstdio>
#include <cstdint>
#include <vector>
#include <deque>
#include <span>
#include <fstream>

static void check_vk_result(VkResult err) {
	if (err == VK_SUCCESS)
		return;
	fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
	if (err < 0)
		abort();
}

void mouseCallback(GLFWwindow* window, int button, int action, int mods) {
	ImGuiIO& io = ImGui::GetIO();
	if (io.WantCaptureMouse) {
		return;
	}

	if (action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_RIGHT) {
		glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
	}
	else if (action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_LEFT) {
		//glfwSetCursorPos(window, 0, 0);
		glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	}
}

void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
	if (action == GLFW_PRESS) {
		if (key == GLFW_KEY_ESCAPE)
			glfwSetWindowShouldClose(window, true);
	}
}

bool validation_layers = true;

float main_scale;
uint32_t width = 1280, height = 960;
bool resize_requested = false;
float renderScale = 1.0f;

Vk_Backend renderer;

VmaAllocator _allocator;

VkInstance _instance;// Vulkan library handle
VkDebugUtilsMessengerEXT _debug_messenger;// Vulkan debug output handle
VkPhysicalDevice _chosenGPU;// GPU chosen as the default device
VkDevice _device; // Vulkan device for commands
VkSurfaceKHR _surface;// Vulkan window surface

VkSwapchainKHR _swapchain;
VkFormat _swapchainImageFormat;

std::vector<VkImage> _swapchainImages;
std::vector<VkImageView> _swapchainImageViews;
VkExtent2D _swapchainExtent;

int lod = 0;

DeletionQueue _mainDeletionQueue;

constexpr unsigned int FRAME_OVERLAP = 2;
FrameData _frames[FRAME_OVERLAP];

uint32_t _swapchainImageCount = 0;
std::vector<VkSemaphore> _readyForPresentSemaphores;
uint32_t _swapchainImageIndex;

uint32_t _frameNumber = 0;
FrameData& get_current_frame() { return _frames[_frameNumber % FRAME_OVERLAP]; };

AllocatedImage _drawImage;
AllocatedImage _depthImage;
VkExtent2D _drawExtent;

struct ComputePushConstants {
	vec4 data1;
	vec4 data2;
	vec4 data3;
	vec4 data4;
};

struct ComputeEffect {
	const char* name;

	VkPipeline pipeline;
	VkPipelineLayout layout;

	ComputePushConstants data;
};

struct GPUDrawPushConstants {
	mat4 worldMatrix;
	VkDeviceAddress vertexBuffer;
	uint32_t bindless_texture;
	float alpha_cutoff;
	uint32_t blending;
};

std::vector<ComputeEffect> backgroundEffects;
int currentBackgroundEffect{ 0 };

DescriptorAllocator globalDescriptorAllocator;
VkDescriptorSet _drawImageDescriptors;
VkDescriptorSetLayout _drawImageDescriptorLayout;

AllocatedImage _whiteImage;
AllocatedImage _blackImage;
AllocatedImage _greyImage;
AllocatedImage _errorCheckerboardImage;

int init_vulkan(GLFWwindow *window) {
    vkb::InstanceBuilder builder;

	uint32_t ext_count = 0;
	const char** glfw_exts = glfwGetRequiredInstanceExtensions(&ext_count);

	for (uint32_t i = 0; i < ext_count; i++) {
		builder.enable_extension(glfw_exts[i]);
	}

	builder.enable_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    //make the vulkan instance, with basic debug features
    auto inst_ret = builder.set_app_name("Example Vulkan Application")
        .request_validation_layers(validation_layers)
        .use_default_debug_messenger()
        .require_api_version(1, 3, 0)
    .build();

    vkb::Instance vkb_inst = inst_ret.value();

    //grab the instance 
    _instance = vkb_inst.instance;
    _debug_messenger = vkb_inst.debug_messenger;

    //SDL_Vulkan_CreateSurface(_window, _instance, &_surface);
	VkResult err = glfwCreateWindowSurface(_instance, window, NULL, &_surface);
	if (err) {
		// Window surface creation failed
		printf("window surface creation failed\n");
		return 1;
	}

	// 1.4

	//vulkan 1.3 features
	VkPhysicalDeviceVulkan13Features features{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
	features.dynamicRendering = true;
	features.synchronization2 = true;

	//vulkan 1.2 features
	VkPhysicalDeviceVulkan12Features features12{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
	features12.bufferDeviceAddress = true;
	features12.descriptorIndexing = true;

	// bindless
	features12.runtimeDescriptorArray = true;
	features12.descriptorBindingPartiallyBound = true;

	features12.shaderStorageBufferArrayNonUniformIndexing = true;
	features12.shaderSampledImageArrayNonUniformIndexing = true;
	features12.shaderStorageImageArrayNonUniformIndexing = true;

	features12.descriptorBindingStorageBufferUpdateAfterBind = true;
	features12.descriptorBindingSampledImageUpdateAfterBind = true;
	features12.descriptorBindingStorageImageUpdateAfterBind = true;

	//use vkbootstrap to select a gpu. 
	//We want a gpu that can write to the SDL surface and supports vulkan 1.3 with the correct features
	vkb::PhysicalDeviceSelector selector{ vkb_inst };
	vkb::PhysicalDevice physicalDevice = selector
		.set_minimum_version(1, 3)
		.set_required_features_13(features)
		.set_required_features_12(features12)
		.set_surface(_surface)
		.select()
	.value();

	//create the final vulkan device
	vkb::DeviceBuilder deviceBuilder{ physicalDevice };

	vkb::Device vkbDevice = deviceBuilder.build().value();

	// Get the VkDevice handle used in the rest of a vulkan application
	_device = vkbDevice.device;
	_chosenGPU = physicalDevice.physical_device;

	renderer._graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
	renderer._graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

	// initialize the memory allocator
	VmaAllocatorCreateInfo allocatorInfo = {};
	allocatorInfo.physicalDevice = _chosenGPU;
	allocatorInfo.device = _device;
	allocatorInfo.instance = _instance;
	allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
	vmaCreateAllocator(&allocatorInfo, &_allocator);

	_mainDeletionQueue.push_function([&]() {
		vmaDestroyAllocator(_allocator);
	});

	return 0;
}

void create_swapchain(uint32_t width, uint32_t height) {
	vkb::SwapchainBuilder swapchainBuilder{ _chosenGPU, _device, _surface };

	_swapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;

	vkb::Swapchain vkbSwapchain = swapchainBuilder
		//.use_default_format_selection()
		.set_desired_format(VkSurfaceFormatKHR{ .format = _swapchainImageFormat, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
		//use vsync present mode
		.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
		.set_desired_extent(width, height)
		.add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
		.build()
		.value();

	_swapchainExtent = vkbSwapchain.extent;
	//store swapchain and its related images
	_swapchain = vkbSwapchain.swapchain;
	_swapchainImages = vkbSwapchain.get_images().value();
	_swapchainImageViews = vkbSwapchain.get_image_views().value();
}

void init_swapchain(uint32_t width, uint32_t height) {
	create_swapchain(width, height);

	// Set _swapchainImageCount to the amount of swapchain images - used to initialize the same amount of 
	// _readyForPresentSemaphores in init_sync_structures
	VK_CHECK(vkGetSwapchainImagesKHR(_device, _swapchain, &_swapchainImageCount, nullptr));

	//draw image size will match the window
	VkExtent3D drawImageExtent = { width, height, 1 };

	//hardcoding the draw format to 32 bit float
	_drawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	_drawImage.imageExtent = drawImageExtent;

	VkImageUsageFlags drawImageUsages{};
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	VkImageCreateInfo rimg_info = image_create_info(_drawImage.imageFormat, drawImageUsages, drawImageExtent);

	//for the draw image, we want to allocate it from gpu local memory
	VmaAllocationCreateInfo rimg_allocinfo = {};
	rimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	rimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	//allocate and create the image
	vmaCreateImage(_allocator, &rimg_info, &rimg_allocinfo, &_drawImage.image, &_drawImage.allocation, nullptr);

	//build a image-view for the draw image to use for rendering
	VkImageViewCreateInfo rview_info = imageview_create_info(_drawImage.imageFormat, _drawImage.image, VK_IMAGE_ASPECT_COLOR_BIT);

	VK_CHECK(vkCreateImageView(_device, &rview_info, nullptr, &_drawImage.imageView));

	// depth image
	_depthImage.imageFormat = VK_FORMAT_D32_SFLOAT;
	_depthImage.imageExtent = drawImageExtent;
	VkImageUsageFlags depthImageUsages{};
	depthImageUsages |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

	VkImageCreateInfo dimg_info = image_create_info(_depthImage.imageFormat, depthImageUsages, drawImageExtent);

	vmaCreateImage(_allocator, &dimg_info, &rimg_allocinfo, &_depthImage.image, &_depthImage.allocation, nullptr);

	//build a image-view for the draw image to use for rendering
	VkImageViewCreateInfo dview_info = imageview_create_info(_depthImage.imageFormat, _depthImage.image, VK_IMAGE_ASPECT_DEPTH_BIT);

	VK_CHECK(vkCreateImageView(_device, &dview_info, nullptr, &_depthImage.imageView));

	_mainDeletionQueue.push_function([=]() {
		vkDestroyImageView(_device, _drawImage.imageView, nullptr);
		vmaDestroyImage(_allocator, _drawImage.image, _drawImage.allocation);

		vkDestroyImageView(_device, _depthImage.imageView, nullptr);
		vmaDestroyImage(_allocator, _depthImage.image, _depthImage.allocation);
	});
}

void destroy_swapchain() {
	vkDestroySwapchainKHR(_device, _swapchain, nullptr);

	// destroy swapchain resources
	for (int i = 0; i < _swapchainImageViews.size(); i++) {
		vkDestroyImageView(_device, _swapchainImageViews[i], nullptr);
	}
}

void init_commands() {
	//create a command pool for commands submitted to the graphics queue.
	//we also want the pool to allow for resetting of individual command buffers
	VkCommandPoolCreateInfo commandPoolInfo = {};
	commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	commandPoolInfo.pNext = nullptr;
	commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	commandPoolInfo.queueFamilyIndex = renderer._graphicsQueueFamily;

	for (int i = 0; i < FRAME_OVERLAP; i++) {
		VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_frames[i]._commandPool));

		// allocate the default command buffer that we will use for rendering
		VkCommandBufferAllocateInfo cmdAllocInfo = {};
		cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		cmdAllocInfo.pNext = nullptr;
		cmdAllocInfo.commandPool = _frames[i]._commandPool;
		cmdAllocInfo.commandBufferCount = 1;
		cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

		VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_frames[i]._mainCommandBuffer));
	}

	VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &renderer._immCommandPool));

	// allocate the command buffer for immediate submits
	VkCommandBufferAllocateInfo cmdAllocInfo = command_buffer_allocate_info(renderer._immCommandPool, 1);

	VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &renderer._immCommandBuffer));

	_mainDeletionQueue.push_function([=]() {
		vkDestroyCommandPool(_device, renderer._immCommandPool, nullptr);
	});
}

void copy_image_to_image(VkCommandBuffer cmd, VkImage source, VkImage destination, VkExtent2D srcSize, VkExtent2D dstSize) {
	VkImageBlit2 blitRegion{ .sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2, .pNext = nullptr };

	blitRegion.srcOffsets[1].x = srcSize.width;
	blitRegion.srcOffsets[1].y = srcSize.height;
	blitRegion.srcOffsets[1].z = 1;

	blitRegion.dstOffsets[1].x = dstSize.width;
	blitRegion.dstOffsets[1].y = dstSize.height;
	blitRegion.dstOffsets[1].z = 1;

	blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blitRegion.srcSubresource.baseArrayLayer = 0;
	blitRegion.srcSubresource.layerCount = 1;
	blitRegion.srcSubresource.mipLevel = 0;

	blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blitRegion.dstSubresource.baseArrayLayer = 0;
	blitRegion.dstSubresource.layerCount = 1;
	blitRegion.dstSubresource.mipLevel = 0;

	VkBlitImageInfo2 blitInfo{ .sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2, .pNext = nullptr };
	blitInfo.dstImage = destination;
	blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	blitInfo.srcImage = source;
	blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	blitInfo.filter = VK_FILTER_LINEAR;
	blitInfo.regionCount = 1;
	blitInfo.pRegions = &blitRegion;

	vkCmdBlitImage2(cmd, &blitInfo);
}

void init_sync_structures() {
	//create syncronization structures
	//one fence to control when the gpu has finished rendering the frame,
	//and 2 semaphores to syncronize rendering with swapchain
	//we want the fence to start signalled so we can wait on it on the first frame
	VkFenceCreateInfo fenceCreateInfo = fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
	VkSemaphoreCreateInfo semaphoreCreateInfo = semaphore_create_info();

	for (int i = 0; i < FRAME_OVERLAP; i++) {
		VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_frames[i]._renderFence));

		// todo do i need? if swapchain image count semaphore
		VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i]._swapchainSemaphore));
		//VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i]._renderSemaphore));
	}

	_readyForPresentSemaphores.resize(_swapchainImageCount);
	for (int i = 0; i < _swapchainImageCount; i++) {
		VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_readyForPresentSemaphores[i]));
	}


	VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &renderer._immFence));
	_mainDeletionQueue.push_function([=]() { vkDestroyFence(_device, renderer._immFence, nullptr); });
}

VkDescriptorSetLayout _singleImageDescriptorLayout;

void init_descriptors() {
	std::vector<DescriptorAllocator::PoolSizeRatio> sizes = {
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 }
	};

	globalDescriptorAllocator.init_pool(_device, 10, sizes);

	//make the descriptor set layout for our compute draw
	{
		DescriptorLayoutBuilder builder;
		builder.add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		_drawImageDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_COMPUTE_BIT);
	}

	{
		DescriptorLayoutBuilder builder;
		builder.add_binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		_singleImageDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_FRAGMENT_BIT);
	}

	//allocate a descriptor set for our draw image
    _drawImageDescriptors = globalDescriptorAllocator.allocate(_device, _drawImageDescriptorLayout);
    {
        DescriptorWriter writer;	
		writer.write_image(0, _drawImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        writer.update_set(_device, _drawImageDescriptors);
    }

	//make sure both the descriptor allocator and the new layout get cleaned up properly
	_mainDeletionQueue.push_function([&]() {
		globalDescriptorAllocator.destroy_pool(_device);
		vkDestroyDescriptorSetLayout(_device, _drawImageDescriptorLayout, nullptr);
		vkDestroyDescriptorSetLayout(_device, _singleImageDescriptorLayout, nullptr);
	});

	for (int i = 0; i < FRAME_OVERLAP; i++) {
		// create a descriptor pool
		std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> frame_sizes = {
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 },
		};

		_frames[i]._frameDescriptors = DescriptorAllocatorGrowable{};
		_frames[i]._frameDescriptors.init(_device, 1000, frame_sizes);
		_mainDeletionQueue.push_function([&, i]() {
			_frames[i]._frameDescriptors.destroy_pools(_device);
		});
	}
}

void init_bindless_descriptors() {
	// Create bindless descriptor set layout
	VkDescriptorSetLayoutBinding bindlessBinding{};
	bindlessBinding.binding = 0;
	bindlessBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bindlessBinding.descriptorCount = Texture_Manager::max_bindless();
	bindlessBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = 1;
	layoutInfo.pBindings = &bindlessBinding;

	// Enable update after bind and partial binding
	VkDescriptorBindingFlags bindingFlags =
		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
		VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;

	VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{};
	bindingFlagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
	bindingFlagsInfo.bindingCount = 1;
	bindingFlagsInfo.pBindingFlags = &bindingFlags;

	layoutInfo.pNext = &bindingFlagsInfo;
	layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;

	VkDescriptorSetLayout layout;
	VK_CHECK(vkCreateDescriptorSetLayout(_device, &layoutInfo, nullptr, &layout));
	Texture_Manager::set_bindless_descriptor_layout(layout);

	// Create a descriptor pool for bindless textures
	VkDescriptorPoolSize poolSize{};
	poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	poolSize.descriptorCount = Texture_Manager::max_bindless();

	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
	poolInfo.maxSets = 1;
	poolInfo.poolSizeCount = 1;
	poolInfo.pPoolSizes = &poolSize;

	VkDescriptorPool bindlessPool;
	VK_CHECK(vkCreateDescriptorPool(_device, &poolInfo, nullptr, &bindlessPool));

	// Allocate the bindless descriptor set
	VkDescriptorSetAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = bindlessPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &layout;

	VkDescriptorSet descriptorSet;
	VK_CHECK(vkAllocateDescriptorSets(_device, &allocInfo, &descriptorSet));
	Texture_Manager::set_bindless_descriptor_set(descriptorSet);

	_mainDeletionQueue.push_function([=]() {
		vkDestroyDescriptorSetLayout(_device, layout, nullptr);
		vkDestroyDescriptorPool(_device, bindlessPool, nullptr);
	});
}

bool load_shader_module(const char* filePath, VkDevice device, VkShaderModule* outShaderModule) {
	// open the file. With cursor at the end
	std::ifstream file(filePath, std::ios::ate | std::ios::binary);

	if (!file.is_open()) {
		return false;
	}

	// find what the size of the file is by looking up the location of the cursor
	// because the cursor is at the end, it gives the size directly in bytes
	size_t fileSize = (size_t)file.tellg();

	// spirv expects the buffer to be on uint32, so make sure to reserve a int
	// vector big enough for the entire file
	std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));

	file.seekg(0);
	file.read((char*)buffer.data(), fileSize);
	file.close();

	// create a new shader module, using the buffer we loaded
	VkShaderModuleCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.pNext = nullptr;

	// codeSize has to be in bytes, so multply the ints in the buffer by size of
	// int to know the real size of the buffer
	createInfo.codeSize = buffer.size() * sizeof(uint32_t);
	createInfo.pCode = buffer.data();

	// check that the creation goes well.
	VkShaderModule shaderModule;
	if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
		return false;
	}
	*outShaderModule = shaderModule;
	return true;
}

VkPipeline _gradientPipeline;
VkPipelineLayout _gradientPipelineLayout;

void init_background_pipelines() {
	VkPipelineLayoutCreateInfo computeLayout{};
	computeLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	computeLayout.pNext = nullptr;
	computeLayout.pSetLayouts = &_drawImageDescriptorLayout;
	computeLayout.setLayoutCount = 1;

	VkPushConstantRange pushConstant{};
	pushConstant.offset = 0;
	pushConstant.size = sizeof(ComputePushConstants);
	pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	computeLayout.pPushConstantRanges = &pushConstant;
	computeLayout.pushConstantRangeCount = 1;

	VK_CHECK(vkCreatePipelineLayout(_device, &computeLayout, nullptr, &_gradientPipelineLayout));

	VkShaderModule gradientShader;
	if (!load_shader_module("spirv/gradient_color.comp.spv", _device, &gradientShader)) {
		printf("Error when building the compute shader\n");
		assert(false);
	}

	VkShaderModule skyShader;
	if (!load_shader_module("spirv/sky.comp.spv", _device, &skyShader)) {
		printf("Error when building the compute shader\n");
		assert(false);
	}

	VkPipelineShaderStageCreateInfo stageinfo{};
	stageinfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stageinfo.pNext = nullptr;
	stageinfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	stageinfo.module = gradientShader;
	stageinfo.pName = "main";

	VkComputePipelineCreateInfo computePipelineCreateInfo{};
	computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	computePipelineCreateInfo.pNext = nullptr;
	computePipelineCreateInfo.layout = _gradientPipelineLayout;
	computePipelineCreateInfo.stage = stageinfo;

	ComputeEffect gradient;
	gradient.layout = _gradientPipelineLayout;
	gradient.name = "gradient";
	gradient.data = {};
	gradient.data.data1 = vec4(1, 0, 0, 1);
	gradient.data.data2 = vec4(0, 0, 1, 1);

	VK_CHECK(vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &gradient.pipeline));

	// change to create sky shader
	computePipelineCreateInfo.stage.module = skyShader;
	ComputeEffect sky;
	sky.layout = _gradientPipelineLayout;
	sky.name = "sky";
	sky.data = {};
	sky.data.data1 = vec4(0.1, 0.2, 0.4, 0.97);

	VK_CHECK(vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &sky.pipeline));

	backgroundEffects.push_back(gradient);
	backgroundEffects.push_back(sky);

	vkDestroyShaderModule(_device, gradientShader, nullptr);
	vkDestroyShaderModule(_device, skyShader, nullptr);

	_mainDeletionQueue.push_function([=]() {
		vkDestroyPipelineLayout(_device, _gradientPipelineLayout, nullptr);
		vkDestroyPipeline(_device, sky.pipeline, nullptr);
		vkDestroyPipeline(_device, gradient.pipeline, nullptr);
	});
}

VkPipelineLayout draw_pipeline_layout;
VkPipeline opaque_pipeline;
VkPipeline transparent_pipeline;

GPUMeshBuffers geometry_buffer;

void init_draw_pipeline() {
	VkShaderModule vertex;
	if (!load_shader_module("spirv/tri_mesh.vert.spv", _device, &vertex)) {
		fprintf(stderr, "Error when building the triangle vertex shader module");
		assert(false);
	}

	VkShaderModule fragment;
	if (!load_shader_module("spirv/tri.frag.spv", _device, &fragment)) {
		fprintf(stderr, "Error when building the triangle fragment shader module");
		assert(false);
	}

	VkPushConstantRange bufferRange{};
	bufferRange.offset = 0;
	bufferRange.size = sizeof(GPUDrawPushConstants);
	bufferRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	VkDescriptorSetLayout layouts[] = { Texture_Manager::get_bindless_descriptor_layout() };

	VkPipelineLayoutCreateInfo pipeline_layout_info = pipeline_layout_create_info();
	pipeline_layout_info.pPushConstantRanges = &bufferRange;
	pipeline_layout_info.pushConstantRangeCount = 1;
	pipeline_layout_info.pSetLayouts = layouts;
	pipeline_layout_info.setLayoutCount = 1;
	VK_CHECK(vkCreatePipelineLayout(_device, &pipeline_layout_info, nullptr, &draw_pipeline_layout));

	Pipeline_Builder pipelineBuilder;

	//use the triangle layout we created
	pipelineBuilder._pipelineLayout = draw_pipeline_layout;
	pipelineBuilder.set_shaders(vertex, fragment);
	pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
	//pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_LINE);
	pipelineBuilder.set_cull_mode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE);
	pipelineBuilder.set_multisampling_none();
	pipelineBuilder.disable_blending();
	pipelineBuilder.enable_depthtest(true, VK_COMPARE_OP_GREATER_OR_EQUAL);
	pipelineBuilder.set_color_attachment_format(_drawImage.imageFormat);
	pipelineBuilder.set_depth_format(_depthImage.imageFormat);
	opaque_pipeline = pipelineBuilder.build_pipeline(_device);

	pipelineBuilder.enable_blending_alphablend();
	pipelineBuilder.enable_depthtest(false, VK_COMPARE_OP_GREATER_OR_EQUAL);
	transparent_pipeline = pipelineBuilder.build_pipeline(_device);

	//clean structures
	vkDestroyShaderModule(_device, fragment, nullptr);
	vkDestroyShaderModule(_device, vertex, nullptr);

	_mainDeletionQueue.push_function([&]() {
		vkDestroyPipelineLayout(_device, draw_pipeline_layout, nullptr);
		vkDestroyPipeline(_device, opaque_pipeline, nullptr);
		vkDestroyPipeline(_device, transparent_pipeline, nullptr);
	});
}


void init_pipelines() {
	init_background_pipelines();

	init_draw_pipeline();
}

void cleanup() {
	vkDeviceWaitIdle(_device);

	for (int i = 0; i < FRAME_OVERLAP; i++) {
		vkDestroyCommandPool(_device, _frames[i]._commandPool, nullptr);

		//destroy sync objects
		vkDestroyFence(_device, _frames[i]._renderFence, nullptr);

		// todo do i need? if swapchain image count semaphore
		//vkDestroySemaphore(_device, _frames[i]._renderSemaphore, nullptr);
		vkDestroySemaphore(_device, _frames[i]._swapchainSemaphore, nullptr);
		_frames[i]._deletionQueue.flush();
	}

	Texture_Manager::cleanup();
	_mainDeletionQueue.flush();

	for (int i = 0; i < _swapchainImageCount; i++)
		vkDestroySemaphore(_device, _readyForPresentSemaphores[i], nullptr);

	destroy_swapchain();

	vkDestroySurfaceKHR(_instance, _surface, nullptr);
	vkDestroyDevice(_device, nullptr);

	vkb::destroy_debug_utils_messenger(_instance, _debug_messenger);
	vkDestroyInstance(_instance, nullptr);
}

void draw_background(VkCommandBuffer cmd) {
	//make a clear-color from frame number. This will flash with a 120 frame period.
	//VkClearColorValue clearValue;
	//float flash = std::abs(std::sin(_frameNumber / 120.f));
	//clearValue = { { 0.0f, 0.0f, flash, 1.0f } };

	//VkImageSubresourceRange clearRange = image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT);

	//clear image
	//vkCmdClearColorImage(cmd, _drawImage.image, VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &clearRange);

	ComputeEffect& effect = backgroundEffects[currentBackgroundEffect];

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, effect.pipeline);

	// bind the descriptor set containing the draw image for the compute pipeline
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _gradientPipelineLayout, 0, 1, &_drawImageDescriptors, 0, nullptr);

	vkCmdPushConstants(cmd, _gradientPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants), &effect.data);

	// execute the compute pipeline dispatch. We are using 16x16 workgroup size so we need to divide by it
	vkCmdDispatch(cmd, std::ceil(_drawExtent.width / 16.0), std::ceil(_drawExtent.height / 16.0), 1);
}

void draw_geometry(VkCommandBuffer cmd, const mat4& projection, const mat4& view) {
	//begin a render pass  connected to our draw image
	VkRenderingAttachmentInfo colorAttachment = attachment_info(_drawImage.imageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	VkRenderingAttachmentInfo depthAttachment = depth_attachment_info(_depthImage.imageView, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

	VkRenderingInfo renderInfo = rendering_info(_drawExtent, &colorAttachment, &depthAttachment);
	vkCmdBeginRendering(cmd, &renderInfo);

	//set dynamic viewport and scissor
	VkViewport viewport = {};
	viewport.x = 0;
	viewport.y = 0;
	viewport.width = _drawExtent.width;
	viewport.height = _drawExtent.height;
	viewport.minDepth = 0.f;
	viewport.maxDepth = 1.f;

	vkCmdSetViewport(cmd, 0, 1, &viewport);

	VkRect2D scissor = {};
	scissor.offset.x = 0;
	scissor.offset.y = 0;
	scissor.extent.width = _drawExtent.width;
	scissor.extent.height = _drawExtent.height;

	vkCmdSetScissor(cmd, 0, 1, &scissor);

	///////////////////////////////////////////////////////////////////////////

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, opaque_pipeline);

	VkDescriptorSet imageSet = get_current_frame()._frameDescriptors.allocate(_device, _singleImageDescriptorLayout);
	{
		DescriptorWriter writer;
		//writer.write_image(0, _errorCheckerboardImage.imageView, _defaultSamplerNearest, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

		//writer.update_set(_device, imageSet);
	}

	//vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _opaquePipelineLayout, 0, 1, &imageSet, 0, nullptr);
	VkDescriptorSet _bindlessDescriptorSet = Texture_Manager::get_bindless_descriptor_set();
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, draw_pipeline_layout,
		0, 1, &_bindlessDescriptorSet, 0, nullptr);

	GPUDrawPushConstants push_constants;
	push_constants.vertexBuffer = geometry_buffer.vertexBufferAddress;
	//push_constants.bindless_texture = Texture_Manager::get_by_name("error").bindless_id;

	vkCmdBindIndexBuffer(cmd, geometry_buffer.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

	push_constants.blending = 0;
	for (Model& model : Model_Manager::get_models()) {
		for (Mesh& mesh : model.meshes) {
			if (!mesh.material.blend) {
				push_constants.worldMatrix = projection * view * mesh.transform;
				push_constants.bindless_texture = mesh.material.albedo;
				push_constants.alpha_cutoff = mesh.material.alpha_cutoff;
				vkCmdPushConstants(cmd, draw_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &push_constants);
				vkCmdDrawIndexed(cmd, mesh.lods[lod].index_count, 1, mesh.lods[lod].base_index, mesh.base_vertex, 0);
			}
		}
	}

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, transparent_pipeline);
	push_constants.blending = 1;

	for (Model& model : Model_Manager::get_models()) {
		for (Mesh& mesh : model.meshes) {
			if (mesh.material.blend) {
				push_constants.worldMatrix = projection * view * mesh.transform;
				push_constants.bindless_texture = mesh.material.albedo;
				push_constants.alpha_cutoff = mesh.material.alpha_cutoff;
				vkCmdPushConstants(cmd, draw_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &push_constants);
				vkCmdDrawIndexed(cmd, mesh.lods[lod].index_count, 1, mesh.lods[lod].base_index, mesh.base_vertex, 0);
			}
		}
	}

	
	vkCmdEndRendering(cmd);
}

void init_imgui(GLFWwindow* window) {
	// 1: create descriptor pool for IMGUI
	//  the size of the pool is very oversize, but it's copied from imgui demo
	//  itself.
	VkDescriptorPoolSize pool_sizes[] = { 
		{ VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 } 
	};

	VkDescriptorPoolCreateInfo pool_info = {};
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	pool_info.maxSets = 1000;
	pool_info.poolSizeCount = (uint32_t)std::size(pool_sizes);
	pool_info.pPoolSizes = pool_sizes;

	VkDescriptorPool imguiPool;
	VK_CHECK(vkCreateDescriptorPool(_device, &pool_info, nullptr, &imguiPool));

	_mainDeletionQueue.push_function([=]() {
		//ImGui_ImplVulkan_Shutdown();
		vkDestroyDescriptorPool(_device, imguiPool, nullptr);
	});


	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;

	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;       // Enable Multi-Viewport / Platform Windows

	// When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
	ImGuiStyle& style = ImGui::GetStyle();
	//if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
	//	style.WindowRounding = 0.0f;
	//	style.Colors[ImGuiCol_WindowBg].w = 1.0f;
	//}

	ImGui::StyleColorsDark();

	style.ScaleAllSizes(main_scale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
	style.FontScaleDpi = main_scale;        // Set initial font scale. (using io.ConfigDpiScaleFonts=true makes this unnecessary. We leave both here for documentation purpose)

	// Setup Platform/Renderer backends
	ImGui_ImplGlfw_InitForVulkan(window, true);
	ImGui_ImplVulkan_InitInfo init_info = {};
	init_info.ApiVersion = VK_API_VERSION_1_3;              // Pass in your value of VkApplicationInfo::apiVersion, otherwise will default to header version.
	init_info.Instance = _instance;
	init_info.PhysicalDevice = _chosenGPU;
	init_info.Device = _device;
	init_info.QueueFamily = renderer._graphicsQueueFamily;
	init_info.Queue = renderer._graphicsQueue;
	//static VkPipelineCache          g_PipelineCache = VK_NULL_HANDLE;
	init_info.PipelineCache = VK_NULL_HANDLE;

	init_info.DescriptorPool = imguiPool;
	init_info.MinImageCount = 3;
	init_info.ImageCount = 3;

	//init_info.Allocator = g_Allocator;
	//init_info.PipelineInfoMain.RenderPass = wd->RenderPass;
	//init_info.PipelineInfoMain.Subpass = 0;
	init_info.UseDynamicRendering = true;
	init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
	init_info.PipelineInfoMain.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	init_info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
	init_info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &_swapchainImageFormat;

	init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
	init_info.CheckVkResultFn = check_vk_result;
	ImGui_ImplVulkan_Init(&init_info);
}

GPUMeshBuffers uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices) {
	const size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
	const size_t indexBufferSize = indices.size() * sizeof(uint32_t);

	GPUMeshBuffers newSurface;

	//create vertex buffer
	newSurface.vertexBuffer = renderer.create_buffer(vertexBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

	//find the adress of the vertex buffer
	VkBufferDeviceAddressInfo deviceAdressInfo{ .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,.buffer = newSurface.vertexBuffer.buffer };
	newSurface.vertexBufferAddress = vkGetBufferDeviceAddress(_device, &deviceAdressInfo);

	//create index buffer
	newSurface.indexBuffer = renderer.create_buffer(indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

	AllocatedBuffer staging = renderer.create_buffer(vertexBufferSize + indexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);

	//void* data = staging.allocation->GetMappedData();
	void* data;
	vmaMapMemory(_allocator, staging.allocation, &data);

	// copy vertex buffer
	memcpy(data, vertices.data(), vertexBufferSize);
	// copy index buffer
	memcpy((char*)data + vertexBufferSize, indices.data(), indexBufferSize);

	vmaUnmapMemory(_allocator, staging.allocation);

	renderer.immediate_submit([&](VkCommandBuffer cmd) {
		VkBufferCopy vertexCopy{ 0 };
		vertexCopy.dstOffset = 0;
		vertexCopy.srcOffset = 0;
		vertexCopy.size = vertexBufferSize;

		vkCmdCopyBuffer(cmd, staging.buffer, newSurface.vertexBuffer.buffer, 1, &vertexCopy);

		VkBufferCopy indexCopy{ 0 };
		indexCopy.dstOffset = 0;
		indexCopy.srcOffset = vertexBufferSize;
		indexCopy.size = indexBufferSize;

		vkCmdCopyBuffer(cmd, staging.buffer, newSurface.indexBuffer.buffer, 1, &indexCopy);
	});

	renderer.destroy_buffer(staging);

	return newSurface;
}

void init_models() {
	//Model_Manager::load_model("submarine/scene.gltf", Mesh_Opt_Flags_All);
	//Model_Manager::load_model("bistro/Scene.gltf", Mesh_Opt_Flags_All);
	//Model_Manager::load_model("CompareAlphaTest/AlphaBlendModeTest.gltf", Mesh_Opt_Flags_All);
	Model_Manager::load_model("house/scene.gltf");
	
	geometry_buffer = uploadMesh(Model_Manager::get_indices(), Model_Manager::get_vertices());

	_mainDeletionQueue.push_function([&]() {
		renderer.destroy_buffer(geometry_buffer.indexBuffer);
		renderer.destroy_buffer(geometry_buffer.vertexBuffer);
	});
}

void resize_swapchain(GLFWwindow* window) {
	printf("RESIZING\n");

	vkDeviceWaitIdle(_device);

	destroy_swapchain();

	int w = 0, h = 0;
	glfwGetFramebufferSize(window, &w, &h);

	width = w;
	height = h;
	create_swapchain(width, height);

	resize_requested = false;
}

int main() {
    printf("hello vk\n");
    

	// window
    if (!glfwInit()) {
        printf("glfw init failed\n");
        return 1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	main_scale = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor()); // Valid on GLFW 3.3+ only
    GLFWwindow* window = glfwCreateWindow(width * main_scale, height * main_scale, "fireball", nullptr, nullptr);
	
	glfwSetMouseButtonCallback(window, mouseCallback);
	glfwSetKeyCallback(window, keyCallback);
	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	//

	if (init_vulkan(window))
		return 1;

	renderer.init(_device, _allocator);

	init_swapchain(width, height);
	init_commands();
	init_sync_structures();
	init_descriptors();
	init_bindless_descriptors();
	init_pipelines();

	Texture_Manager::init(&renderer);

	Model_Manager::init("../resources/models/");
	init_models();
	
	init_imgui(window);

	Camera camera;

	double dt;
	double last_frame = 0.0;

    while (!glfwWindowShouldClose(window)) {
		double current_time = glfwGetTime();
		dt = current_time - last_frame;
		last_frame = current_time;

		glfwPollEvents();

		int32_t newWidth, newHeight;
		glfwGetWindowSize(window, &newWidth, &newHeight);
		if (newWidth == 0 || newHeight == 0) {
			continue;
		}
		else if (newWidth != width || newHeight != height) {
			resize_requested = true;
		}

		//if (glfwGetWindowAttrib(window, GLFW_ICONIFIED)) { // minimized
		//	int w = 0, h = 0;
		//	while (w == 0 || h == 0) {
		//		glfwGetFramebufferSize(window, &w, &h);
		//		glfwWaitEvents();
		//	}
		//}

		if (resize_requested) {
			resize_swapchain(window);
		}

		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		if (ImGui::Begin("background")) {
			ComputeEffect& selected = backgroundEffects[currentBackgroundEffect];
			ImGui::SliderFloat("Render Scale", &renderScale, 0.3f, 1.f);
			ImGui::Text("Selected effect: ", selected.name);
			ImGui::SliderInt("Effect Index", &currentBackgroundEffect, 0, backgroundEffects.size() - 1);
			ImGui::InputFloat4("data1", (float*)&selected.data.data1);
			ImGui::InputFloat4("data2", (float*)&selected.data.data2);
			ImGui::InputFloat4("data3", (float*)&selected.data.data3);
			ImGui::InputFloat4("data4", (float*)&selected.data.data4);
		}
		ImGui::End();

		if (ImGui::Begin("Camera Controls")) {
			// Position sliders
			ImGui::SliderFloat("X", &camera.position.x, -5000.0f, 5000.0f);
			ImGui::SliderFloat("Y", &camera.position.y, -5000.0f, 5000.0f);
			ImGui::SliderFloat("Z", &camera.position.z, -5000.0f, 5000.0f);

			ImGui::SliderFloat("Pitch", &camera.pitch, -89.0f, 89.0f);
			ImGui::SliderFloat("Yaw", &camera.yaw, -180.0f, 180.0f);
			ImGui::SliderFloat("Zoom", &camera.zoom, -180.0f, 180.0f);

			ImGui::SliderInt("LOD", &lod, 0, NUM_LODS - 1);

			ImGui::End();
		}

		ImGui::Render();

		if (!ImGui::GetIO().WantCaptureMouse) {
			double xpos, ypos;
			glfwGetCursorPos(window, &xpos, &ypos);
			camera.update(xpos, ypos);
			camera.move(window, dt);
		}

		// wait until the gpu has finished rendering the last frame. Timeout of 1
		// second
		VK_CHECK(vkWaitForFences(_device, 1, &get_current_frame()._renderFence, true, 1000000000));
		
		get_current_frame()._deletionQueue.flush();
		get_current_frame()._frameDescriptors.clear_pools(_device);

		VK_CHECK(vkResetFences(_device, 1, &get_current_frame()._renderFence));

		//request image from the swapchain
		uint32_t swapchainImageIndex;
		
		//VK_CHECK(vkAcquireNextImageKHR(_device, _swapchain, 1000000000, get_current_frame()._swapchainSemaphore, nullptr, &swapchainImageIndex));
		VkResult e = vkAcquireNextImageKHR(_device, _swapchain, 1000000000, get_current_frame()._swapchainSemaphore, nullptr, &swapchainImageIndex);
		if (e == VK_ERROR_OUT_OF_DATE_KHR) {
			resize_requested = true;
			continue;
		}
		
		VkCommandBuffer cmd = get_current_frame()._mainCommandBuffer;
		// now that we are sure that the commands finished executing, we can safely
		// reset the command buffer to begin recording again.
		VK_CHECK(vkResetCommandBuffer(cmd, 0));

		//begin the command buffer recording. We will use this command buffer exactly once, so we want to let vulkan know that
		VkCommandBufferBeginInfo cmdBeginInfo = command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

		_drawExtent.height = std::min(_swapchainExtent.height, _drawImage.imageExtent.height) * renderScale;
		_drawExtent.width = std::min(_swapchainExtent.width, _drawImage.imageExtent.width) * renderScale;

		VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

		//make the swapchain image into writeable mode before rendering
		transition_image(cmd, _drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

		draw_background(cmd);

		transition_image(cmd, _drawImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		transition_image(cmd, _depthImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

		mat4 view = camera.get_view();
		mat4 projection = camera.get_projection((float)width/(float)height);
		draw_geometry(cmd, projection, view);

		transition_image(cmd, _drawImage.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
		transition_image(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
		
		// execute a copy from the draw image into the swapchain
		copy_image_to_image(cmd, _drawImage.image, _swapchainImages[swapchainImageIndex], _drawExtent, _swapchainExtent);

		VkRenderingAttachmentInfo colorAttachment = {};
		colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		colorAttachment.imageView = _swapchainImageViews[swapchainImageIndex];
		colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // Keep existing content
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

		VkRenderingInfo renderInfo = {};
		renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
		renderInfo.renderArea.offset = { 0, 0 };
		renderInfo.renderArea.extent = _swapchainExtent;
		renderInfo.layerCount = 1;
		renderInfo.colorAttachmentCount = 1;
		renderInfo.pColorAttachments = &colorAttachment;

		vkCmdBeginRendering(cmd, &renderInfo);
		ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
		vkCmdEndRendering(cmd);

		// set swapchain image layout to Present so we can show it on the screen
		transition_image(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

		//finalize the command buffer (we can no longer add commands, but it can now be executed)
		VK_CHECK(vkEndCommandBuffer(cmd));

		//prepare the submission to the queue. 
		//we want to wait on the _presentSemaphore, as that semaphore is signaled when the swapchain is ready
		//we will signal the _renderSemaphore, to signal that rendering has finished
		VkCommandBufferSubmitInfo cmdinfo = command_buffer_submit_info(cmd);

		VkSemaphoreSubmitInfo waitInfo = semaphore_submit_info(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, get_current_frame()._swapchainSemaphore);
		//VkSemaphoreSubmitInfo signalInfo = semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, get_current_frame()._renderSemaphore);
		VkSemaphoreSubmitInfo signalInfo = semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, _readyForPresentSemaphores[swapchainImageIndex]);

		VkSubmitInfo2 submit = submit_info(&cmdinfo, &signalInfo, &waitInfo);

		//submit command buffer to the queue and execute it.
		// _renderFence will now block until the graphic commands finish execution
		VK_CHECK(vkQueueSubmit2(renderer._graphicsQueue, 1, &submit, get_current_frame()._renderFence));

		//prepare present
		// this will put the image we just rendered to into the visible window.
		// we want to wait on the _renderSemaphore for that, 
		// as its necessary that drawing commands have finished before the image is displayed to the user
		VkPresentInfoKHR presentInfo = {};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentInfo.pNext = nullptr;
		presentInfo.pSwapchains = &_swapchain;
		presentInfo.swapchainCount = 1;

		//presentInfo.pWaitSemaphores = &get_current_frame()._renderSemaphore;
		presentInfo.pWaitSemaphores = &_readyForPresentSemaphores[swapchainImageIndex];
		presentInfo.waitSemaphoreCount = 1;

		presentInfo.pImageIndices = &swapchainImageIndex;

		//VK_CHECK(vkQueuePresentKHR(_graphicsQueue, &presentInfo));
		VkResult presentResult = vkQueuePresentKHR(renderer._graphicsQueue, &presentInfo);
		if (presentResult == VK_ERROR_OUT_OF_DATE_KHR) {
			resize_requested = true;
		}

		ImGuiIO& io = ImGui::GetIO();
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
		}
		
		_frameNumber++;
    }

	VkResult err = vkDeviceWaitIdle(_device);
	check_vk_result(err);

	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	cleanup();

	glfwDestroyWindow(window);
	glfwTerminate();

    return 0;
}
