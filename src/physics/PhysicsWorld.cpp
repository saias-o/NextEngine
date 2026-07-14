#include "physics/PhysicsWorld.hpp"

#include "core/Profiler.hpp"
#include "physics/JoltGlue.hpp"

#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#ifdef __EMSCRIPTEN__
#include <Jolt/Core/JobSystemSingleThreaded.h>
#endif
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>

#include "core/Log.hpp"

#include <algorithm>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace saida {

using namespace JPH;

namespace {

class BPLayerInterfaceImpl final : public BroadPhaseLayerInterface {
public:
    BPLayerInterfaceImpl() {
        mapping_[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
        mapping_[Layers::MOVING] = BroadPhaseLayers::MOVING;
    }
    uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::NUM_LAYERS; }
    BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer layer) const override {
        return mapping_[layer];
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(BroadPhaseLayer) const override { return "Layer"; }
#endif
private:
    BroadPhaseLayer mapping_[Layers::NUM_LAYERS];
};

class ObjectVsBroadPhaseLayerFilterImpl final : public ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(ObjectLayer layer, BroadPhaseLayer bpLayer) const override {
        switch (layer) {
            case Layers::NON_MOVING: return bpLayer == BroadPhaseLayers::MOVING;
            case Layers::MOVING: return true;
            default: return false;
        }
    }
};

class ObjectLayerPairFilterImpl final : public ObjectLayerPairFilter {
public:
    bool ShouldCollide(ObjectLayer a, ObjectLayer b) const override {
        switch (a) {
            case Layers::NON_MOVING: return b == Layers::MOVING;  // static collides only with moving
            case Layers::MOVING: return true;
            default: return false;
        }
    }
};

int g_refCount = 0;

void globalInit() {
    if (g_refCount++ == 0) {
        RegisterDefaultAllocator();
        Factory::sInstance = new Factory();
        RegisterTypes();
    }
}

void globalShutdown() {
    if (--g_refCount == 0) {
        UnregisterTypes();
        delete Factory::sInstance;
        Factory::sInstance = nullptr;
    }
}

EMotionType toMotionType(BodyMotion m) {
    switch (m) {
        case BodyMotion::Static: return EMotionType::Static;
        case BodyMotion::Kinematic: return EMotionType::Kinematic;
        default: return EMotionType::Dynamic;
    }
}

uint64 contactKey(BodyID a, BodyID b) {
    uint32 x = a.GetIndexAndSequenceNumber();
    uint32 y = b.GetIndexAndSequenceNumber();
    if (x > y) std::swap(x, y);
    return (static_cast<uint64>(x) << 32) | y;
}

} // namespace

// Records contact begin/end for BOTH solid collisions and sensor (trigger)
// overlaps, tagging each with whether a sensor was involved. Jolt calls these from
// worker threads, so all state is mutex-guarded and drained on the main thread.
class TriggerContactListener final : public JPH::ContactListener {
public:
    void OnContactAdded(const Body& b1, const Body& b2, const ContactManifold&,
                        ContactSettings&) override {
        bool sensor = b1.IsSensor() || b2.IsSensor();
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_.emplace(contactKey(b1.GetID(), b2.GetID()), sensor).second)
            events_.push_back({b1.GetID(), b2.GetID(), true, sensor});
    }

    void OnContactRemoved(const SubShapeIDPair& pair) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = active_.find(contactKey(pair.GetBody1ID(), pair.GetBody2ID()));
        if (it != active_.end()) {
            bool sensor = it->second;
            active_.erase(it);
            events_.push_back({pair.GetBody1ID(), pair.GetBody2ID(), false, sensor});
        }
    }

    std::vector<PhysicsWorld::ContactEvent> drain() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<PhysicsWorld::ContactEvent> out;
        out.swap(events_);
        return out;
    }

private:
    std::mutex mutex_;
    std::unordered_map<uint64, bool> active_;  // active pair -> involved a sensor
    std::vector<PhysicsWorld::ContactEvent> events_;
};

struct PhysicsWorld::LayerState {
    BPLayerInterfaceImpl broadphase;
    ObjectVsBroadPhaseLayerFilterImpl objectVsBroadphase;
    ObjectLayerPairFilterImpl objectVsObject;
};

