#include "scene/scene.h"

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
