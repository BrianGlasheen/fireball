#include "fireball/networking/client.h" // TODO fix this has to come before any glm include i think..

#include "fireball/camera.h"
#include "fireball/asset/model_manager.h"
#include "fireball/asset/texture_manager.h"
#include "fireball/core/physics.h"
#include "fireball/networking/client.h"
#include "fireball/renderer/vk_backend.h"
#include "fireball/scene/components.h"
#include "fireball/scene/serializer.h"
#include "fireball/scene/scene.h"
#include "fireball/util/math.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <cstdio>
#include <cstdint>
#include <span>
#include <unordered_map>

static bool g_spawn_requested = false;

enum class Game_State {
	MAIN_MENU = 0,
	CONNECTING,
	LOADING,
	PLAYING,
	PAUSE_MENU,
	NUM_GAME_STATE
};

static Game_State game_state = Game_State::MAIN_MENU;

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

	Model_Manager::load_model("drag/scene.gltf");
	// Model_Manager::load_model("911/scene.gltf");
	
	Model_Manager::wait_for_all_loads();
	Texture_Manager::wait_for_all_loads();

	renderer.upload_geometry(Model_Manager::get_indices(), Model_Manager::get_vertices());

	// create entities from list that server has
	std::unordered_map<uint64_t, Entity> id_map;
	
	Camera camera;
	Client client;
	client.on_snapshot = [&](const uint8_t* data , size_t bytes) {
		deserialize_scene(scene, data, bytes, id_map);
    };

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

		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		renderer.begin_frame();

		int state = static_cast<int>(game_state);
		if (ImGui::SliderInt("Game state", &state, 0, static_cast<int>(Game_State::NUM_GAME_STATE) - 1)) {
			game_state = static_cast<Game_State>(state);
		}

		static char ip_buffer[64] = "127.0.0.1";
		static int port = 5678;
		static float fake_loading_progress = 0.0f;
		static char name[16] = "client";

		client.tick();
		// check data from client to set game state

		if (game_state == Game_State::MAIN_MENU) {
			ImGui::Begin("Main Menu", nullptr,
				ImGuiWindowFlags_NoResize |
				ImGuiWindowFlags_NoCollapse |
				ImGuiWindowFlags_AlwaysAutoResize);

			ImGui::Text("Connect to Server");
			ImGui::Separator();

			ImGui::InputText("IP Address", ip_buffer, IM_ARRAYSIZE(ip_buffer));
			ImGui::InputInt("Port", &port);
			ImGui::InputText("Name", name, IM_ARRAYSIZE(name));

			if (ImGui::Button("Connect")) {
				// read network
				fake_loading_progress = 0.0f;
				client.connect(ip_buffer, port, name);
				// try to connect to server
				// set state to connecting
				game_state = Game_State::PLAYING;
			}

			ImGui::SameLine();

			if (ImGui::Button("Quit")) {
			}

			ImGui::End();

			renderer.draw_blank(vec4(0.4f, 0.7f, 0.2f, 1.0f));
		}
		else if (game_state == Game_State::LOADING) {
			// poll server
			// parse data
			// do what's needed with said data
			// update display / log
			// if done loading change state 

			fake_loading_progress += 0.005f;
			if (fake_loading_progress >= 1.0f) {
				fake_loading_progress = 1.0f;
				game_state = Game_State::PLAYING;
			}

			ImGui::Begin("Loading...", nullptr,
				ImGuiWindowFlags_NoResize |
				ImGuiWindowFlags_NoCollapse |
				ImGuiWindowFlags_AlwaysAutoResize);

			ImGui::Text("Connecting to server...");
			ImGui::ProgressBar(fake_loading_progress, ImVec2(300, 0));

			if (ImGui::Button("Cancel"))
				game_state = Game_State::MAIN_MENU;

			ImGui::End();

			renderer.draw_blank(vec4(0.4f, 0.2f, 0.7f, 1.0f));
		}
		else if (game_state == Game_State::PLAYING) {
			fake_loading_progress = 0.0f;

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

			if (!ImGui::GetIO().WantCaptureMouse) {
				double xpos, ypos;
				glfwGetCursorPos(window, &xpos, &ypos);
				camera.update(xpos, ypos);
				camera.move(window, dt);
			}

			// pull network data if exists
			// reconcile diffs if large
			// if good, keep predicting / interpolating with current info
			// poll input / requests
			// queue in net buffer
			// if tick send update, or just send, tbd
			// if disconnect send message and cleanup for main menu

			Physics::update();

			if (g_spawn_requested) {
				g_spawn_requested = false;
				vec3 spawn_pos = camera.position + camera.front * 5.0f;
				// spawn_entity(spawn_pos);
			}

			scene.update(dt);

			mat4 view = camera.get_view();
			mat4 projection = camera.get_projection((float)width / (float)height);

			renderer.clear_color  = vec4(0.2f, 0.2f, 0.8f, 1.0f);
			renderer.render(projection, view);
		}

		ImGui::Render();
		renderer.end_frame_and_submit();

		ImGuiIO& io = ImGui::GetIO();
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
		}
    }

	// if (client.connected)
	client.disconnect();

	renderer.cleanup();
	// Physics::shutdown(); // ??

	glfwDestroyWindow(window);
	glfwTerminate();

    return 0;
}
