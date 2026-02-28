#include "physics.h"

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Collision/RayCast.h>
//#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/CastResult.h>

#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Character/CharacterBase.h>

#include <iostream>
#include <cstdarg>
#include <thread>

using namespace JPH;
using namespace JPH::literals;

namespace Physics {
    class ObjectLayerPairFilterImpl;
    class BPLayerInterfaceImpl;
    class ObjectVsBroadPhaseLayerFilterImpl;
    class MyContactListener;
    class MyBodyActivationListener;

    struct PhysicsState {
        std::unique_ptr<TempAllocatorImpl> tempAllocator;
        std::unique_ptr<JobSystemThreadPool> jobSystem;
        std::unique_ptr<PhysicsSystem> physicsSystem;
        std::unique_ptr<BPLayerInterfaceImpl> broadPhaseLayerInterface;
        std::unique_ptr<ObjectVsBroadPhaseLayerFilterImpl> objectVsBroadphaseLayerFilter;
        std::unique_ptr<ObjectLayerPairFilterImpl> objectVsObjectLayerFilter;
        std::unique_ptr<MyBodyActivationListener> bodyActivationListener;
        std::unique_ptr<MyContactListener> contactListener;
    };

    static PhysicsState g_state;

    namespace Layers {
        static constexpr ObjectLayer NON_MOVING = 0;
        static constexpr ObjectLayer MOVING = 1;
        static constexpr ObjectLayer CHARACTER = 2;
        static constexpr ObjectLayer NUM_LAYERS = 3;
    };

    namespace BroadPhaseLayers {
        static constexpr BroadPhaseLayer NON_MOVING(0);
        static constexpr BroadPhaseLayer MOVING(1);
        static constexpr uint NUM_LAYERS(2);
    };

    static void TraceImpl(const char* inFMT, ...) {
        va_list list;
        va_start(list, inFMT);
        char buffer[1024];
        vsnprintf(buffer, sizeof(buffer), inFMT, list);
        va_end(list);
        std::cout << buffer << std::endl;
    }

#ifdef JPH_ENABLE_ASSERTS
    static bool AssertFailedImpl(const char* inExpression, const char* inMessage, const char* inFile, uint inLine) {
        std::cout << inFile << ":" << inLine << ": (" << inExpression << ") " << (inMessage != nullptr ? inMessage : "") << std::endl;
        return true;
    };
#endif

    class ObjectLayerPairFilterImpl : public ObjectLayerPairFilter {
    public:
        virtual bool ShouldCollide(ObjectLayer inObject1, ObjectLayer inObject2) const override {
            switch (inObject1) {
            case Layers::NON_MOVING:
                return inObject2 == Layers::MOVING;
            case Layers::MOVING:
                return true;
            default:
                JPH_ASSERT(false);
                return false;
            }
        }
    };

    class BPLayerInterfaceImpl final : public BroadPhaseLayerInterface {
    public:
        BPLayerInterfaceImpl() {
            mObjectToBroadPhase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
            mObjectToBroadPhase[Layers::MOVING] = BroadPhaseLayers::MOVING;
        }

        virtual uint GetNumBroadPhaseLayers() const override {
            return BroadPhaseLayers::NUM_LAYERS;
        }

        virtual BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer inLayer) const override {
            JPH_ASSERT(inLayer < Layers::NUM_LAYERS);
            return mObjectToBroadPhase[inLayer];
        }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
        virtual const char* GetBroadPhaseLayerName(BroadPhaseLayer inLayer) const override {
            switch ((BroadPhaseLayer::Type)inLayer) {
            case (BroadPhaseLayer::Type)BroadPhaseLayers::NON_MOVING: return "NON_MOVING";
            case (BroadPhaseLayer::Type)BroadPhaseLayers::MOVING: return "MOVING";
            default: JPH_ASSERT(false); return "INVALID";
            }
        }
#endif

