#pragma once

#include "asset/model_manager.h"
#include "components.h"

#include "fireball/scene/scene.h"

#include <unordered_map>

// enum class Ecs_Message : uint8_t {
//     FullSnapshot    = 1,  // initial join / full resync
//     TransformDelta  = 2,  // position/rotation updates
//     ComponentSet    = 3,  // one component changed
//     EntityDestroyed = 4,
// };

struct ByteWriter {
    std::vector<uint8_t> data;

    template<typename T>
    void write(const T& val) {
        const uint8_t* ptr = reinterpret_cast<const uint8_t*>(&val);
        data.insert(data.end(), ptr, ptr + sizeof(T));
    }

    void write_string(const std::string& s) {
        uint16_t len = static_cast<uint16_t>(s.size());
        write(len);
        data.insert(data.end(), s.begin(), s.end());
    }
};

struct ByteReader {
    const uint8_t* ptr;
    size_t remaining;

    ByteReader(const uint8_t* data, size_t size) : ptr(data), remaining(size) {}

    template<typename T>
    bool read(T& out) {
        if (remaining < sizeof(T)) return false;
        memcpy(&out, ptr, sizeof(T));
        ptr += sizeof(T);
        remaining -= sizeof(T);
        return true;
    }

    bool read_string(std::string& out) {
        uint16_t len;
        if (!read(len)) return false;
        if (remaining < len) return false;
        out.assign(reinterpret_cast<const char*>(ptr), len);
        ptr += len;
        remaining -= len;
        return true;
    }
};

inline void serialize(ByteWriter& w, const Name_Component& n) {
    w.write_string(n.string);
}

inline bool deserialize(ByteReader& r, Name_Component& n) {
    return r.read_string(n.string);
}

inline void serialize(ByteWriter& w, const Server_Model_Component& m) {
    w.write_string(m.model_name);
}

inline bool deserialize(ByteReader& r, Server_Model_Component& m) {
    return r.read_string(m.model_name);
}

inline void serialize(ByteWriter& w, const Light_Component& l) {
    w.write(l.type);
    w.write(l.color);
    w.write(l.intensity);
    w.write(l.range);
    w.write(l.direction);
    w.write(l.inner_cone_angle);
    w.write(l.outer_cone_angle);
    w.write(l.enabled);
}

inline bool deserialize(ByteReader& r, Light_Component& l) {
    if (!r.read(l.type)) return false;
    if (!r.read(l.color)) return false;
    if (!r.read(l.intensity)) return false;
    if (!r.read(l.range)) return false;
    if (!r.read(l.direction)) return false;
    if (!r.read(l.inner_cone_angle)) return false;
    if (!r.read(l.outer_cone_angle)) return false;
    if (!r.read(l.enabled)) return false;
    l.dirty = true;
    return true;
}

//   [uint64 entity_id]
//   [uint64 parent_id]  (0 if no parent)
//   [uint8  component_count]
//   for each component:
//     [uint8  NetComponentID]
//     [uint16 byte_length]
//     [data...]

enum class NetComponentID : uint8_t {
    Transform = 1,
    Name = 2,
    ServerModel = 3,
    Light = 4,
};

inline void serialize(ByteWriter& w, const Transform_Component& t) {
    w.write(t.position);
    w.write(t.rotation);
    w.write(t.scale);
}

inline bool deserialize(ByteReader& r, Transform_Component& t) {
    if (!r.read(t.position)) return false;
    if (!r.read(t.rotation)) return false;
    if (!r.read(t.scale)) return false;
    t.dirty = true; // mark dirty so transform system recalculates
    t.updated = false;
    return true;
}

struct NetEntity {
    uint64_t entity_id;
    uint64_t parent_id;

    struct NetComponent {
        NetComponentID id;
        std::vector<uint8_t> data;
    };
    std::vector<NetComponent> components;
};

