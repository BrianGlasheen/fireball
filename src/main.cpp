#include "camera.h"
#include "asset/model_manager.h"
#include "asset/texture_manager.h"
#include "renderer/vk_util.h"
#include "renderer/vk_backend.h"
//#include "scene/scene.h"
#include "util/math.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <cstdio>
#include <cstdint>
#include <vector>
#include <span>

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
float renderScale = 1.0f;

Vk_Backend renderer;

int lod = 0;

GPU_Mesh_Buffers upload_mesh(std::span<uint32_t> indices, std::span<Vertex> vertices) {
	std::vector<VkDrawIndexedIndirectCommand> opaque_cmds;
	std::vector<VkDrawIndexedIndirectCommand> transparent_cmds;
	std::vector<mat4> transforms;
	std::vector<GPU_Material> materials;

	uint32_t i = 0;
	for (Model& model : Model_Manager::get_models()) {
		for (Mesh& mesh : model.meshes) {
			mat4 transform = mesh.transform;
			transforms.push_back(transform);

			GPU_Material material;
			material.albedo = mesh.material.albedo;
			material.normal = mesh.material.normal;
			material.alpha_cutoff = mesh.material.alpha_cutoff;
			material.blending = mesh.material.blend ? 1 : 0;
			materials.push_back(material);

			VkDrawIndexedIndirectCommand cmd;
			cmd.indexCount = mesh.lods[lod].index_count;
			cmd.instanceCount = 1;
			cmd.firstIndex = mesh.lods[lod].base_index;
			cmd.vertexOffset = mesh.base_vertex;
			cmd.firstInstance = i;
			i++;

			if (mesh.material.blend)
				transparent_cmds.push_back(cmd);
			else
				opaque_cmds.push_back(cmd);
		}
	}

	renderer.opaque_count = opaque_cmds.size();
	renderer.transparent_count = transparent_cmds.size();

	size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
	size_t indexBufferSize = indices.size() * sizeof(uint32_t);
	size_t transformBufferSize = transforms.size() * sizeof(mat4);
	size_t materialBufferSize = materials.size() * sizeof(GPU_Material);
	size_t opaque_cmds_size = opaque_cmds.size() * sizeof(VkDrawIndexedIndirectCommand);
	size_t transparent_cmds_size = transparent_cmds.size() * sizeof(VkDrawIndexedIndirectCommand);

	GPU_Mesh_Buffers mesh_buffer;

	//create vertex buffer
	mesh_buffer.vertex_buffer = renderer.create_buffer(vertexBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
	//find the adress of the vertex buffer
	VkBufferDeviceAddressInfo deviceAdressInfo{ .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = mesh_buffer.vertex_buffer.buffer };
	renderer.gpu_push_constants.vertex_buffer = vkGetBufferDeviceAddress(renderer._device, &deviceAdressInfo);

	//create index buffer
	mesh_buffer.index_buffer = renderer.create_buffer(indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

	mesh_buffer.transform_buffer = renderer.create_buffer(transformBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
	deviceAdressInfo = { .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = mesh_buffer.transform_buffer.buffer };
	renderer.gpu_push_constants.transform_buffer = vkGetBufferDeviceAddress(renderer._device, &deviceAdressInfo);

	mesh_buffer.material_buffer = renderer.create_buffer(materialBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
	deviceAdressInfo = { .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,.buffer = mesh_buffer.material_buffer.buffer };
	renderer.gpu_push_constants.material_buffer = vkGetBufferDeviceAddress(renderer._device, &deviceAdressInfo);

	renderer.opaque_command_buffer = renderer.create_buffer(opaque_cmds_size, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

	renderer.transparent_command_buffer = renderer.create_buffer(transparent_cmds_size, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

	Allocated_Buffer staging = renderer.create_buffer(vertexBufferSize + indexBufferSize + transformBufferSize + materialBufferSize + opaque_cmds_size + transparent_cmds_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);

	//void* data = staging.allocation->GetMappedData();
	void* data;
	vmaMapMemory(renderer._allocator, staging.allocation, &data);

	// copy vertex buffer
	memcpy(data, vertices.data(), vertexBufferSize);
	memcpy((char*)data + vertexBufferSize, indices.data(), indexBufferSize);
	memcpy((char*)data + vertexBufferSize + indexBufferSize, transforms.data(), transformBufferSize);
	memcpy((char*)data + vertexBufferSize + indexBufferSize + transformBufferSize, materials.data(), materialBufferSize);
	memcpy((char*)data + vertexBufferSize + indexBufferSize + transformBufferSize + materialBufferSize, opaque_cmds.data(), opaque_cmds_size);
	memcpy((char*)data + vertexBufferSize + indexBufferSize + transformBufferSize + materialBufferSize + opaque_cmds_size, transparent_cmds.data(), transparent_cmds_size);

	vmaUnmapMemory(renderer._allocator, staging.allocation);

	renderer.immediate_submit([&](VkCommandBuffer cmd) {
		VkBufferCopy vertexCopy = {};
		vertexCopy.dstOffset = 0;
		vertexCopy.srcOffset = 0;
		vertexCopy.size = vertexBufferSize;

		vkCmdCopyBuffer(cmd, staging.buffer, mesh_buffer.vertex_buffer.buffer, 1, &vertexCopy);
		VkBufferCopy indexCopy = {};
		indexCopy.dstOffset = 0;
		indexCopy.srcOffset = vertexBufferSize;
		indexCopy.size = indexBufferSize;
		vkCmdCopyBuffer(cmd, staging.buffer, mesh_buffer.index_buffer.buffer, 1, &indexCopy);

		VkBufferCopy transformCopy = {};
		transformCopy.dstOffset = 0;
		transformCopy.srcOffset = vertexBufferSize + indexBufferSize;
		transformCopy.size = transformBufferSize;
		vkCmdCopyBuffer(cmd, staging.buffer, mesh_buffer.transform_buffer.buffer, 1, &transformCopy);

		VkBufferCopy materialCopy = {};
		materialCopy.dstOffset = 0;
		materialCopy.srcOffset = vertexBufferSize + indexBufferSize + transformBufferSize;
		materialCopy.size = materialBufferSize;
		vkCmdCopyBuffer(cmd, staging.buffer, mesh_buffer.material_buffer.buffer, 1, &materialCopy);

		VkBufferCopy opaqueCopy = {};
		opaqueCopy.dstOffset = 0;
		opaqueCopy.srcOffset = vertexBufferSize + indexBufferSize + transformBufferSize + materialBufferSize;
		opaqueCopy.size = opaque_cmds_size;
		vkCmdCopyBuffer(cmd, staging.buffer, renderer.opaque_command_buffer.buffer, 1, &opaqueCopy);

		VkBufferCopy transparentCopy = {};
		transparentCopy.dstOffset = 0;
		transparentCopy.srcOffset = vertexBufferSize + indexBufferSize + transformBufferSize + materialBufferSize + opaque_cmds_size;
		transparentCopy.size = transparent_cmds_size;
		vkCmdCopyBuffer(cmd, staging.buffer, renderer.transparent_command_buffer.buffer, 1, &transparentCopy);
	});

	renderer.destroy_buffer(staging);

	return mesh_buffer;
}

void init_models() {
	//Model_Manager::load_model("submarine/scene.gltf", Mesh_Opt_Flags_All);
	//Model_Manager::load_model("bistro/bistro.gltf");
	Model_Manager::load_model("CompareAlphaTest/AlphaBlendModeTest.gltf", Mesh_Opt_Flags_All);
	Model_Manager::load_model("house/scene.gltf");
	//Model_Manager::load_model("factory/scene.gltf");

	renderer.geometry_buffer = upload_mesh(Model_Manager::get_indices(), Model_Manager::get_vertices());

	renderer._mainDeletionQueue.push_function([&]() {
		renderer.destroy_buffer(renderer.geometry_buffer.index_buffer);
		renderer.destroy_buffer(renderer.geometry_buffer.vertex_buffer);
		renderer.destroy_buffer(renderer.geometry_buffer.transform_buffer);
		renderer.destroy_buffer(renderer.geometry_buffer.material_buffer);
		renderer.destroy_buffer(renderer.opaque_command_buffer);
		renderer.destroy_buffer(renderer.transparent_command_buffer);
	});
}

int main() {
    printf("hello vk\n");

	// init window
	// init renderer
		// init textures
		// init model manager

	// init imgui

	// window
    if (!glfwInit()) {
        printf("glfw init failed\n");
        return 1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	// todo multiply width height by scaling
    GLFWwindow* window = glfwCreateWindow(width, height, "fireball", nullptr, nullptr);
	
	glfwSetMouseButtonCallback(window, mouseCallback);
	glfwSetKeyCallback(window, keyCallback);
	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	//

	if (renderer.init(window, width, height, validation_layers)) {
		fprintf(stderr, "Renderer failed to initialize\n");
		return 1;
	}

	Texture_Manager::init(&renderer);
	Model_Manager::init("../resources/models/");
	init_models();

	Camera camera;

	double dt;
	double last_frame = 0.0;
	uint32_t fps_frames = 0;
	double fps_timer = 0.0;

    while (!glfwWindowShouldClose(window)) {
		fps_frames++;
		double current_time = glfwGetTime();
		dt = current_time - last_frame;
		last_frame = current_time;
		fps_timer += dt;

		if (fps_timer >= 0.25) {
			double fps = fps_frames / fps_timer;
			double frame_ms = 1000.0 / fps;

			char title[256];
			snprintf(title, sizeof(title), "fireball | %.1f FPS | %.2f ms", fps, frame_ms);
			glfwSetWindowTitle(window, title);

			fps_timer = 0.0;
			fps_frames = 0;
		}

		glfwPollEvents();

		int32_t newWidth, newHeight;
		glfwGetWindowSize(window, &newWidth, &newHeight);
		if (newWidth == 0 || newHeight == 0) {
			continue;
		}
		else if (newWidth != width || newHeight != height) {
			//resize_requested = true;
		}

		//if (glfwGetWindowAttrib(window, GLFW_ICONIFIED)) { // minimized
		//	int w = 0, h = 0;
		//	while (w == 0 || h == 0) {
		//		glfwGetFramebufferSize(window, &w, &h);
		//		glfwWaitEvents();
		//	}
		//}

		//if (resize_requested) {
		//	resize_swapchain(window);
		//}

		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

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

		mat4 view = camera.get_view();
		mat4 projection = camera.get_projection((float)width / (float)height);

		renderer.render(projection, view);

		ImGuiIO& io = ImGui::GetIO();
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
		}
    }

	renderer.cleanup();

	glfwDestroyWindow(window);
	glfwTerminate();

    return 0;
}
