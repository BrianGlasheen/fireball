#include "camera.h"
#include "asset/model_manager.h"
#include "asset/texture_manager.h"
#include "core/physics.h"
#include "renderer/vk_util.h"
#include "renderer/vk_backend.h"
#include "scene/components.h"
#include "scene/scene.h"
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

static bool g_spawn_requested = false;

void mouseCallback(GLFWwindow* window, int button, int action, int mods) {
	ImGuiIO& io = ImGui::GetIO();
	if (io.WantCaptureMouse) {
		return;
	}

    if (action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_RIGHT) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
    else if (action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_LEFT) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        g_spawn_requested = true;
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
uint32_t width = 1600, height = 900;
float renderScale = 1.0f;

Vk_Backend renderer;

int lod = 0;

int main() {
    printf("hello vk\n");

	// init window
	// init renderer
		// init textures
		// init model manager

	// window
    if (!glfwInit()) {
        fprintf(stderr, "glfw init failed\n");
        return 1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	// todo multiply width height by scaling
    GLFWwindow* window = glfwCreateWindow(width, height, "fireball", nullptr, nullptr);
	
	glfwSetMouseButtonCallback(window, mouseCallback);
	glfwSetKeyCallback(window, keyCallback);
	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	//	static std::vector<AllocatedImage> _bindlessTextures;


	if (renderer.init(window, width, height, validation_layers)) {
		fprintf(stderr, "Renderer failed to initialize\n");
		return 1;
	}

	Texture_Manager::init(&renderer);
	Model_Manager::init("../resources/models/");
	Physics::init();

	Scene scene(&renderer);

	// Model_Handle test = Model_Manager::load_model("CompareAlphaTest/AlphaBlendModeTest.gltf", Mesh_Opt_Flags_All);
	Model_Handle wand = Model_Manager::load_model("wand/scene.gltf");
	Model_Handle ciri = Model_Manager::load_model("turtle/scene.gltf");
	// Model_Handle ciri = Model_Manager::load_model("bistro/bistro.gltf");
	Model_Handle plane = Model_Manager::load_model("plane.obj");
	//Model_Manager::load_model("factory/scene.gltf");
	//Model_Manager::load_model("minecraft/scene.gltf");
	Model_Manager::wait_for_all_loads();
	Texture_Manager::wait_for_all_loads();

	Entity e2 = scene.create_entity("plane");
	e2.get_mut<Transform_Component>().scale = vec3(100.0f);
	e2.set<Model_Component>({ plane });

	Physics_Info plane_info = {
		.shape = Physics_Shape::Plane,
		.pos = vec3(0.0f),
		.orientation = quat(1.0f, 0.0f, 0.0f, 0.0f),
		.scale = vec3(100.0f, 2.0f, 100.0f),
	};

	Physics_Info box = {
		.shape = Physics_Shape::Box,
		.pos = vec3(0.0f),
		.orientation = quat(1.0f, 0.0f, 0.0f, 0.0f),
		.scale = vec3(1.0f),
	};

	printf("hern\n");
	Physics_Handle plane_handle = Physics::add_object(plane_info, true);
	printf("hern\n");

	e2.set<Physics_Component>({plane_handle, plane_info});
	printf("hern2\n");
	
	Entity prev;
	for (int i = 0; i < 5; i++) {
		Entity e = scene.create_entity(std::to_string(i));

		if (prev.is_alive()) {
			e.add(flecs::ChildOf, prev);
		}
		//prev = e;

		Transform_Component& tc = e.get_mut<Transform_Component>();
		tc.position.y += 50 * i;
		tc.dirty = true;

		Model_Handle handle;
		if (i % 2 == 0)
			handle = ciri;
		else
			handle = wand;

		auto entityName = e.get<Name_Component>().string;
		auto modelName = Model_Manager::get_model_name(handle);
		printf("[MAIN] adding entity %s, model %s\n", entityName.c_str(), modelName.c_str());

		e.set<Model_Component>({ handle });

		Light_Component l{
			.type = Light_Component::Type::Point,
			.color = vec3(1.0f),
			.intensity = 10.0f,
			.range = 100.0f,
			//.direction
			.inner_cone_angle = 30.0f,
			.outer_cone_angle = 45.0f,
			.dirty = true,
			.enabled = true
		};

		//e.add<Motion>();
		e.set<Light_Component>({ l });

		box.pos = tc.position;
		Physics_Handle ph = Physics::add_object(box);
		printf("physics id %d", ph);
		e.set<Physics_Component>({ ph, box });
	}

	renderer.upload_geometry(Model_Manager::get_indices(), Model_Manager::get_vertices());

	renderer.init_mesh_cull_descriptors();

	auto spawn_entity = [&](const vec3& pos) {
		static int spawn_count = 0;
		Entity e = scene.create_entity("spawned_" + std::to_string(spawn_count++));

		Transform_Component& tc = e.get_mut<Transform_Component>();
		tc.position = pos;
		tc.dirty = true;

		Model_Handle handle = (spawn_count % 2 == 0) ? ciri : wand;
		e.set<Model_Component>({ handle });

		Physics_Info info = {
			.shape = Physics_Shape::Box,
			.pos   = pos,
			.orientation = quat(1.0f, 0.0f, 0.0f, 0.0f),
			.scale = vec3(1.0f),
		};

		Physics_Handle ph = Physics::add_object(info, false);
		e.set<Physics_Component>({ ph, info });

		printf("[MAIN] spawned entity at (%.2f, %.2f, %.2f)\n", pos.x, pos.y, pos.z);
	};

	Camera camera;

	double dt;
	double last_frame = 0.0;
	uint32_t fps_frames = 0;
	double fps_timer = 0.0;

	Physics::optimize_broad_phase();
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

		// TODO stuff code in some corner
		scene.world.query<Light_Component>()
		.each([&](Entity e, const Light_Component& light) {
			vec3 pos = vec3(e.get<Transform_Component>().world_transform[3]);
			renderer.debug_renderer.add_point(
				pos,
				vec4(light.color, 1)
			);
			renderer.debug_renderer.add_line(
				pos, pos + vec3(0.0f, 10.0f, 0.0f)
			);
		});

		scene.world.query<Model_Component>()
			.each([&](Entity e, const Model_Component& model) {
			if (model.handle.animated) {
				const auto& bones = Model_Manager::get_model_bones(model.handle);
				mat4 worldTransform = e.get<Transform_Component>().world_transform;

				for (const Bone& b : bones) {
					mat4 bindPose = glm::inverse(b.inverse_bind);
					vec3 boneLocalPos = vec3(bindPose[3]);
					vec3 boneWorldPos = vec3(worldTransform * vec4(boneLocalPos, 1.0f));

					renderer.debug_renderer.add_point(boneWorldPos, vec4(1.0f, 0.0f, 0.0f, 1.0f));
				}
			}
		});

		scene.world.query<Physics_Component>()
		.each([&](Entity e, const Physics_Component& pc)
		{
			if (pc.info.shape != Physics_Shape::Box &&
				pc.info.shape != Physics_Shape::Plane)
				return;

			vec3 center = Physics::get_pos(pc.handle);
			quat rot = Physics::get_orientation(pc.handle);
			vec3 half = pc.info.scale * 0.5f;

			vec4 green(0.0f, 1.0f, 0.0f, 1.0f);

			vec3 corners[8] = {
				{-half.x, -half.y, -half.z},
				{ half.x, -half.y, -half.z},
				{ half.x,  half.y, -half.z},
				{-half.x,  half.y, -half.z},

				{-half.x, -half.y,  half.z},
				{ half.x, -half.y,  half.z},
				{ half.x,  half.y,  half.z},
				{-half.x,  half.y,  half.z},
			};

			for (int i = 0; i < 8; ++i)
				corners[i] = center + (rot * corners[i]);

			auto& dbg = renderer.debug_renderer;

			dbg.add_line(corners[0], corners[1], green);
			dbg.add_line(corners[1], corners[2], green);
			dbg.add_line(corners[2], corners[3], green);
			dbg.add_line(corners[3], corners[0], green);

			dbg.add_line(corners[4], corners[5], green);
			dbg.add_line(corners[5], corners[6], green);
			dbg.add_line(corners[6], corners[7], green);
			dbg.add_line(corners[7], corners[4], green);

			dbg.add_line(corners[0], corners[4], green);
			dbg.add_line(corners[1], corners[5], green);
			dbg.add_line(corners[2], corners[6], green);
			dbg.add_line(corners[3], corners[7], green);
		});

		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		if (ImGui::Begin("Camera Controls")) {
			ImGui::SliderFloat("X", &camera.position.x, -5000.0f, 5000.0f);
			ImGui::SliderFloat("Y", &camera.position.y, -5000.0f, 5000.0f);
			ImGui::SliderFloat("Z", &camera.position.z, -5000.0f, 5000.0f);

			ImGui::SliderFloat("Pitch", &camera.pitch, -89.0f, 89.0f);
			ImGui::SliderFloat("Yaw", &camera.yaw, -180.0f, 180.0f);
			ImGui::SliderFloat("Zoom", &camera.zoom, -180.0f, 180.0f);

			ImGui::End();
		}

		scene.show_entity_inspector();

		ImGui::Render();

		if (!ImGui::GetIO().WantCaptureMouse) {
			double xpos, ypos;
			glfwGetCursorPos(window, &xpos, &ypos);
			camera.update(xpos, ypos);
			camera.move(window, dt);
		}

		Physics::update();

		if (g_spawn_requested) {
			g_spawn_requested = false;
			vec3 spawn_pos = camera.position + camera.front * 5.0f;
			spawn_entity(spawn_pos);
		}

		scene.update(dt);

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
	// Physics::shutdown(); // ??

	glfwDestroyWindow(window);
	glfwTerminate();

    return 0;
}
