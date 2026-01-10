#include "vk_backend.h"

#include "asset/texture_manager.h"
#include "renderer/pipeline.h"
#include "renderer/vk_types.h"
#include "renderer/vk_util.h"

#include <VkBootstrap.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

int Vk_Backend::init(GLFWwindow* window, uint32_t w, uint32_t h, bool validation_layers) {
	if (init_vulkan(window, validation_layers))
		return 1;

	width = w;
	height = h;

	init_swapchain(width, height);
	init_commands();
	init_sync_structures();
	init_descriptors();
	init_bindless_descriptors();
	init_pipelines();
	init_draw_buffers();
	init_light_buffer();
	//init_mesh_cull_descriptors();
	init_imgui(window);

	return 0;
}

int Vk_Backend::init_vulkan(GLFWwindow* window, bool validation_layers) {
	vkb::InstanceBuilder builder;

	uint32_t ext_count = 0;
	const char** glfw_exts = glfwGetRequiredInstanceExtensions(&ext_count);

	for (uint32_t i = 0; i < ext_count; i++) {
		builder.enable_extension(glfw_exts[i]);
	}

	builder.enable_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

	//make the vulkan instance, with basic debug features
	auto inst_ret = builder.set_app_name("fireball")
		.request_validation_layers(validation_layers)
		.use_default_debug_messenger()
		.require_api_version(1, 3, 0)
	.build();

	vkb::Instance vkb_inst = inst_ret.value();

	//grab the instance 
	_instance = vkb_inst.instance;
	_debug_messenger = vkb_inst.debug_messenger;

	VkResult err = glfwCreateWindowSurface(_instance, window, NULL, &_surface);
	if (err) {
		// Window surface creation failed
		printf("window surface creation failed\n");
		return 1;
	}

	// 1.4

	VkPhysicalDeviceVulkan13Features features13{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
	features13.dynamicRendering = true;
	features13.synchronization2 = true;
	features13.shaderDemoteToHelperInvocation = true;

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

	features12.drawIndirectCount = true;

	VkPhysicalDeviceFeatures features = {};
	features.multiDrawIndirect = true;
	features.fragmentStoresAndAtomics = true;

	//use vkbootstrap to select a gpu. 
	//We want a gpu that can write to the SDL surface and supports vulkan 1.3 with the correct features
	vkb::PhysicalDeviceSelector selector{ vkb_inst };
	vkb::PhysicalDevice physicalDevice = selector
		.set_minimum_version(1, 3)
		.set_required_features(features)
		.set_required_features_13(features13)
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

	_graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
	_graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

	// initialize the memory allocator
	VmaAllocatorCreateInfo allocatorInfo = {};
	allocatorInfo.physicalDevice = _chosenGPU;
	allocatorInfo.device = _device;
	allocatorInfo.instance = _instance;
	allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
	vmaCreateAllocator(&allocatorInfo, &_allocator);

	return 0;
}

void Vk_Backend::create_swapchain(uint32_t width, uint32_t height) {
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

void Vk_Backend::init_swapchain(uint32_t width, uint32_t height) {
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
}

void Vk_Backend::destroy_swapchain() {
	vkDestroySwapchainKHR(_device, _swapchain, nullptr);

	for (int i = 0; i < _swapchainImageViews.size(); i++)
		vkDestroyImageView(_device, _swapchainImageViews[i], nullptr);
}

void Vk_Backend::resize_swapchain(GLFWwindow* window) {
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

void Vk_Backend::init_sync_structures() {
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

	VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_immFence));
}

void Vk_Backend::init_commands() {
		//create a command pool for commands submitted to the graphics queue.
	//we also want the pool to allow for resetting of individual command buffers
	VkCommandPoolCreateInfo commandPoolInfo = {};
	commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	commandPoolInfo.pNext = nullptr;
	commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	commandPoolInfo.queueFamilyIndex = _graphicsQueueFamily;

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

	VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_immCommandPool));

	// allocate the command buffer for immediate submits
	VkCommandBufferAllocateInfo cmdAllocInfo = command_buffer_allocate_info(_immCommandPool, 1);

	VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_immCommandBuffer));
}

