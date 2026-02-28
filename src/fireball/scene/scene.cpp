#include "scene.h"

#include "components.h"

#include "fireball/core/physics.h"
#include "fireball/renderer/vk_backend.h"

#include <imgui.h>

Scene::Scene(Vk_Backend* _renderer) {
    renderer = _renderer;

    world.system<Physics_Component>()
    .kind(flecs::OnUpdate)
    .each([this](Entity e, Physics_Component& pc) {
        Transform_Component& tc = e.get_mut<Transform_Component>();
        tc.position = Physics::get_pos(pc.handle);
        tc.rotation = Physics::get_rot(pc.handle);
        tc.dirty = true;

        // printf("id %lu pos %f %f %f\n", e.id(), tc.position.x, tc.position.y, tc.position.z);
    });

    world.observer<Physics_Component>()
    .event(flecs::OnSet)
    .each([this](Entity e, const Physics_Component& pc) {
        // TODO if physics info changes remove body and readd with. Can either use new info or stuff from current info
    });

    world.observer<Physics_Component>()
    .event(flecs::OnRemove)
    .each([this](Entity e, const Physics_Component& pc) {
        Physics::remove_body(pc.handle);
    });

    world.system<Transform_Component>("Transform_System")
    .kind(flecs::OnUpdate)
    .multi_threaded(false)
    .with<Transform_Component>(flecs::ChildOf).parent().optional().cascade()
    .each([](flecs::entity e, Transform_Component& t) {
        auto parent = e.parent();
        Transform_Component* parent_transform = nullptr;
        if (parent && parent.has<Transform_Component>()) {
            parent_transform = &parent.get_mut<Transform_Component>();
        }

        if (e.has<Motion>()) {
            t.rotation.y += 0.01f;
            t.dirty = true;
        }

        if (!t.dirty && (!parent_transform || !parent_transform->updated)) {
            t.updated = false;
            return;
        }

        mat4 translation = translate(mat4(1.0f), t.position);
        mat4 rotation = yawPitchRoll(t.rotation.y, t.rotation.x, t.rotation.z);
        mat4 scale = glm::scale(mat4(1.0f), t.scale);
        mat4 local_transform = translation * scale * rotation;

        if (parent_transform) {
            t.world_transform = parent_transform->world_transform * local_transform;
        }
        else {
            t.world_transform = local_transform;
        }

        t.dirty = false;
        t.updated = true;
    });


    world.system<Model_Component>()
    .kind(flecs::OnUpdate)
    .each([this](Entity e, Model_Component& m) {
        if (e.get<Transform_Component>().updated) {
            //printf("[SCENE] updating entity %s, model %s\n", e.get<Name_Component>().string.c_str(), Model_Manager::get_model_name(m.handle).c_str());

            renderer->update_meshes(e, m.handle);
        }

        // if materials new then update

        // mesh flags change (shadows, invisible)
    });
    
    world.observer<Model_Component>()
    .event(flecs::OnSet)
    .each([this](Entity e, Model_Component& model) {
        printf("[SCENE] WATCHER TRIGGERED for entity %s, model %s\n", e.get<Name_Component>().string.c_str(), Model_Manager::get_model_name(model.handle).c_str());

        renderer->allocate_model(e, model.handle);
    });

    world.observer<Model_Component>()
    .event(flecs::OnRemove)
    .each([this](Entity e, const Model_Component& model) {
        renderer->deallocate_model(e);
    });


    world.system<Light_Component>()
    .kind(flecs::OnUpdate)
    .each([this](Entity e, Light_Component& light) {
        if (light.dirty || e.get<Transform_Component>().updated) {
            GPU_Light l {
                .position_radius = vec4(vec3(e.get<Transform_Component>().world_transform[3]), light.range),
                .color_strength = vec4(light.color, light.intensity),
                .direction_type = vec4(light.direction, light.type == Light_Component::Type::Point ? 0.0f : 1.0f),
                .params = vec4(light.inner_cone_angle, light.outer_cone_angle, 0, light.enabled ? 1.0f : 0.0f)
            };

            renderer->update_light(e, l);
            light.dirty = false;
            //printf("updated light\n");
        }
    });

    world.observer<Light_Component>()
    .event(flecs::OnSet)
    .each([this](Entity e, const Light_Component& light) {
        //vec4 params; // inner cone, outer cone, shadow map idx, enabled 
        GPU_Light l {
            .position_radius = vec4(vec3(e.get<Transform_Component>().world_transform[3]), light.range),
            .color_strength = vec4(light.color, light.intensity),
            .direction_type = vec4(light.direction, light.type == Light_Component::Type::Point ? 0.0f : 1.0f),
            .params = vec4(light.inner_cone_angle, light.outer_cone_angle, 0, light.enabled ? 1.0f : 0.0f)
        };

        renderer->allocate_light(e, l);
    });

    world.observer<Light_Component>()
    .event(flecs::OnRemove)
    .each([this](Entity e, const Light_Component& l) {
        renderer->deallocate_light(e);
    });
}

