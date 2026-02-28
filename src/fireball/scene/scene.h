#pragma once

#include "entity.h"

#include <flecs.h>

#include <functional>
#include <string>

class Vk_Backend;

class Scene {
public:
    //vec3 sun_direction = normalize(vec3(0.0, -1.0f, -1.0f));
    //vec3 sun_color = vec3(1.0f);
    //float sun_strength = 0.5f;

    Scene(Vk_Backend* _renderer);
    ~Scene() = default;

    void update(float dt);

    Entity create_entity(const std::string& name = "Entity");
    void remove_entity(Entity e);

    void show_entity_inspector();
    template<typename T>
    bool display_component(Entity e, const char* name, std::function<void(T&)> ui_func);

    flecs::world world;
    Vk_Backend* renderer;

private:
    // void register_physics_systems();
    // void register_transform_systems();
    // void register_render_systems();
    // void register_lifetime_systems();
};