void Vk_Backend::init_descriptors() {
	std::vector<DescriptorAllocator::PoolSizeRatio> sizes = {
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 7 },
	};

	globalDescriptorAllocator.init_pool(_device, 10, sizes);

	//make the descriptor set layout for our compute draw
	{
		DescriptorLayoutBuilder builder;
		builder.add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		_drawImageDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_COMPUTE_BIT);
	}

	//allocate a descriptor set for our draw image
	_drawImageDescriptors = globalDescriptorAllocator.allocate(_device, _drawImageDescriptorLayout);
	{
		DescriptorWriter writer;
		writer.write_image(0, _drawImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		writer.update_set(_device, _drawImageDescriptors);
	}

	for (int i = 0; i < FRAME_OVERLAP; i++) {
		// create a descriptor pool
		std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> frame_sizes = {
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 },
		};

		_frames[i]._frameDescriptors = DescriptorAllocatorGrowable{};
		_frames[i]._frameDescriptors.init(_device, 1000, frame_sizes);
	}
}

void Vk_Backend::init_bindless_descriptors() {
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

void Vk_Backend::init_draw_buffers() {
	opaque_command_buffer = create_buffer(MAX_DRAW_COMMANDS * sizeof(VkDrawIndexedIndirectCommand), VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

	transparent_command_buffer = create_buffer(MAX_DRAW_COMMANDS * sizeof(VkDrawIndexedIndirectCommand), VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

	command_count_buffer = create_buffer(sizeof(Command_Counts), VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

	
	transform_buffer = create_buffer(MAX_DRAW_COMMANDS * sizeof(mat4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
	VkBufferDeviceAddressInfo deviceAdressInfo = { .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = transform_buffer.buffer };
	gpu_push_constants.transform_buffer = vkGetBufferDeviceAddress(_device, &deviceAdressInfo);

	material_buffer = create_buffer(MAX_DRAW_COMMANDS * sizeof(GPU_Material), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
	deviceAdressInfo = { .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,.buffer = material_buffer.buffer };
	gpu_push_constants.material_buffer = vkGetBufferDeviceAddress(_device, &deviceAdressInfo);

	mesh_render_info_buffer = create_buffer(MAX_DRAW_COMMANDS * sizeof(GPU_Mesh_Render_Info), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

	mesh_buffer = create_buffer(MAX_DRAW_COMMANDS * sizeof(GPU_Mesh), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY);


	_mainDeletionQueue.push_function([&]() {
		destroy_buffer(opaque_command_buffer);
		destroy_buffer(transparent_command_buffer);
		destroy_buffer(command_count_buffer);

		destroy_buffer(transform_buffer);
		destroy_buffer(material_buffer);
		destroy_buffer(mesh_render_info_buffer);
		destroy_buffer(mesh_buffer);
	});
}

void Vk_Backend::init_light_buffer() {
	light_buffer = create_buffer(MAX_LIGHTS * sizeof(GPU_Light), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
	VkBufferDeviceAddressInfo deviceAdressInfo = { .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = light_buffer.buffer };
	gpu_push_constants.light_buffer = vkGetBufferDeviceAddress(_device, &deviceAdressInfo);

	_mainDeletionQueue.push_function([&]() {
		destroy_buffer(light_buffer);
	});
}

void Vk_Backend::init_background_pipelines() {
	//VkPipelineLayoutCreateInfo computeLayout{};
	//computeLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	//computeLayout.pNext = nullptr;
	//computeLayout.pSetLayouts = &_drawImageDescriptorLayout;
	//computeLayout.setLayoutCount = 1;

	//VK_CHECK(vkCreatePipelineLayout(_device, &computeLayout, nullptr, &_gradientPipelineLayout));

	//VkShaderModule skyShader;
	//if (!load_shader_module("spirv/sky.comp.spv", _device, &skyShader)) {
	//	printf("Error when building the compute shader\n");
	//	assert(false);
	//}

	//VkPipelineShaderStageCreateInfo stageinfo{};
	//stageinfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	//stageinfo.pNext = nullptr;
	//stageinfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	//stageinfo.module = skyShader;
	//stageinfo.pName = "main";

	//VkComputePipelineCreateInfo computePipelineCreateInfo{};
	//computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	//computePipelineCreateInfo.pNext = nullptr;
	//computePipelineCreateInfo.layout = _gradientPipelineLayout;
	//computePipelineCreateInfo.stage = stageinfo;

	//// change to create sky shader
	//computePipelineCreateInfo.stage.module = skyShader;
	//ComputeEffect sky;
	//sky.layout = _gradientPipelineLayout;
	//sky.name = "sky";
	//sky.data = {};
	//sky.data.data1 = vec4(0.1, 0.2, 0.4, 0.97);

	//VK_CHECK(vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &sky.pipeline));

	//backgroundEffects.push_back(sky);

	//vkDestroyShaderModule(_device, skyShader, nullptr);

	//_mainDeletionQueue.push_function([=]() {
	//	vkDestroyPipelineLayout(_device, _gradientPipelineLayout, nullptr);
	//	vkDestroyPipeline(_device, sky.pipeline, nullptr);
	//});
}

void Vk_Backend::init_draw_pipeline() {
	VkShaderModule vertex;
	if (!load_shader_module("spirv/main.vert.spv", _device, &vertex)) {
		fprintf(stderr, "Error when building the triangle vertex shader module");
		assert(false);
	}

	VkShaderModule fragment;
	if (!load_shader_module("spirv/main.frag.spv", _device, &fragment)) {
		fprintf(stderr, "Error when building the triangle fragment shader module");
		assert(false);
	}

	VkPushConstantRange pushRanges = {};
	pushRanges.offset = 0;
	pushRanges.size = sizeof(GPU_Push_Constants);
	pushRanges.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayout layouts[] = { Texture_Manager::get_bindless_descriptor_layout() };

	VkPipelineLayoutCreateInfo pipeline_layout_info = pipeline_layout_create_info();
	pipeline_layout_info.pPushConstantRanges = &pushRanges;
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

void Vk_Backend::init_mesh_cull_pipeline() {
	std::vector<VkDescriptorSetLayoutBinding> bindings;

	bindings.push_back({
		.binding = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
	});

	bindings.push_back({
		.binding = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
	});

	bindings.push_back({
		.binding = 2,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
	});

	bindings.push_back({
		.binding = 3,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
	});

	bindings.push_back({
		.binding = 4,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
	});

	bindings.push_back({
		.binding = 5,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
	});

	bindings.push_back({
		.binding = 6,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
	});

	VkDescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = bindings.size();
	layoutInfo.pBindings = bindings.data();

	VK_CHECK(vkCreateDescriptorSetLayout(_device, &layoutInfo, nullptr, &mesh_cull_descriptor_layout));

	VkPushConstantRange pushConstantRange{};
	pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	pushConstantRange.offset = 0;
	pushConstantRange.size = sizeof(Cull_Push_Constants);

	VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 1;
	pipelineLayoutInfo.pSetLayouts = &mesh_cull_descriptor_layout;
	pipelineLayoutInfo.pushConstantRangeCount = 1;
	pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

	VK_CHECK(vkCreatePipelineLayout(_device, &pipelineLayoutInfo, nullptr, &mesh_cull_pipeline_layout));

	VkShaderModule cullShader;
	if (!load_shader_module("spirv/mesh_cull.comp.spv", _device, &cullShader)) {
		printf("Error when building the mesh cull compute shader\n");
		assert(false);
	}

	VkPipelineShaderStageCreateInfo stageInfo{};
	stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	stageInfo.module = cullShader;
	stageInfo.pName = "main";

	VkComputePipelineCreateInfo pipelineInfo{};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	pipelineInfo.layout = mesh_cull_pipeline_layout;
	pipelineInfo.stage = stageInfo;

	VK_CHECK(vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mesh_cull_pipeline));

	vkDestroyShaderModule(_device, cullShader, nullptr);

	_mainDeletionQueue.push_function([=]() {
		vkDestroyPipeline(_device, mesh_cull_pipeline, nullptr);
		vkDestroyPipelineLayout(_device, mesh_cull_pipeline_layout, nullptr);
		vkDestroyDescriptorSetLayout(_device, mesh_cull_descriptor_layout, nullptr);
	});
}

void Vk_Backend::init_mesh_cull_descriptors() {
	// Allocate descriptor set
	VkDescriptorSetAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = globalDescriptorAllocator.pool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &mesh_cull_descriptor_layout;

	VK_CHECK(vkAllocateDescriptorSets(_device, &allocInfo, &mesh_cull_descriptor_set));

	std::vector<VkWriteDescriptorSet> writes;

	VkDescriptorBufferInfo opaqueCommandsInfo{};
	opaqueCommandsInfo.buffer = opaque_command_buffer.buffer;
	opaqueCommandsInfo.offset = 0;
	opaqueCommandsInfo.range = VK_WHOLE_SIZE;
	writes.push_back({
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = mesh_cull_descriptor_set,
		.dstBinding = 0,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.pBufferInfo = &opaqueCommandsInfo
	});

	VkDescriptorBufferInfo transparentCommandsInfo{};
	transparentCommandsInfo.buffer = transparent_command_buffer.buffer;
	transparentCommandsInfo.offset = 0;
	transparentCommandsInfo.range = VK_WHOLE_SIZE;
	writes.push_back({
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = mesh_cull_descriptor_set,
		.dstBinding = 1,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.pBufferInfo = &transparentCommandsInfo
	});

	// count
	VkDescriptorBufferInfo commandCountInfo{};
	commandCountInfo.buffer = command_count_buffer.buffer;
	commandCountInfo.offset = 0;
	commandCountInfo.range = VK_WHOLE_SIZE;
	writes.push_back({
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = mesh_cull_descriptor_set,
		.dstBinding = 2,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.pBufferInfo = &commandCountInfo
	});

	// meshes
	VkDescriptorBufferInfo meshBufferInfo{};
	meshBufferInfo.buffer = mesh_buffer.buffer;
	meshBufferInfo.offset = 0;
	meshBufferInfo.range = VK_WHOLE_SIZE;
	writes.push_back({
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = mesh_cull_descriptor_set,
		.dstBinding = 3,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.pBufferInfo = &meshBufferInfo
	});

	// render info
	VkDescriptorBufferInfo meshRenderInfo{};
	meshRenderInfo.buffer = mesh_render_info_buffer.buffer;
	meshRenderInfo.offset = 0;
	meshRenderInfo.range = VK_WHOLE_SIZE;
	writes.push_back({
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = mesh_cull_descriptor_set,
		.dstBinding = 4,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.pBufferInfo = &meshRenderInfo
	});

	// transforms
	VkDescriptorBufferInfo transformBufferInfo{};
	transformBufferInfo.buffer = transform_buffer.buffer;
	transformBufferInfo.offset = 0;
	transformBufferInfo.range = VK_WHOLE_SIZE;
	writes.push_back({
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = mesh_cull_descriptor_set,
		.dstBinding = 5,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.pBufferInfo = &transformBufferInfo
	});

	// materials
	VkDescriptorBufferInfo materialBufferInfo{};
	materialBufferInfo.buffer = material_buffer.buffer;
	materialBufferInfo.offset = 0;
	materialBufferInfo.range = VK_WHOLE_SIZE;
	writes.push_back({
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = mesh_cull_descriptor_set,
		.dstBinding = 6,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.pBufferInfo = &materialBufferInfo
	});

	vkUpdateDescriptorSets(_device, writes.size(), writes.data(), 0, nullptr);
}

void Vk_Backend::init_pipelines() {
	init_background_pipelines();
	init_draw_pipeline();
	init_mesh_cull_pipeline();
}

void Vk_Backend::init_imgui(GLFWwindow* window) {
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

	style.ScaleAllSizes(ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor()));        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
	style.FontScaleDpi = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor());        // Set initial font scale. (using io.ConfigDpiScaleFonts=true makes this unnecessary. We leave both here for documentation purpose)

	// Setup Platform/Renderer backends
	ImGui_ImplGlfw_InitForVulkan(window, true);
	ImGui_ImplVulkan_InitInfo init_info = {};
	init_info.ApiVersion = VK_API_VERSION_1_3;              // Pass in your value of VkApplicationInfo::apiVersion, otherwise will default to header version.
	init_info.Instance = _instance;
	init_info.PhysicalDevice = _chosenGPU;
	init_info.Device = _device;
	init_info.QueueFamily = _graphicsQueueFamily;
	init_info.Queue = _graphicsQueue;
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

void Vk_Backend::upload_geometry(std::span<uint32_t> indices, std::span<Vertex> vertices) {
	size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
	size_t indexBufferSize = indices.size() * sizeof(uint32_t);

	//create vertex buffer
	vertex_buffer = create_buffer(vertexBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
	VkBufferDeviceAddressInfo deviceAdressInfo { .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = vertex_buffer.buffer };
	gpu_push_constants.vertex_buffer = vkGetBufferDeviceAddress(_device, &deviceAdressInfo);

	//create index buffer
	index_buffer = create_buffer(indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

	Allocated_Buffer staging = create_buffer(vertexBufferSize + indexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);

	void* data;
	vmaMapMemory(_allocator, staging.allocation, &data);

	memcpy(data, vertices.data(), vertexBufferSize);
	memcpy((char*)data + vertexBufferSize, indices.data(), indexBufferSize);

	vmaUnmapMemory(_allocator, staging.allocation);

	immediate_submit([&](VkCommandBuffer cmd) {
		VkBufferCopy vertexCopy = {};
		vertexCopy.dstOffset = 0;
		vertexCopy.srcOffset = 0;
		vertexCopy.size = vertexBufferSize;

		vkCmdCopyBuffer(cmd, staging.buffer, vertex_buffer.buffer, 1, &vertexCopy);
		VkBufferCopy indexCopy = {};
		indexCopy.dstOffset = 0;
		indexCopy.srcOffset = vertexBufferSize;
		indexCopy.size = indexBufferSize;
		vkCmdCopyBuffer(cmd, staging.buffer, index_buffer.buffer, 1, &indexCopy);
	});

	destroy_buffer(staging);

	_mainDeletionQueue.push_function([&]() {
		destroy_buffer(index_buffer);
		destroy_buffer(vertex_buffer);
	});
}

void Vk_Backend::allocate_model(Entity e, Model_Handle handle) {
	// check if mesh_allocations[e.id()] exists
	// if so, deallocate_model, maybe warning
	// proceed

	Model& model = Model_Manager::get_model(handle);
	uint32_t mesh_count = model.meshes.size();

	auto alloc = mesh_allocator.allocate(mesh_count);
	if (!alloc.valid()) {
		assert(false && "Out of mesh slots!");
		return;
	}

	mesh_allocations[e.id()] = alloc;

	printf("[ALLOC] Allocating model '%s' for entity %llu: %u meshes\n", Model_Manager::get_model_name(handle).c_str(), e.id(), mesh_count);

	printf("[ALLOC] Alloc range: base=%u, count=%u\n", alloc.base, alloc.count);

	std::vector<mat4> transforms;
	std::vector<GPU_Material> materials;
	std::vector<GPU_Mesh_Render_Info> render_infos;
	std::vector<GPU_Mesh> meshes;

	transforms.reserve(mesh_count);
	materials.reserve(mesh_count);
	render_infos.reserve(mesh_count);
	meshes.reserve(mesh_count);

	for (uint32_t i = 0; i < mesh_count; i++) {
		Mesh& mesh = model.meshes[i];
		uint32_t gpu_index = alloc.base + i;

		transforms.push_back(e.get<Transform_Component>().world_transform * mesh.transform);

		GPU_Material material;
		material.albedo = mesh.material.albedo;
		material.normal = mesh.material.normal;
		material.alpha_cutoff = mesh.material.alpha_cutoff;
		material.blending = mesh.material.blend ? 1 : 0; // rm me
		materials.push_back(material);

		GPU_Mesh_Render_Info render_info;
		render_info.transform_index = gpu_index;
		render_info.material_index = gpu_index;
		render_infos.push_back(render_info);

		GPU_Mesh gpu_mesh;
		gpu_mesh.base_vertex = (int32_t)mesh.base_vertex;
		gpu_mesh.vertex_count = mesh.vertex_count;
		gpu_mesh.mesh_render_info_index = gpu_index;
		gpu_mesh.flags = 0;
		gpu_mesh.bounding_sphere = vec4(0.0f);

		for (int lod = 0; lod < NUM_LODS; lod++)
			gpu_mesh.lods[lod] = mesh.lods[lod];

		meshes.push_back(gpu_mesh);

		printf("[ALLOC] Mesh %u -> GPU index %u\n", i, gpu_index);
	}

	assert(transforms.size() == materials.size() &&
		materials.size() == render_infos.size() &&
		render_infos.size() == meshes.size());

	uint32_t count = transforms.size();

	size_t transform_size = count * sizeof(mat4);
	size_t material_size = count * sizeof(GPU_Material);
	size_t render_info_size = count * sizeof(GPU_Mesh_Render_Info);
	size_t mesh_size = count * sizeof(GPU_Mesh);
	size_t total_size = transform_size + material_size + render_info_size + mesh_size;

	Allocated_Buffer staging = create_buffer(
		total_size,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VMA_MEMORY_USAGE_CPU_ONLY
	);

	// map and copy all data
	void* data;
	vmaMapMemory(_allocator, staging.allocation, &data);

	size_t offset = 0;
	memcpy((char*)data + offset, transforms.data(), transform_size);
	offset += transform_size;

	memcpy((char*)data + offset, materials.data(), material_size);
	offset += material_size;

	memcpy((char*)data + offset, render_infos.data(), render_info_size);
	offset += render_info_size;

	memcpy((char*)data + offset, meshes.data(), mesh_size);

	vmaUnmapMemory(_allocator, staging.allocation);

	uint32_t base_index = alloc.base;

	immediate_submit([&](VkCommandBuffer cmd) {
		size_t src_offset = 0;

		VkBufferCopy transform_copy = {
			.srcOffset = src_offset,
			.dstOffset = base_index * sizeof(mat4),
			.size = transform_size
		};
		vkCmdCopyBuffer(cmd, staging.buffer, transform_buffer.buffer, 1, &transform_copy);
		src_offset += transform_size;

		VkBufferCopy material_copy = {
			.srcOffset = src_offset,
			.dstOffset = base_index * sizeof(GPU_Material),
			.size = material_size
		};
		vkCmdCopyBuffer(cmd, staging.buffer, material_buffer.buffer, 1, &material_copy);
		src_offset += material_size;

		VkBufferCopy render_info_copy = {
			.srcOffset = src_offset,
			.dstOffset = base_index * sizeof(GPU_Mesh_Render_Info),
			.size = render_info_size
		};
		vkCmdCopyBuffer(cmd, staging.buffer, mesh_render_info_buffer.buffer, 1, &render_info_copy);
		src_offset += render_info_size;

		VkBufferCopy mesh_copy = {
			.srcOffset = src_offset,
			.dstOffset = base_index * sizeof(GPU_Mesh),
			.size = mesh_size
		};
		vkCmdCopyBuffer(cmd, staging.buffer, mesh_buffer.buffer, 1, &mesh_copy);
	});

	destroy_buffer(staging);
}

void Vk_Backend::update_meshes(Entity e, Model_Handle handle) {
	auto it = mesh_allocations.find(e);
	if (it == mesh_allocations.end()) {
		return;
	}

	const Range_Allocation& alloc = it->second;
	Model& model = Model_Manager::get_model(handle);

	mat4 entity_transform = e.get<Transform_Component>().world_transform;

	std::vector<mat4> transforms;
	transforms.reserve(alloc.count);

	for (uint32_t i = 0; i < alloc.count; i++) {
		Mesh& mesh = model.meshes[i];

		mat4 world_transform = entity_transform * mesh.transform;
		transforms.push_back(world_transform);
	}

	update_buffer_range(transform_buffer, sizeof(mat4), alloc.base, transforms.data(), alloc.count);
}

void Vk_Backend::deallocate_model(Entity e) {
	auto it = mesh_allocations.find(e);
	if (it == mesh_allocations.end()) {
		return;
	}

	mesh_allocator.free(it->second);

	mesh_allocations.erase(it);

	// if we shrunk max allocations, dont need to fill
	// if we didnt, now have loose mesh data in between valid
	// need to manually disable these from being drawn
	// (set draw flag to false)
}

void Vk_Backend::allocate_light(Entity e, GPU_Light light) {
	if (light_allocations.find(e) != light_allocations.end()) {
		update_light(e, light);
		return;
	}

	if (num_lights >= MAX_LIGHTS) {
		assert(false && "MAX LIGHTS exceeded!");
		return;
	}

	uint32_t new_index;
	if (light_free_list.size() != 0) {
		new_index = light_free_list.back();
		light_free_list.pop_back();
	}
	else {
		new_index = num_lights;
		num_lights++;
	}

	light_allocations[e] = new_index;

	update_buffer_range(light_buffer, sizeof(GPU_Light), new_index, &light, 1);

	printf("[RENDERER] ADDED NEW LIGHT %d\n", num_lights);
}

void Vk_Backend::update_light(Entity e, GPU_Light light) {
	auto it = light_allocations.find(e);
	if (it == light_allocations.end()) {
		return;
	}

	uint32_t index = it->second;

	update_buffer_range(light_buffer, sizeof(GPU_Light), index, &light, 1);
}

void Vk_Backend::deallocate_light(Entity e) {
	auto it = light_allocations.find(e);
	if (it == light_allocations.end()) {
		return;
	}

	uint32_t removed_index = it->second;
	uint32_t last_index = num_lights - 1;

	// if removing last, just remove
	if (removed_index == last_index) {
		light_allocations.erase(e);
		num_lights--;
		return;
	}

	light_free_list.push_back(removed_index);
	light_allocations.erase(e);

	GPU_Light l = {}; // zero initialized light is disabled
	update_buffer_range(light_buffer, sizeof(GPU_Light), removed_index, &l, 1);
}

void Vk_Backend::update_buffer_range(Allocated_Buffer& buffer, size_t element_size, uint32_t start_index, const void* data, uint32_t count) {
	size_t total_size = count * element_size;
	size_t dst_offset = start_index * element_size;

	Allocated_Buffer staging = create_buffer(total_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);

	void* mapped;
	vmaMapMemory(_allocator, staging.allocation, &mapped);
	memcpy(mapped, data, total_size);
	vmaUnmapMemory(_allocator, staging.allocation);

	immediate_submit([&](VkCommandBuffer cmd) {
		VkBufferCopy copy = {
			.srcOffset = 0,
			.dstOffset = dst_offset,
			.size = total_size
		};
		vkCmdCopyBuffer(cmd, staging.buffer, buffer.buffer, 1, &copy);
	});

	destroy_buffer(staging);
}

void Vk_Backend::cleanup() {
	vkDeviceWaitIdle(_device);

	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	for (int i = 0; i < FRAME_OVERLAP; i++) {
		vkDestroyCommandPool(_device, _frames[i]._commandPool, nullptr);

		//destroy sync objects
		vkDestroyFence(_device, _frames[i]._renderFence, nullptr);

		// todo do i need? if swapchain image count semaphore
		//vkDestroySemaphore(_device, _frames[i]._renderSemaphore, nullptr);
		vkDestroySemaphore(_device, _frames[i]._swapchainSemaphore, nullptr);
		_frames[i]._deletionQueue.flush();

		_frames[i]._frameDescriptors.destroy_pools(_device);
	}

	Texture_Manager::cleanup();

	_mainDeletionQueue.flush();
	globalDescriptorAllocator.destroy_pool(_device);
	vkDestroyDescriptorSetLayout(_device, _drawImageDescriptorLayout, nullptr);

	vkDestroyFence(_device, _immFence, nullptr);
	vkDestroyCommandPool(_device, _immCommandPool, nullptr);

	vkDestroyImageView(_device, _drawImage.imageView, nullptr);
	vmaDestroyImage(_allocator, _drawImage.image, _drawImage.allocation);
	vkDestroyImageView(_device, _depthImage.imageView, nullptr);
	vmaDestroyImage(_allocator, _depthImage.image, _depthImage.allocation);

	vmaDestroyAllocator(_allocator);

	for (int i = 0; i < _swapchainImageCount; i++)
		vkDestroySemaphore(_device, _readyForPresentSemaphores[i], nullptr);

	destroy_swapchain();

	vkDestroySurfaceKHR(_instance, _surface, nullptr);
	vkDestroyDevice(_device, nullptr);

	vkb::destroy_debug_utils_messenger(_instance, _debug_messenger);
	vkDestroyInstance(_instance, nullptr);
}

void Vk_Backend::render(const mat4& projection, const mat4& view) {
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
		// trigger resize
	}

	VkCommandBuffer cmd = get_current_frame()._mainCommandBuffer;
	// now that we are sure that the commands finished executing, we can safely
	// reset the command buffer to begin recording again.
	VK_CHECK(vkResetCommandBuffer(cmd, 0));

	//begin the command buffer recording. We will use this command buffer exactly once, so we want to let vulkan know that
	VkCommandBufferBeginInfo cmdBeginInfo = command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	float renderScale = 1.0f; // todo me
	_drawExtent.height = std::min(_swapchainExtent.height, _drawImage.imageExtent.height) * renderScale;
	_drawExtent.width = std::min(_swapchainExtent.width, _drawImage.imageExtent.width) * renderScale;

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	generate_draw_commands(cmd);

	//make the swapchain image into writeable mode before rendering
	transition_image(cmd, _drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

	draw_background(cmd);

	transition_image(cmd, _drawImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	transition_image(cmd, _depthImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

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
	VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submit, get_current_frame()._renderFence));

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
	VkResult presentResult = vkQueuePresentKHR(_graphicsQueue, &presentInfo);
	if (presentResult == VK_ERROR_OUT_OF_DATE_KHR) {
		resize_requested = true;
		// trigger resize
	}

	_frameNumber++;
}

void Vk_Backend::generate_draw_commands(VkCommandBuffer cmd) {
	vkCmdFillBuffer(cmd, command_count_buffer.buffer, 0, sizeof(Command_Counts), 0);

	VkMemoryBarrier resetBarrier{};
	resetBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	resetBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	resetBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

	vkCmdPipelineBarrier(cmd,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		0, 1, &resetBarrier, 0, nullptr, 0, nullptr);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mesh_cull_pipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mesh_cull_pipeline_layout, 0, 1, &mesh_cull_descriptor_set, 0, nullptr);

	Cull_Push_Constants pc;
	//pc.mesh_count = total_mesh_count;
	pc.mesh_count = mesh_allocator.max_allocated;
	//pc.selected_lod = lod;
	//pc.camera_pos = camera_position;
	vkCmdPushConstants(cmd, mesh_cull_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

	uint32_t dispatch_count = (pc.mesh_count + 255) / 256;
	vkCmdDispatch(cmd, dispatch_count, 1, 1);

	// barrier for culling
	VkMemoryBarrier barrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
	barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
	vkCmdPipelineBarrier(cmd,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
		0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void Vk_Backend::draw_background(VkCommandBuffer cmd) {
	//make a clear-color from frame number. This will flash with a 120 frame period.
	VkClearColorValue clearValue;
	//float flash = std::abs(std::sin(_frameNumber / 120.f));
	clearValue = { { 0.0f, 0.0f, 0.5f, 1.0f } };

	VkImageSubresourceRange clearRange = image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT);

	//clear image
	vkCmdClearColorImage(cmd, _drawImage.image, VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &clearRange);

	//ComputeEffect& effect = backgroundEffects[currentBackgroundEffect];

	//vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, effect.pipeline);

	//// bind the descriptor set containing the draw image for the compute pipeline
	//vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _gradientPipelineLayout, 0, 1, &_drawImageDescriptors, 0, nullptr);

	//vkCmdPushConstants(cmd, _gradientPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants), &effect.data);

	//// execute the compute pipeline dispatch. We are using 16x16 workgroup size so we need to divide by it
	//vkCmdDispatch(cmd, std::ceil(_drawExtent.width / 16.0), std::ceil(_drawExtent.height / 16.0), 1);
}

void Vk_Backend::draw_geometry(VkCommandBuffer cmd, const mat4& projection, const mat4& view) {
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

	//VkDescriptorSet imageSet = get_current_frame()._frameDescriptors.allocate(_device, _singleImageDescriptorLayout);
	//{
	//	DescriptorWriter writer;
	//	//writer.write_image(0, _errorCheckerboardImage.imageView, _defaultSamplerNearest, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

	//	//writer.update_set(_device, imageSet);
	//}

	////vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _opaquePipelineLayout, 0, 1, &imageSet, 0, nullptr);
	VkDescriptorSet _bindlessDescriptorSet = Texture_Manager::get_bindless_descriptor_set();
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, draw_pipeline_layout,
		0, 1, &_bindlessDescriptorSet, 0, nullptr);

	gpu_push_constants.projection = projection;
	gpu_push_constants.view = view;
	gpu_push_constants.max_lights = num_lights;
	vkCmdPushConstants(cmd, draw_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(GPU_Push_Constants), &gpu_push_constants);

	vkCmdBindIndexBuffer(cmd, index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);

	// draw
	vkCmdDrawIndexedIndirectCount(cmd,
		opaque_command_buffer.buffer, 0,
		command_count_buffer.buffer, offsetof(Command_Counts, opaque),
		MAX_DRAW_COMMANDS,
		sizeof(VkDrawIndexedIndirectCommand));

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, transparent_pipeline);

	// draw
	vkCmdDrawIndexedIndirectCount(cmd,
		transparent_command_buffer.buffer, 0,
		command_count_buffer.buffer, offsetof(Command_Counts, transparent),
		MAX_DRAW_COMMANDS,
		sizeof(VkDrawIndexedIndirectCommand));

	vkCmdEndRendering(cmd);
}

Allocated_Buffer Vk_Backend::create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage) {
	// allocate buffer
	VkBufferCreateInfo bufferInfo = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	bufferInfo.pNext = nullptr;
	bufferInfo.size = allocSize;

	bufferInfo.usage = usage;

	VmaAllocationCreateInfo vmaallocInfo = {};
	vmaallocInfo.usage = memoryUsage;
	vmaallocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
	Allocated_Buffer newBuffer;

	// allocate the buffer
	VK_CHECK(vmaCreateBuffer(_allocator, &bufferInfo, &vmaallocInfo, &newBuffer.buffer, &newBuffer.allocation, &newBuffer.info));

	return newBuffer;
}

void Vk_Backend::destroy_buffer(const Allocated_Buffer& buffer) {
	vmaDestroyBuffer(_allocator, buffer.buffer, buffer.allocation);
}

void Vk_Backend::immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function) {
	VK_CHECK(vkResetFences(_device, 1, &_immFence));
	VK_CHECK(vkResetCommandBuffer(_immCommandBuffer, 0));

	VkCommandBuffer cmd = _immCommandBuffer;

	VkCommandBufferBeginInfo cmdBeginInfo = command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	function(cmd);

	VK_CHECK(vkEndCommandBuffer(cmd));

	VkCommandBufferSubmitInfo cmdinfo = command_buffer_submit_info(cmd);
	VkSubmitInfo2 submit = submit_info(&cmdinfo, nullptr, nullptr);

	// submit command buffer to the queue and execute it.
	//  _renderFence will now block until the graphic commands finish execution
	VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submit, _immFence));

	VK_CHECK(vkWaitForFences(_device, 1, &_immFence, true, 9999999999));
}

FrameData& Vk_Backend::get_current_frame() {
	return _frames[_frameNumber % FRAME_OVERLAP];
};
