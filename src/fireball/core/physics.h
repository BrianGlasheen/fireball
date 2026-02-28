#pragma once

#include "fireball/util/math.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Math/Vec3.h>
#include <Jolt/Math/Quat.h>

namespace JPH {
    class BodyInterface;
    class PhysicsSystem;
    class TempAllocatorImpl;
    class JobSystemThreadPool;
    class Body;
    class Shape;
    using BodyID = class BodyID;
}

typedef JPH::BodyID Physics_Handle;

enum class Physics_Shape {
    Box = 0,
    Sphere,
    Plane,
    Cylinder,
    Capsule,
    Mesh
};

struct Physics_Info {
    Physics_Shape shape;
    vec3 pos;
    quat orientation;
    vec3 scale;
    // stuff for mesh
};

namespace Physics {
    bool init();
    void shutdown();
    void update(float deltaTime = 1.0f / 60.0f);
    void optimize_broad_phase();

    Physics_Handle add_object(const Physics_Info& physics_info, bool is_static = false);

    vec3 get_pos(Physics_Handle handle);
    vec3 get_rot(Physics_Handle handle);
    quat get_orientation(Physics_Handle handle);

    void remove_body(JPH::BodyID id);

    bool is_active(JPH::BodyID id);
}
