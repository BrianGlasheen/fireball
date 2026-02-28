#include "fireball/scene/scene.h"

#include "fireball/scene/components.h"

Scene::Scene(Vk_Backend* _renderer) {
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
