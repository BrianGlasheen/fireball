#include "scene/scene.h"

#include <imgui.h>

void Scene::init() {

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

    world.each<Name_Component>([](Entity e, Name_Component n) {
        std::string name = e.get<Name_Component>().string;

        ImGuiTreeNodeFlags flags =
            ImGuiTreeNodeFlags_Leaf |
            ImGuiTreeNodeFlags_SpanAvailWidth;

        if (selected_entity == e) {
            flags |= ImGuiTreeNodeFlags_Selected;
        }

        bool node_open = ImGui::TreeNodeEx((void*)(intptr_t)e.id(), flags, "%s", name.c_str());

        if (ImGui::IsItemClicked())
            selected_entity = e;

        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Delete Entity")) {
                if (selected_entity == e) {
                    selected_entity = flecs::entity();
                }
                e.destruct();
                ImGui::EndPopup();
                if (node_open) ImGui::TreePop();
                return;
            }
            ImGui::EndPopup();
        }

        if (node_open)
            ImGui::TreePop();

    });

    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("ComponentInspector", ImVec2(0, 0), true);

    if (selected_entity.is_alive()) {
        std::string name = selected_entity.get<Name_Component>().string;

        ImGui::Text("Entity: %s (ID: %llu)", name.c_str(), selected_entity.id());
        ImGui::Separator();

        display_component<Name_Component>(selected_entity, "Name", [](Name_Component& name) {
            char buffer[256];
            strncpy(buffer, name.string.c_str(), sizeof(buffer));
            if (ImGui::InputText("Name", buffer, sizeof(buffer))) {
                name.string = buffer;
            }
        });

        display_component<Transform_Component>(selected_entity, "Transform", [](Transform_Component& t) {
            ImGui::DragFloat3("Position", &t.position.x, 0.1f);
            ImGui::DragFloat3("Rotation", &t.orientation.x, 1.0f, -180.0f, 180.0f);
            ImGui::DragFloat3("Scale", &t.scale.x, 0.01f, 0.001f, 100.0f);
        });

        display_component<Model_Component>(selected_entity, "Model", [](Model_Component& m) {
            ImGui::InputInt("Model Index", (int*)&m.handle);
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
            e.modified<T>();
        }
        ImGui::PopID();
    }

    return true;
}