PhysicsWorld::PhysicsWorld() {
    globalInit();

    layers_ = std::make_unique<LayerState>();
    tempAllocator_ = std::make_unique<TempAllocatorImpl>(16 * 1024 * 1024);

#ifdef __EMSCRIPTEN__
    // wasm sans pthreads : Jolt fournit un job system mono-thread équivalent.
    jobSystem_ = std::make_unique<JobSystemSingleThreaded>(cMaxPhysicsJobs);
#else
    unsigned hw = std::thread::hardware_concurrency();
    int workerThreads = std::max(1, static_cast<int>(hw) - 1);
    jobSystem_ = std::make_unique<JobSystemThreadPool>(cMaxPhysicsJobs, cMaxPhysicsBarriers, workerThreads);
#endif

    system_ = std::make_unique<PhysicsSystem>();
    const uint cMaxBodies = 8192;
    const uint cNumBodyMutexes = 0;  // 0 = autodetect
    const uint cMaxBodyPairs = 8192;
    const uint cMaxContactConstraints = 4096;
    system_->Init(cMaxBodies, cNumBodyMutexes, cMaxBodyPairs, cMaxContactConstraints,
                  layers_->broadphase, layers_->objectVsBroadphase, layers_->objectVsObject);
    system_->SetGravity(Vec3(0.0f, -9.81f, 0.0f));

    contactListener_ = std::make_unique<TriggerContactListener>();
    system_->SetContactListener(contactListener_.get());
}

PhysicsWorld::~PhysicsWorld() {
    system_.reset();
    contactListener_.reset();
    jobSystem_.reset();
    tempAllocator_.reset();
    layers_.reset();
    globalShutdown();
}

std::vector<PhysicsWorld::ContactEvent> PhysicsWorld::drainContactEvents() {
    return contactListener_->drain();
}

void* PhysicsWorld::bodyUserData(JPH::BodyID id) const {
    if (id.IsInvalid()) return nullptr;
    return reinterpret_cast<void*>(system_->GetBodyInterface().GetUserData(id));
}

void PhysicsWorld::step(float dt) {
    SAIDA_PROFILE_FUNCTION();
    if (dt <= 0.0f) return;

    const float fixed = 1.0f / 60.0f;
    accumulator_ += dt;
    if (accumulator_ > 0.25f) accumulator_ = 0.25f;  // avoid spiral of death after a hitch

    int steps = 0;
    while (accumulator_ >= fixed && steps < 8) {
        SAIDA_PROFILE_SCOPE("Physics/JoltUpdate");
        system_->Update(fixed, 1, tempAllocator_.get(), jobSystem_.get());
        accumulator_ -= fixed;
        ++steps;
    }
    SAIDA_PROFILE_COUNTER("Physics/FixedSteps", steps);
}

JPH::BodyID PhysicsWorld::createBody(const BodyDesc& d) {
    if (!d.shape) return BodyID();

    ObjectLayer layer = (d.motion == BodyMotion::Static) ? Layers::NON_MOVING : Layers::MOVING;
    BodyCreationSettings settings(d.shape, RVec3(d.position.x, d.position.y, d.position.z),
                                  toJolt(d.rotation), toMotionType(d.motion), layer);
    settings.mIsSensor = d.isSensor;
    settings.mFriction = d.friction;
    settings.mRestitution = d.restitution;
    settings.mUserData = reinterpret_cast<uint64>(d.userData);

    if (d.motion == BodyMotion::Dynamic) {
        settings.mLinearDamping = d.linearDamping;
        settings.mAngularDamping = d.angularDamping;
        settings.mGravityFactor = d.gravityFactor;
        if (d.mass > 0.0f) {
            settings.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
            settings.mMassPropertiesOverride.mMass = d.mass;
        }
    }

    BodyInterface& bi = system_->GetBodyInterface();
    Body* body = bi.CreateBody(settings);
    if (!body) {
        Log::warn("PhysicsWorld: body limit reached, cannot create body");
        return BodyID();
    }
    EActivation activation = (d.motion == BodyMotion::Static) ? EActivation::DontActivate
                                                              : EActivation::Activate;
    bi.AddBody(body->GetID(), activation);
    return body->GetID();
}

void PhysicsWorld::removeBody(JPH::BodyID id) {
    if (id.IsInvalid()) return;
    BodyInterface& bi = system_->GetBodyInterface();
    bi.RemoveBody(id);
    bi.DestroyBody(id);
}