void Scene::update(float dt) {
    world.progress(dt);
}

Entity Scene::create_entity(const std::string& name) {
    auto e = world.entity()
        .add<Transform_Component>()
        .set<Name_Component>({ name });

    return e;
}

void Scene::remove_entity(Entity e) {
    e.destruct();
}

void Scene::show_entity_inspector() {
    ImGui::Begin("Entity Inspector");

    world.defer_begin();

    ImGui::BeginChild("EntityList", ImVec2(200, 0), true);

    static Entity selected_entity;
    static Entity dragged_entity;

    std::function<void(Entity)> draw_entity_tree = [&](Entity e) {
        std::string name = e.get<Name_Component>().string;

        bool has_children = false;
        e.children([&](Entity child) {
            has_children = true;
        });

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
            ImGuiTreeNodeFlags_SpanAvailWidth |
            ImGuiTreeNodeFlags_OpenOnDoubleClick;

        if (!has_children) {
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }

        if (selected_entity == e) {
            flags |= ImGuiTreeNodeFlags_Selected;
        }

        bool node_open = ImGui::TreeNodeEx((void*)(intptr_t)e.id(), flags, "%s", name.c_str());

        if (ImGui::IsItemClicked()) {
            selected_entity = e;
        }

        // drag source
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            Entity payload = e;
            ImGui::SetDragDropPayload("ENTITY_DND", &payload, sizeof(Entity));
            ImGui::Text("%s", name.c_str());
            ImGui::EndDragDropSource();
        }

        // drop target
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_DND")) {
                printf("GOT DROP\n");
                Entity dropped = *(Entity*)payload->Data;

                if (!dropped.is_alive() || !e.is_alive()) {
                    ImGui::EndDragDropTarget();
                    return;
                }

                bool is_valid = true;
                if (dropped == e) {
                    is_valid = false;
                }
                else {
                    Entity check = e;
                    while (check.is_alive() && check.has(flecs::ChildOf, flecs::Wildcard)) {
                        Entity check_parent = check.target(flecs::ChildOf);
                        if (check_parent == dropped) {
                            is_valid = false;
                            break;
                        }
                        check = check_parent;
                    }
                }

                if (is_valid) {
                    dropped.remove(flecs::ChildOf, flecs::Wildcard);
                    dropped.add(flecs::ChildOf, e);
                    if (dropped.has<Transform_Component>()) {
                        dropped.get_mut<Transform_Component>().dirty = true;
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

        // right click menu
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Unparent")) {
                Entity parent = e.target(flecs::ChildOf);
                if (parent.is_alive()) {
                    e.remove(flecs::ChildOf, flecs::Wildcard);
                    if (e.has<Transform_Component>()) {
                        e.get_mut<Transform_Component>().dirty = true;
                    }
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Delete Entity")) {
                if (selected_entity == e) {
                    selected_entity = flecs::entity();
                }
                e.destruct();
                ImGui::EndPopup();
                if (node_open && has_children) ImGui::TreePop();
                return;
            }
            ImGui::EndPopup();
        }

        if (node_open && has_children) {
            e.children([&](Entity child) {
                draw_entity_tree(child);
            });
            ImGui::TreePop();
        }
    };

    world.query_builder<Name_Component>()
    .with(flecs::ChildOf, flecs::Wildcard).oper(flecs::Not)
    .build()
    .each([&](Entity e, Name_Component&) {
        draw_entity_tree(e);
    });

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_DND")) {
            uint64_t dropped_id = *(uint64_t*)payload->Data;
            Entity dropped = world.entity(dropped_id);

            if (dropped.is_alive()) {
                dropped.remove(flecs::ChildOf, flecs::Wildcard);
                if (dropped.has<Transform_Component>()) {
                    dropped.get_mut<Transform_Component>().dirty = true;
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("ComponentInspector", ImVec2(0, 0), true);

    if (selected_entity.is_alive()) {
        std::string name = selected_entity.get<Name_Component>().string;

        ImGui::Text("Entity: %s (ID: %lu)", name.c_str(), selected_entity.id());
        ImGui::Separator();

        display_component<Name_Component>(selected_entity, "Name", [](Name_Component& name) {
            char buffer[256];
            strncpy(buffer, name.string.c_str(), sizeof(buffer));
            if (ImGui::InputText("Name", buffer, sizeof(buffer))) {
                name.string = buffer;
            }
        });

        display_component<Transform_Component>(selected_entity, "Transform", [](Transform_Component& t) {
            bool changed = false;

            changed |= ImGui::DragFloat3("Position", &t.position.x, 0.1f);
            ImGui::SameLine();
            if (ImGui::Button("Reset")) {
                t.position = vec3(0.0f);
                t.dirty = true;
            }

            changed |= ImGui::DragFloat3("Rotation", &t.rotation.x, 1.0f, 0.0f, 360.0f);
            ImGui::SameLine();
            if (ImGui::Button("Reset")) {
                t.rotation = vec3(0.0f);
                t.dirty = true;
            }

            changed |= ImGui::DragFloat3("Scale", &t.scale.x, 0.01f, 0.001f, 100.0f);

            if (changed) {
                t.dirty = true;
            }
        });

        display_component<Model_Component>(selected_entity, "Model", [](Model_Component& m) {
            ImGui::Text("%s", Model_Manager::get_model_name(m.handle).c_str());
        });

        display_component<Light_Component>(selected_entity, "Light", [](Light_Component& l) {
            bool changed = false;

            const char* type_names[] = { "Point", "Spot" };
            int type = static_cast<int>(l.type);
            if (ImGui::Combo("Type", &type, type_names, IM_ARRAYSIZE(type_names))) {
                l.type = static_cast<Light_Component::Type>(type);
                changed = true;
            }

            changed |= ImGui::ColorEdit3("Color", &l.color.x);
            changed |= ImGui::DragFloat("Intensity", &l.intensity, 0.1f, 0.0f, 1000.0f);
            changed |= ImGui::DragFloat("Range", &l.range, 0.1f, 0.01f, 1000.0f);

            if (l.type == Light_Component::Type::Spot) {
                if (ImGui::DragFloat3("Direction", &l.direction.x, 0.01f)) {
                    l.direction = glm::normalize(l.direction);
                    changed = true;
                }
                changed |= ImGui::DragFloat("Inner Cone (deg)", &l.inner_cone_angle, 0.1f, 0.0f, 89.0f);
                changed |= ImGui::DragFloat("Outer Cone (deg)", &l.outer_cone_angle, 0.1f, l.inner_cone_angle, 90.0f);
            }

            if (changed)
                l.dirty = true;
        });

        ImGui::Separator();

        if (ImGui::Button("Add Component")) {
            ImGui::OpenPopup("AddComponentPopup");
        }

        if (ImGui::BeginPopup("AddComponentPopup")) {
            if (!selected_entity.has<Name_Component>() && ImGui::MenuItem("Name")) {
                selected_entity.set<Name_Component>({ "Entity" });
            }
            if (!selected_entity.has<Transform_Component>() && ImGui::MenuItem("Transform")) {
                selected_entity.set<Transform_Component>({ vec3(0,0,0), vec3(0,0,0), vec3(1,1,1) });
            }
            if (!selected_entity.has<Model_Component>() && ImGui::MenuItem("Model")) {
                selected_entity.set<Model_Component>({ 0 });
            }
            if (!selected_entity.has<Light_Component>() && ImGui::MenuItem("Light")) {
                selected_entity.set<Light_Component>({ });
            }
            ImGui::EndPopup();
        }

    }
    else {
        ImGui::TextDisabled("No entity selected");
    }

    ImGui::EndChild();

    world.defer_end();

    ImGui::End();
}

template<typename T>
bool Scene::display_component(Entity e, const char* name, std::function<void(T&)> ui_func) {
    if (!e.has<T>()) return false;

    ImGuiTreeNodeFlags header_flags =
        ImGuiTreeNodeFlags_DefaultOpen |
        ImGuiTreeNodeFlags_Framed;

    bool open = ImGui::CollapsingHeader(name, header_flags);

    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Remove Component")) {
            e.remove<T>();
        }
        ImGui::EndPopup();
    }

    if (open) {
        ImGui::PushID(name);
        T* component = e.try_get_mut<T>();
        if (component) {
            ui_func(*component);
            //e.modified<T>();
        }
        ImGui::PopID();
    }

    return true;
}