    private:
        BroadPhaseLayer mObjectToBroadPhase[Layers::NUM_LAYERS];
    };

    class ObjectVsBroadPhaseLayerFilterImpl : public ObjectVsBroadPhaseLayerFilter {
    public:
        virtual bool ShouldCollide(ObjectLayer inLayer1, BroadPhaseLayer inLayer2) const override {
            switch (inLayer1) {
            case Layers::NON_MOVING:
                return inLayer2 == BroadPhaseLayers::MOVING;
            case Layers::MOVING:
                return true;
            default:
                JPH_ASSERT(false);
                return false;
            }
        }
    };

    class MyContactListener : public ContactListener {
    public:
        virtual ValidateResult OnContactValidate(const Body& inBody1, const Body& inBody2, RVec3Arg inBaseOffset, const CollideShapeResult& inCollisionResult) override {
            return ValidateResult::AcceptAllContactsForThisBodyPair;
        }

        virtual void OnContactAdded(const Body& inBody1, const Body& inBody2, const ContactManifold& inManifold, ContactSettings& ioSettings) override {
            // Handle contact added
            //std::cout << "A contact was added" << std::endl;
        }

        virtual void OnContactPersisted(const Body& inBody1, const Body& inBody2, const ContactManifold& inManifold, ContactSettings& ioSettings) override {
            // Handle contact persisted
            //std::cout << "A contact was persisted" << std::endl;
        }

        virtual void OnContactRemoved(const SubShapeIDPair& inSubShapePair) override {
            // Handle contact removed
            //std::cout << "A contact was removed" << std::endl;
        }
    };

    class MyBodyActivationListener : public BodyActivationListener {
    public:
        virtual void OnBodyActivated(const BodyID& inBodyID, uint64 inBodyUserData) override {
            std::cout << "A body got activated" << std::endl;
        }

        virtual void OnBodyDeactivated(const BodyID& inBodyID, uint64 inBodyUserData) override {
            std::cout << "A body went to sleep" << std::endl;
        }
    };

    // Public API Implementation
    bool init() {
        // Register allocation hook.In this example we'll just let Jolt use malloc / free but you can override these if you want (see Memory.h).
        // This needs to be done before any other Jolt function is called.
        RegisterDefaultAllocator();

        // Install trace and assert callbacks
        Trace = TraceImpl;
        JPH_IF_ENABLE_ASSERTS(AssertFailed = AssertFailedImpl;)

        // Create a factory, this class is responsible for creating instances of classes based on their name or hash and is mainly used for deserialization of saved data.
        // It is not directly used in this example but still required.
        Factory::sInstance = new Factory();

        // Register all physics types with the factory and install their collision handlers with the CollisionDispatch class.
        // If you have your own custom shape types you probably need to register their handlers with the CollisionDispatch before calling this function.
        // If you implement your own default material (PhysicsMaterial::sDefault) make sure to initialize it before this function or else this function will create one for you.
        RegisterTypes();

        // We need a temp allocator for temporary allocations during the physics update. We're
        // pre-allocating 10 MB to avoid having to do allocations during the physics update.
        // B.t.w. 10 MB is way too much for this example but it is a typical value you can use.
        // If you don't want to pre-allocate you can also use TempAllocatorMalloc to fall back to
        // malloc / free.
        g_state.tempAllocator = std::make_unique<TempAllocatorImpl>(10 * 1024 * 1024);

        // We need a job system that will execute physics jobs on multiple threads. Typically
        // you would implement the JobSystem interface yourself and let Jolt Physics run on top
        // of your own job scheduler. JobSystemThreadPool is an example implementation.
        g_state.jobSystem = std::make_unique<JobSystemThreadPool>(cMaxPhysicsJobs, cMaxPhysicsBarriers, std::thread::hardware_concurrency() - 1);

        // This is the max amount of rigid bodies that you can add to the physics system. If you try to add more you'll get an error.
        // Note: This value is low because this is a simple test. For a real project use something in the order of 65536.
        const uint cMaxBodies = 65536;
        // This determines how many mutexes to allocate to protect rigid bodies from concurrent access. Set it to 0 for the default settings.
        const uint cNumBodyMutexes = 0;
        // This is the max amount of body pairs that can be queued at any time (the broad phase will detect overlapping
        // body pairs based on their bounding boxes and will insert them into a queue for the narrowphase). If you make this buffer
        // too small the queue will fill up and the broad phase jobs will start to do narrow phase work. This is slightly less efficient.
        // Note: This value is low because this is a simple test. For a real project use something in the order of 65536.
        const uint cMaxBodyPairs = 65536;
        // This is the maximum size of the contact constraint buffer. If more contacts (collisions between bodies) are detected than this
        // number then these contacts will be ignored and bodies will start interpenetrating / fall through the world.
        // Note: This value is low because this is a simple test. For a real project use something in the order of 10240.
        const uint cMaxContactConstraints = 10240;

        // Create mapping table from object layer to broadphase layer
        // Note: As this is an interface, PhysicsSystem will take a reference to this so this instance needs to stay alive!
        // Also have a look at BroadPhaseLayerInterfaceTable or BroadPhaseLayerInterfaceMask for a simpler interface.
        g_state.broadPhaseLayerInterface = std::make_unique<BPLayerInterfaceImpl>();
        // Create class that filters object vs broadphase layers
        // Note: As this is an interface, PhysicsSystem will take a reference to this so this instance needs to stay alive!
        // Also have a look at ObjectVsBroadPhaseLayerFilterTable or ObjectVsBroadPhaseLayerFilterMask for a simpler interface.
        g_state.objectVsBroadphaseLayerFilter = std::make_unique<ObjectVsBroadPhaseLayerFilterImpl>();
        // Create class that filters object vs object layers
        // Note: As this is an interface, PhysicsSystem will take a reference to this so this instance needs to stay alive!
        // Also have a look at ObjectLayerPairFilterTable or ObjectLayerPairFilterMask for a simpler interface.
        g_state.objectVsObjectLayerFilter = std::make_unique<ObjectLayerPairFilterImpl>();

        // Now we can create the actual physics system.
        g_state.physicsSystem = std::make_unique<PhysicsSystem>();
        g_state.physicsSystem->Init(cMaxBodies, cNumBodyMutexes, cMaxBodyPairs, cMaxContactConstraints, *g_state.broadPhaseLayerInterface, *g_state.objectVsBroadphaseLayerFilter, *g_state.objectVsObjectLayerFilter);

        // A body activation listener gets notified when bodies activate and go to sleep
        // Note that this is called from a job so whatever you do here needs to be thread safe.
        // Registering one is entirely optional.
        g_state.bodyActivationListener = std::make_unique<MyBodyActivationListener>();
        // A contact listener gets notified when bodies (are about to) collide, and when they separate again.
        // Note that this is called from a job so whatever you do here needs to be thread safe.
        // Registering one is entirely optional.
        g_state.contactListener = std::make_unique<MyContactListener>();

        // A body activation listener gets notified when bodies activate and go to sleep
        // Note that this is called from a job so whatever you do here needs to be thread safe.
        // Registering one is entirely optional.
        g_state.physicsSystem->SetBodyActivationListener(g_state.bodyActivationListener.get());
        // A contact listener gets notified when bodies (are about to) collide, and when they separate again.
        // Note that this is called from a job so whatever you do here needs to be thread safe.
        // Registering one is entirely optional.   
        g_state.physicsSystem->SetContactListener(g_state.contactListener.get());

        printf("[PHYSICS] initialized successfully\n");
        return true;
    }

    void shutdown() {
        if (g_state.physicsSystem) {
            g_state.physicsSystem->SetBodyActivationListener(nullptr);
            g_state.physicsSystem->SetContactListener(nullptr);
        }

        g_state.contactListener.reset();
        g_state.bodyActivationListener.reset();
        g_state.physicsSystem.reset();
        g_state.objectVsObjectLayerFilter.reset();
        g_state.objectVsBroadphaseLayerFilter.reset();
        g_state.broadPhaseLayerInterface.reset();
        g_state.jobSystem.reset();
        g_state.tempAllocator.reset();

        delete Factory::sInstance;
        Factory::sInstance = nullptr;
    }

    void update(float deltaTime) {
        const int cCollisionSteps = 1; // todo change if running slow
        g_state.physicsSystem->Update(deltaTime, cCollisionSteps, g_state.tempAllocator.get(), g_state.jobSystem.get());
    }

    void optimize_broad_phase() {
        g_state.physicsSystem->OptimizeBroadPhase();
    }

    Physics_Handle add_object(const Physics_Info& info, bool is_static) {
        BodyInterface& body_interface = g_state.physicsSystem->GetBodyInterface();

        RVec3 position(info.pos.x, info.pos.y, info.pos.z);
        Quat rotation(info.orientation.w, info.orientation.x, info.orientation.y, info.orientation.z);

        EMotionType motion_type = is_static ? EMotionType::Static : EMotionType::Dynamic;

        RefConst<Shape> shape;

        switch (info.shape) {
            case Physics_Shape::Box:
            {
                Vec3 half_extents(info.scale.x * 0.5f, info.scale.y * 0.5f, info.scale.z * 0.5f);

                    shape = new BoxShape(half_extents);
                    break;
            }

            case Physics_Shape::Sphere:
            {
                float radius = info.scale.x * 0.5f;
                shape = new SphereShape(radius);
                break;
            }
            
            case Physics_Shape::Plane:
            {
                // TODO special jolt plane
                Vec3 half_extents(info.scale.x * 0.5f, 0.5f, info.scale.z * 0.5f);

                shape = new BoxShape(half_extents);
                motion_type = EMotionType::Static;
                break;
            }

            default:
                assert(false);
                return Physics_Handle{ 0 };
        }

        ObjectLayer layer = is_static ? Layers::NON_MOVING : Layers::MOVING;

        BodyCreationSettings settings(
            shape,
            position,
            rotation,
            motion_type,
            layer
        );

        // TODO physics material
        settings.mFriction = 0.5f;
        settings.mRestitution = 0.0f;

        if (!is_static) {
            settings.mLinearDamping = 0.05f;
            settings.mAngularDamping = 0.05f;
        }

        Body* body = body_interface.CreateBody(settings);

        body_interface.AddBody(body->GetID(), is_static ? EActivation::DontActivate : EActivation::Activate);

        return body->GetID();
    }

    vec3 get_pos(Physics_Handle handle) {
        BodyInterface& body_interface = g_state.physicsSystem->GetBodyInterface();
        RVec3 pos = body_interface.GetPosition(handle);

        return glm::vec3(static_cast<float>(pos.GetX()), static_cast<float>(pos.GetY()), static_cast<float>(pos.GetZ()));
    }

    vec3 get_rot(Physics_Handle handle) {
        BodyInterface& body_interface = g_state.physicsSystem->GetBodyInterface();
        Quat rot = body_interface.GetRotation(handle);

        return eulerAngles(quat(rot.GetX(), rot.GetY(), rot.GetZ(), rot.GetW()));
    }

    quat get_orientation(Physics_Handle handle) {
        BodyInterface& body_interface = g_state.physicsSystem->GetBodyInterface();
        Quat rot = body_interface.GetRotation(handle);

        return quat(rot.GetX(), rot.GetY(), rot.GetZ(), rot.GetW());
    }

    // JPH::BodyID add_box(const glm::vec3& pos, const glm::vec3& size, bool isStatic) {
    //     // Create box shape
    //     RefConst<Shape> box_shape = new BoxShape(Vec3(size.x * 0.5f, size.y * 0.5f, size.z * 0.5f));

    //     // Create body creation settings
    //     Vec3 box_half_extents = Vec3(size.x * 0.5f, size.y * 0.5f, size.z * 0.5f);
    //     RVec3 corrected_pos = RVec3(pos.x, pos.y + box_half_extents.GetY(), pos.z);

    //     BodyCreationSettings body_settings(box_shape, RVec3(pos.x, pos.y, pos.z),
    //         Quat::sIdentity(), isStatic ? EMotionType::Static : EMotionType::Dynamic,
    //         isStatic ? Layers::NON_MOVING : Layers::MOVING);

    //     body_settings.mRestitution = 0.2f;

    //     // Create body
    //     BodyInterface& body_interface = g_state.physicsSystem->GetBodyInterface();
    //     BodyID body_id = body_interface.CreateAndAddBody(body_settings, EActivation::Activate);

    //     return body_id;
    // }

    // JPH::BodyID add_sphere(const glm::vec3& pos, float radius, bool isStatic) {
    //     // Create sphere shape
    //     RefConst<Shape> sphere_shape = new SphereShape(radius);

    //     // Create body creation settings
    //     BodyCreationSettings body_settings(sphere_shape, RVec3(pos.x, pos.y, pos.z),
    //         Quat::sIdentity(), isStatic ? EMotionType::Static : EMotionType::Dynamic,
    //         isStatic ? Layers::NON_MOVING : Layers::MOVING);

    //     // Create body
    //     BodyInterface& body_interface = g_state.physicsSystem->GetBodyInterface();
    //     BodyID body_id = body_interface.CreateAndAddBody(body_settings, EActivation::Activate);

    //     return body_id;
    // }

    void remove_body(JPH::BodyID id) {
        BodyInterface& body_interface = g_state.physicsSystem->GetBodyInterface();
        body_interface.RemoveBody(id);
        body_interface.DestroyBody(id);
    }

    bool is_active(JPH::BodyID id) {
        BodyInterface& body_interface = g_state.physicsSystem->GetBodyInterface();
        return body_interface.IsActive(id);
    }
}