void PhysicsWorld::setBodyTransform(JPH::BodyID id, const glm::vec3& position,
                                    const glm::quat& rotation, bool activate) {
    if (id.IsInvalid()) return;
    system_->GetBodyInterface().SetPositionAndRotation(
        id, RVec3(position.x, position.y, position.z), toJolt(rotation),
        activate ? EActivation::Activate : EActivation::DontActivate);
}

void PhysicsWorld::moveKinematic(JPH::BodyID id, const glm::vec3& position,
                                 const glm::quat& rotation, float dt) {
    if (id.IsInvalid() || dt <= 0.0f) return;
    system_->GetBodyInterface().MoveKinematic(
        id, RVec3(position.x, position.y, position.z), toJolt(rotation), dt);
}

void PhysicsWorld::setLinearVelocity(JPH::BodyID id, const glm::vec3& v) {
    if (id.IsInvalid()) return;
    BodyInterface& bi = system_->GetBodyInterface();
    bi.ActivateBody(id);
    bi.SetLinearVelocity(id, Vec3(v.x, v.y, v.z));
}

void PhysicsWorld::setAngularVelocity(JPH::BodyID id, const glm::vec3& v) {
    if (id.IsInvalid()) return;
    BodyInterface& bi = system_->GetBodyInterface();
    bi.ActivateBody(id);
    bi.SetAngularVelocity(id, Vec3(v.x, v.y, v.z));
}

void PhysicsWorld::getBodyTransform(JPH::BodyID id, glm::vec3& position,
                                    glm::quat& rotation) const {
    if (id.IsInvalid()) return;
    const BodyInterface& bi = system_->GetBodyInterface();
    RVec3 p;
    Quat q;
    bi.GetPositionAndRotation(id, p, q);
    position = glm::vec3(p.GetX(), p.GetY(), p.GetZ());
    rotation = toGlm(q);
}

JPH::Ref<JPH::CharacterVirtual> PhysicsWorld::createCharacter(
    const JPH::Shape* shape, const glm::vec3& position, const glm::quat& rotation,
    float mass, float maxSlopeAngleRad, void* userData) {
    if (!shape) return nullptr;

    Ref<CharacterVirtualSettings> settings = new CharacterVirtualSettings();
    settings->mShape = shape;
    settings->mMass = mass;
    settings->mMaxSlopeAngle = maxSlopeAngleRad;
    settings->mUp = Vec3::sAxisY();
    // Un CharacterVirtual n'existe pas dans la broadphase : sans inner body,
    // les capteurs (Area) et les raycasts ne voient jamais le personnage.
    // L'inner body kinématique suit le personnage et hérite de son userData
    // (le dispatch de triggers retrouve donc le CharacterBodyNode).
    settings->mInnerBodyShape = shape;
    settings->mInnerBodyLayer = Layers::MOVING;

    return new CharacterVirtual(settings, RVec3(position.x, position.y, position.z),
                                toJolt(rotation), reinterpret_cast<uint64>(userData),
                                system_.get());
}

void PhysicsWorld::updateCharacter(JPH::CharacterVirtual& character, float dt) {
    // ExtendedUpdate = move/slide + WalkStairs + StickToFloor with Jolt's defaults
    // (step up 0.4 m, stick down 0.5 m).
    CharacterVirtual::ExtendedUpdateSettings settings;
    character.ExtendedUpdate(dt, system_->GetGravity(), settings,
                             system_->GetDefaultBroadPhaseLayerFilter(Layers::MOVING),
                             system_->GetDefaultLayerFilter(Layers::MOVING),
                             {}, {}, *tempAllocator_);
}

RaycastHit PhysicsWorld::raycast(const glm::vec3& origin, const glm::vec3& direction,
                                 float maxDistance) const {
    RaycastHit out;
    RRayCast ray{RVec3(origin.x, origin.y, origin.z), toJolt(direction) * maxDistance};
    RayCastResult result;
    if (system_->GetNarrowPhaseQuery().CastRay(ray, result)) {
        out.hit = true;
        out.body = result.mBodyID;
        out.distance = result.mFraction * maxDistance;
        RVec3 hitPos = ray.GetPointOnRay(result.mFraction);
        out.point = glm::vec3(hitPos.GetX(), hitPos.GetY(), hitPos.GetZ());

        BodyLockRead lock(system_->GetBodyLockInterface(), result.mBodyID);
        if (lock.Succeeded()) {
            Vec3 n = lock.GetBody().GetWorldSpaceSurfaceNormal(result.mSubShapeID2, hitPos);
            out.normal = toGlm(n);
        }
    }
    return out;
}

} // namespace saida
