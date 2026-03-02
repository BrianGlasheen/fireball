#include "fireball/core/physics.h"
#include "fireball/networking/server.h"
#include "fireball/scene/components.h"
#include "fireball/scene/serializer.h"
#include "fireball/scene/scene.h"
#include "fireball/util/math.h"
#include "fireball/util/time.h"

#include <cstdio>
#include <cstdint>
#include <iostream>
#include <thread>

bool running = true;

struct Player {
    HSteamNetConnection conn;
    std::string name;
};

std::unordered_map<HSteamNetConnection, Player> g_players;

int main() {
    printf("fireball server starting\n");

	Server server;
	Physics::init();
	Scene scene(nullptr);

	server.on_client_joined = [&](HSteamNetConnection conn, const std::string& name) {
        g_players[conn] = { conn, name };
        printf("[SERVER] '%s' joined (%zu players online)\n", name.c_str(), g_players.size());

        // TODO:serialize scene entity list into a snapshot buffer and send via:
        // server.send_to(conn, { NetMsg::FullSnapshot, <scene_buffer> });
    };

    server.on_client_left = [&](HSteamNetConnection conn) {
        auto it = g_players.find(conn);
        if (it != g_players.end()) {
            printf("[SERVER] '%s' left (%zu players remaining)\n",
                   it->second.name.c_str(), g_players.size() - 1);
            g_players.erase(it);
        }
    };

	server.on_full_snapshot = [&]() {
		return serialize_scene(scene.world);
	};

	Entity e2 = scene.create_entity("plane");
	e2.get_mut<Transform_Component>().scale = vec3(100.0f);
	e2.set<Server_Model_Component>({ "plane.obj" });

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

	Physics_Handle plane_handle = Physics::add_object(plane_info, true);
	e2.set<Physics_Component>({plane_handle, plane_info});
	
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

		if (i % 2 == 0)
			e.set<Server_Model_Component>({ "ciri/scene.gltf" });
		else
			e.set<Server_Model_Component>({ "wand/scene.gltf" });

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
		// printf("physics id %d", ph);
		e.set<Physics_Component>({ ph, box });
	}

    double dt = 0.0;
    double fps_timer = 0.0;
    uint32_t fps_frames = 0;

	Physics::optimize_broad_phase();

	std::thread console_thread([]() {
		std::string input;

		while (running) {
			std::getline(std::cin, input);

			if (input == "q" || input == "quit") {
				printf("[SERVER] Shutdown requested...\n");
				running = false;
				break;
			}
		}
	});

	short port = 5678;
	server.start(port);
	
    Time last_frame = high_resolution_clock::now();
    while (running) {
		Time now = high_resolution_clock::now();
		dt = duration<double>(now - last_frame).count();
		last_frame = now;

		fps_frames++;
		fps_timer += dt;

		if (fps_timer >= 0.5) {
			double fps = fps_frames / fps_timer;
			double frame_ms = 1000.0 / fps;
			fps_timer = 0.0;
			fps_frames = 0;

			// printf("[SERVER] %.1f FPS | %.2f ms\n", fps, frame_ms);
		}

		// int state = static_cast<int>(game_state);
		// if (ImGui::SliderInt("Game state", &state, 0, static_cast<int>(Game_State::NUM_GAME_STATE) - 1)) {
		// 	game_state = static_cast<Game_State>(state);
		// }

		// get network 
		Physics::update();
		scene.update(dt);
		server.tick();
		// send network data
	}

	printf("[SERVER] Shutting down...\n");

	server.stop();
	// Physics::shutdown();

	if (console_thread.joinable())
		console_thread.join();

	printf("[SERVER] Shutdown\n");

	return 0;
}
