#pragma once

#include "scene/components.h"
#include "util/math.h"

#include <flecs.h>

#include <functional>
#include <string>

using Entity = flecs::entity ;

class Scene {
public:
    //vec3 sun_direction = normalize(vec3(0.0, -1.0f, -1.0f));
    //vec3 sun_color = vec3(1.0f);
    //float sun_strength = 0.5f;

    Scene() = default;
    ~Scene() = default;

    void init();

    Entity create_entity(const std::string& name = "Entity");
    void remove_entity(Entity e);

    void show_entity_inspector();
    template<typename T>
    bool display_component(Entity e, const char* name, std::function<void(T&)> ui_func);

    flecs::world world;
};