static std::vector<uint8_t> serialize_entity(flecs::entity e) {
    ByteWriter w;

    w.write(e.id());

    uint64_t parent_id = 0;
    if (e.has(flecs::ChildOf, flecs::Wildcard)) {
        parent_id = e.target(flecs::ChildOf).id();
    }
    w.write(parent_id);

    std::vector<std::pair<NetComponentID, std::vector<uint8_t>>> comps;

    auto try_serialize = [&]<typename T>(NetComponentID id) {
        if (e.has<T>()) {
            ByteWriter cw;
            serialize(cw, *e.try_get<T>());
            comps.push_back({ id, std::move(cw.data) });
        }
    };

    try_serialize.template operator()<Transform_Component>(NetComponentID::Transform);
    try_serialize.template operator()<Name_Component>(NetComponentID::Name);
    try_serialize.template operator()<Server_Model_Component>(NetComponentID::ServerModel);
    try_serialize.template operator()<Light_Component>(NetComponentID::Light);

    w.write(static_cast<uint8_t>(comps.size()));

    for (auto& [id, data] : comps) {
        w.write(id);
        w.write(static_cast<uint16_t>(data.size()));
        w.data.insert(w.data.end(), data.begin(), data.end());
    }

    return w.data;
}

static std::vector<uint8_t> serialize_scene(flecs::world& world) {
    ByteWriter w;

    std::vector<std::vector<uint8_t>> entity_bufs;

    // in hierarchy order
    std::function<void(flecs::entity)> visit = [&](flecs::entity e) {
        entity_bufs.push_back(serialize_entity(e));
        e.children([&](flecs::entity child) {
            visit(child);
        });
    };

    world.query_builder<Name_Component>()
        .with(flecs::ChildOf, flecs::Wildcard).oper(flecs::Not)
        .build()
        .each([&](Entity e, Name_Component& nc) {
            visit(e);
            printf("serializing %s\n", nc.c_str());
        });

    w.write(static_cast<uint32_t>(entity_bufs.size()));
    for (auto& buf : entity_bufs) {
        w.write(static_cast<uint32_t>(buf.size()));
        w.data.insert(w.data.end(), buf.begin(), buf.end());
    }

    return w.data;
}

#ifdef FIREBALL_CLIENT

static bool apply_component(Entity e, NetComponentID id, ByteReader& r) {
    switch (id) {
        case NetComponentID::Transform: {
            Transform_Component t;
            if (!deserialize(r, t)) return false;
            e.set<Transform_Component>(t);
            return true;
        }
        case NetComponentID::Name: {
            Name_Component n;
            if (!deserialize(r, n)) return false;
            e.set<Name_Component>(n);
            return true;
        }
        case NetComponentID::ServerModel: {
            Server_Model_Component m;
            if (!deserialize(r, m)) return false;
            e.set<Model_Component>({ Model_Manager::load_model(m.model_name) });
            return true;
        }
        case NetComponentID::Light: {
            Light_Component l;
            if (!deserialize(r, l)) return false;
            e.set<Light_Component>(l);
            return true;
        }
        default:
            return false;
    }
}

static bool deserialize_entity(Scene& scene, ByteReader& r, std::unordered_map<uint64_t, flecs::entity>& id_map) {
    uint64_t server_id, parent_server_id;
    if (!r.read(server_id))        return false;
    if (!r.read(parent_server_id)) return false;

    flecs::entity e;
    auto it = id_map.find(server_id);
    if (it != id_map.end()) {
        e = it->second;
    } else {
        e = scene.create_entity();
        id_map[server_id] = e;
    }

    if (parent_server_id != 0) {
        auto pit = id_map.find(parent_server_id);
        if (pit != id_map.end()) {
            e.add(flecs::ChildOf, pit->second);
        }
    }

    uint8_t comp_count;
    if (!r.read(comp_count)) return false;

    for (uint8_t i = 0; i < comp_count; i++) {
        NetComponentID comp_id;
        uint16_t comp_size;
        if (!r.read(comp_id))   return false;
        if (!r.read(comp_size)) return false;
        if (r.remaining < comp_size) return false;

        ByteReader cr(r.ptr, comp_size);
        r.ptr       += comp_size;
        r.remaining -= comp_size;

        if (!apply_component(e, comp_id, cr)) return false;
    }

    return true;
}

// id_map: maps server entity IDs -> local entity IDs
static bool deserialize_scene(
    Scene& scene,
    const uint8_t* data, size_t size,
    std::unordered_map<uint64_t, flecs::entity>& id_map)
{
    ByteReader r(data, size);

    uint32_t entity_count;
    if (!r.read(entity_count)) return false;

    for (uint32_t i = 0; i < entity_count; i++) {
        uint32_t entity_size;
        if (!r.read(entity_size)) return false;
        if (r.remaining < entity_size) return false;

        ByteReader er(r.ptr, entity_size);
        r.ptr       += entity_size;
        r.remaining -= entity_size;

        if (!deserialize_entity(scene, er, id_map)) return false;
    }

    return true;
}

#endif
