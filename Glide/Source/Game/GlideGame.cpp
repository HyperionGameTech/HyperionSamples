#include <Game/GlideGame.hpp>

#include <Core/Reflection/BoxedValue.hpp>

#include <Framework/EngineGlobals.hpp>
#include <Framework/CVarManager.hpp>

#include <Scene/World.hpp>
#include <Scene/Scene.hpp>
#include <Scene/Node.hpp>
#include <Scene/Entity.hpp>
#include <Scene/EntityManager.hpp>
#include <Scene/Camera/Camera.hpp>
#include <Scene/Camera/PerspectiveCamera.hpp>
#include <Scene/Components/MeshComponent.hpp>

#include <Input/Event.hpp>

#include <System/AppContext.hpp>

#include <UI/UISubsystem.hpp>
#include <UI/UIStage.hpp>

#include <Rendering/Mesh.hpp>
#include <Rendering/Material.hpp>
#include <Rendering/Util/MeshBuilder.hpp>

#include <Core/Math/Color.hpp>
#include <Core/Math/Quat4f.hpp>
#include <Core/Math/MathUtil.hpp>

#include <Core/Threading/Threads.hpp>
#include <Core/Threading/Scheduler.hpp>

#include <Core/Logging/Logger.hpp>

#include <Core/Reflection/ClassUtils.hpp>
#include <Core/Reflection/ClassRegistry.hpp>

#include <cmath>

namespace Hyperion {

ENGINE_API HYP_DECLARE_LOG_CHANNEL(Game);

namespace {

constexpr Pair<const char*, const char*> GlimmerCVarOverrides[] = {
    { "Rendering.Glimmer.Probes.PoolBlocks", "1024" },
    { "Rendering.Glimmer.Probes.NearField.Cascades", "4" }
};

constexpr float BaseSpeed = 32.0f;
constexpr float BoostSpeed = 20.0f;
constexpr float BoostResponse = 3.0f;
constexpr float RingKick = 5.0f;
constexpr float MeterPerRing = 0.25f;
constexpr float MeterDrain = 0.4f;

constexpr uint32 RingPoints = 100;
constexpr float RingRadius = 4.0f;
constexpr float MissDistance = 40.0f;
constexpr float RingCollectDuration = 0.3f;
constexpr float FinishRadius = 12.0f;

constexpr float SuperBoostSpeed = 45.0f;
constexpr float SuperBoostDuration = 3.0f;
constexpr float PickupRadius = 4.0f;
constexpr uint32 PickupEvery = 5;

constexpr float MaxYawRate = 1.25f;
constexpr float MaxPitch = 0.55f;
constexpr float BankAmount = 0.35f;
constexpr float InputResponse = 5.0f;
constexpr float YawResponse = 3.0f;
constexpr float PitchResponse = 2.5f;
constexpr float RollResponse = 3.0f;
constexpr float Ceiling = 78.0f;

// share of the screen's short side a finger drags for a full turn or climb
constexpr float TouchDragRange = 0.12f;

constexpr float WingSpan = 2.0f;
constexpr float RockMargin = 0.3f;
constexpr float RetryDelay = 0.6f;

constexpr float BaseFov = 70.0f;
constexpr float FovKick = 12.0f;
constexpr float FovRate = 4.0f;
constexpr float ShakeAmount = 0.1f;

constexpr float CameraDistance = 12.0f;
constexpr float CameraHeight = 3.5f;
constexpr float CameraLookAhead = 15.0f;
constexpr float CameraClearance = 1.5f;
constexpr float CameraBank = 0.35f;
constexpr float CameraPitchShare = 0.7f;
constexpr float CameraYawRate = 3.5f;
constexpr float CameraPitchRate = 2.5f;
constexpr float CameraRollRate = 2.0f;
constexpr float CameraDistanceRate = 1.5f;

constexpr uint32 NumWindStreaks = 48;

const Color RingFlashColor(1.0f, 0.85f, 0.4f, 1.0f);
const Color BoostFlashColor(0.3f, 0.85f, 1.0f, 1.0f);
const Color PointsColor(1.0f, 0.85f, 0.35f, 1.0f);
const Color StreakColor(1.0f, 0.6f, 0.95f, 1.0f);
const Color ChainBrokenColor(1.0f, 0.4f, 0.3f, 1.0f);
const Color SuperBoostColor(0.45f, 0.9f, 1.0f, 1.0f);

// moves value toward target at the same pace whatever the frame rate
float Ease(float value, float target, float rate, float delta)
{
    return value + (target - value) * (1.0f - std::exp(-rate * delta));
}

float WrapAngle(float angle)
{
    while (angle > MathUtil::pi<float>)
    {
        angle -= 2.0f * MathUtil::pi<float>;
    }

    while (angle < -MathUtil::pi<float>)
    {
        angle += 2.0f * MathUtil::pi<float>;
    }

    return angle;
}

Vec3f DirectionFromAngles(float yaw, float pitch)
{
    return Vec3f(std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch));
}

String SnapshotValueToString(const CVarSnapshotValue& value)
{
    if (value.Is<bool>())
    {
        return value.GetUnchecked<bool>() ? "true" : "false";
    }

    if (value.Is<int32>())
    {
        return HYP_FORMAT("{}", value.GetUnchecked<int32>());
    }

    if (value.Is<float>())
    {
        return HYP_FORMAT("{}", value.GetUnchecked<float>());
    }

    return String::empty;
}

Handle<Material> CreateUnlitMaterial(Name name, const Vec4f& color, bool additive)
{
    MaterialAttributes attributes;

    if (additive)
    {
        attributes.bucket = RenderBucket::Translucent;
        attributes.blendFunction = BlendFunction::Additive();
        attributes.flags = MaterialAttributeFlags::DepthTest | MaterialAttributeFlags::DisableShadowCasting;
    }
    else
    {
        attributes.flags = MaterialAttributeFlags::DepthWrite | MaterialAttributeFlags::DepthTest | MaterialAttributeFlags::DisableShadowCasting;
    }

    Handle<Material> material = MakeHandle<Material>(name, attributes);

    MaterialParameters parameters = material->GetParameters();
    parameters.albedo = color;
    parameters.unlit = true;

    material->SetParameters(parameters);
    material->SetIsDynamic(true);
    InitObject(material);

    return material;
}

Handle<Entity> CreateEffectEntity(Name name, Node* parent, const Handle<Mesh>& mesh, const Handle<Material>& material)
{
    Handle<Entity> entity = MakeHandle<Entity>(name);
    InitObject(entity);

    entity->AddComponent<MeshComponent>(MeshComponent { mesh, material });
    entity->SetLocalBounds(mesh->GetAABB());

    parent->AddChild(entity);
    entity->SetIsDynamic(true);

    return entity;
}

} // namespace

GlideGame::GlideGame()
    : Game()
{
}

GlideGame::~GlideGame()
{
}

#pragma region Startup and shutdown

void GlideGame::BeforeConnectingToServer()
{
    const char* hostAddress = NetGlobals::GetHostAddress();

    if (hostAddress != nullptr && *hostAddress != '\0')
    {
        Game::BeforeConnectingToServer();

        return;
    }

    // singleplayer: load the world straight away, next frame
    GetThreadById(g_simThread)->GetScheduler().Enqueue(
        [self = MakeStrongRef(this)]()
        {
            self->SyncContentAndLaunch();
        },
        TaskEnqueueFlags::FIRE_AND_FORGET);
}

void GlideGame::BeforeContentLoaded()
{
    Game::BeforeContentLoaded();
}

void GlideGame::AfterContentLoaded()
{
    Game::AfterContentLoaded();

    if (m_syncState.state == ContentSyncState::Failed)
    {
        HYP_LOG(Game, Error, "Could not load world '{}'", GetStartupWorldName());
    }
}

void GlideGame::OnLaunch()
{
    ApplyCVarOverrides();
    FindLevel();
    CreateChaseCamera();

    Game::OnLaunch();

    if (m_uiSubsystem.IsValid())
    {
        m_hud.Create(*m_uiSubsystem->GetUIStage());
        m_hud.SetInvertVertical(m_invertVertical);

#if defined(HYP_ANDROID) || defined(HYP_IOS)
        m_touchControls = true;
        m_hud.SetTouchControls();
#endif
    }

    if (!m_paperPlane.Create(m_plane.Get()))
    {
        HYP_LOG(Game, Warning, "The plane has no paper material to fold a new plane from, keeping the imported model");
    }

    CreateWindStreaks();
    //CreatePickups();
    ResetRun();

    HYP_LOG(Game, Info, "GlideGame launched: {} rings, plane {}", m_rings.Size(), m_plane.IsValid());

    StartSimulating();
}

void GlideGame::BeforeShutdown()
{
    ReleaseLevel();
    RestoreCVarOverrides();

    Game::BeforeShutdown();
}

void GlideGame::ApplyCVarOverrides()
{
    CVarManager& manager = CVarManager::GetInstance();

    for (const Pair<const char*, const char*>& entry : GlimmerCVarOverrides)
    {
        CVarBase* cvar = manager.FindVar(ANSIString(entry.first));

        if (cvar == nullptr)
        {
            continue;
        }

        const CVarSnapshot& snapshot = manager.GetCurrentSnapshot();

        if (cvar->id >= 0 && cvar->id < snapshot.numVars)
        {
            m_cvarsToRestore.PushBack({ String(entry.first), SnapshotValueToString(snapshot.values[cvar->id]) });
        }

        cvar->SetFromString(String(entry.second));
    }
}

// the editor runs the game in its own process, so put back what it had
void GlideGame::RestoreCVarOverrides()
{
    for (const Pair<String, String>& entry : m_cvarsToRestore)
    {
        CVarBase* cvar = CVarManager::GetInstance().FindVar(entry.first.ToAnsi());

        if (cvar != nullptr && entry.second.Any())
        {
            cvar->SetFromString(entry.second);
        }
    }

    m_cvarsToRestore.Clear();
}

void GlideGame::FindLevel()
{
    m_rings.Clear();
    m_collision.Clear();

    if (!m_world.IsValid())
    {
        return;
    }

    for (const Handle<Scene>& scene : m_world->GetScenes())
    {
        if (!scene.IsValid() || !scene->IsForegroundScene())
        {
            continue;
        }

        if (Node* plane = scene->FindNodeByName(NAME("Plane")))
        {
            m_plane = MakeStrongRef(plane);
        }

        if (Node* canyon = scene->FindNodeByName(NAME("Canyon")))
        {
            m_collision.SetCanyon(MakeStrongRef(canyon));
        }

        if (Node* finishGate = scene->FindNodeByName(NAME("FinishGate")))
        {
            m_finishGate = MakeStrongRef(finishGate);
        }

        if (Node* rings = scene->FindNodeByName(NAME("Rings")))
        {
            for (const Handle<Node>& ringNode : rings->GetChildren())
            {
                const Handle<Node>& meshNode = ringNode->GetChildren().Any() ? ringNode->GetChildren()[0] : ringNode;

                Ring ring;
                ring.node = ringNode;
                ring.center = meshNode->GetWorldTranslation();
                ring.normal = meshNode->GetWorldRotation().RotateVector(Vec3f::UnitZ()).Normalized();

                m_rings.PushBack(std::move(ring));
            }
        }
    }

    m_collision.FindRocks(m_world.Get());

    if (!m_plane.IsValid())
    {
        return;
    }

    m_startPosition = m_plane->GetWorldTranslation();

    // start off facing the nearest ring, however the plane was left turned in the editor
    Vec3f startForward = m_plane->GetWorldRotation().RotateVector(Vec3f::UnitZ());
    float nearestDistance = MathUtil::MaxSafeValue<float>();

    for (const Ring& ring : m_rings)
    {
        const float distance = (ring.center - m_startPosition).Length();

        if (distance < nearestDistance)
        {
            nearestDistance = distance;
            startForward = ring.center - m_startPosition;
        }
    }

    m_startYaw = std::atan2(startForward.x, startForward.z);
}

void GlideGame::ReleaseLevel()
{
    for (WindStreak& streak : m_windStreaks)
    {
        streak.entity->Remove();
    }

    for (BoostPickup& pickup : m_pickups)
    {
        pickup.entity->Remove();
    }

    m_windStreaks.Clear();
    m_pickups.Clear();

    m_paperPlane.Destroy();

    if (m_chaseCamera.IsValid())
    {
        m_chaseCamera->Remove();
        m_chaseCamera.Reset();
    }

    if (m_uiSubsystem.IsValid())
    {
        m_hud.Destroy(*m_uiSubsystem->GetUIStage());
    }

    m_rings.Clear();
    m_collision.Clear();
    m_plane.Reset();
    m_finishGate.Reset();
}

void GlideGame::CreateChaseCamera()
{
    if (!m_plane.IsValid() || m_plane->GetScene() == nullptr)
    {
        HYP_LOG(Game, Error, "No 'Plane' node in the level");

        return;
    }

    m_chaseCamera = MakeHandle<Camera>(BaseFov, 1280, 720, 0.25f, 4000.0f);
    m_chaseCamera->SetName(NAME("ChaseCamera"));
    m_chaseCamera->AddCameraController(MakeHandle<PerspectiveCameraController>());

    m_plane->GetScene()->GetRoot()->AddChild(m_chaseCamera);

    m_chaseCamera->SetIsDynamic(true);
    m_chaseCamera->AddTag<EntityTag::PrimaryCamera>();
}

void GlideGame::CreateWindStreaks()
{
    if (!m_plane.IsValid() || m_plane->GetScene() == nullptr)
    {
        return;
    }

    Handle<Mesh> mesh = MeshBuilder::Cube();
    InitObject(mesh);

    Handle<Material> material = CreateUnlitMaterial(NAME("WindStreak"), Vec4f(0.45f, 0.47f, 0.5f, 0.8f), true);
    Node* sceneRoot = m_plane->GetScene()->GetRoot().Get();

    for (uint32 index = 0; index < NumWindStreaks; index++)
    {
        WindStreak streak;
        streak.entity = CreateEffectEntity(NAME_FMT("WindStreak_{}", index), sceneRoot, mesh, material);

        m_windStreaks.PushBack(std::move(streak));
    }
}

void GlideGame::CreatePickups()
{
    if (!m_plane.IsValid() || m_plane->GetScene() == nullptr || m_rings.Size() < 2)
    {
        return;
    }

    Handle<Mesh> mesh = MeshBuilder::NormalizedCubeSphere(3);
    InitObject(mesh);

    Handle<Material> material = CreateUnlitMaterial(NAME("BoostPickup"), Vec4f(0.3f, 0.85f, 1.0f, 1.0f), false);
    Node* sceneRoot = m_plane->GetScene()->GetRoot().Get();

    // a pickup sits halfway between every few rings
    for (uint32 index = 2; index + 1 < m_rings.Size(); index += PickupEvery)
    {
        BoostPickup pickup;
        pickup.position = (m_rings[index].center + m_rings[index + 1].center) * 0.5f;
        pickup.entity = CreateEffectEntity(NAME_FMT("BoostPickup_{}", index), sceneRoot, mesh, material);

        m_pickups.PushBack(std::move(pickup));
    }
}

#pragma endregion Startup and shutdown

#pragma region Run flow

void GlideGame::ResetRun()
{
    m_state = RunState::Ready;
    m_stateTime = 0.0f;
    m_runTime = 0.0f;

    m_position = m_startPosition;
    m_yaw = m_startYaw;
    m_pitch = 0.0f;
    m_roll = 0.0f;
    m_yawRate = 0.0f;
    m_steer = 0.0f;
    m_climb = 0.0f;

    m_boost = 0.0f;
    m_boostMeter = 0.0f;
    m_boosting = false;
    m_superBoostTime = 0.0f;

    m_score = 0;
    m_chain = 0;
    m_ringsCollected = 0;
    m_nextRing = 0;

    for (Ring& ring : m_rings)
    {
        ring.collected = false;
        ring.missed = false;
        ring.collectTime = 0.0f;
        ring.node->SetLocalScale(Vec3f::One(), TransformChangeType::Simulation);
    }

    for (BoostPickup& pickup : m_pickups)
    {
        pickup.collected = false;
    }

    for (WindStreak& streak : m_windStreaks)
    {
        RespawnWindStreak(streak, 4.0f);
    }

    UpdatePlaneTransform();
    UpdateCamera(0.0f, true);
    UpdateEffects(0.0f);

    m_hud.SetFlightStats(GetFlightStats());
    m_hud.ShowTitleScreen();
}

void GlideGame::StartFlying()
{
    m_state = RunState::Flying;
    m_stateTime = 0.0f;

    m_hud.ShowFlightHud();
}

void GlideGame::Crash()
{
    m_state = RunState::Crashed;
    m_stateTime = 0.0f;
    m_boosting = false;

    HYP_LOG(Game, Info, "Crashed after {} s with {} rings, score {}", m_runTime, m_ringsCollected, m_score);

    m_hud.ShowCrashScreen(GetFlightStats());
}

void GlideGame::Finish()
{
    m_state = RunState::Finished;
    m_stateTime = 0.0f;
    m_boosting = false;

    HYP_LOG(Game, Info, "Finished in {} s with {} rings, score {}", m_runTime, m_ringsCollected, m_score);

    if (m_bestTime <= 0.0f || m_runTime < m_bestTime)
    {
        m_bestTime = m_runTime;
    }

    BestRun best;
    best.isNewBest = m_score > m_bestScore;

    if (best.isNewBest)
    {
        m_bestScore = m_score;
    }

    best.score = m_bestScore;
    best.time = m_bestTime;

    m_hud.ShowFinishScreen(GetFlightStats(), best);
}

void GlideGame::OnUpdate(float delta)
{
    Game::OnUpdate(delta);

    if (!IsLaunched() || !m_plane.IsValid())
    {
        return;
    }

    if (!m_collision.HasRocks())
    {
        m_collision.FindRocks(m_world.Get());
    }

    delta = MathUtil::Min(delta, 0.1f);
    m_stateTime += delta;

    switch (m_state)
    {
    case RunState::Ready:
        // bob gently while waiting to launch
        m_position = m_startPosition + Vec3f(0.0f, 0.25f * std::sin(m_stateTime * 1.5f), 0.0f);
        UpdatePlaneTransform();
        UpdateCamera(delta, false);
        break;

    case RunState::Flying:
        m_runTime += delta;
        UpdateFlight(delta);
        UpdateCamera(delta, false);
        m_hud.SetFlightStats(GetFlightStats());
        break;

    case RunState::Crashed:
    case RunState::Finished:
        m_boost = Ease(m_boost, 0.0f, BoostResponse, delta);
        UpdateRings(m_position, delta);
        UpdateCamera(delta, false);
        break;
    }

    UpdateEffects(delta);
}

bool GlideGame::OnInputEvent(const Event& event)
{
    if (event.GetType() == EventType::TOUCH_DOWN || event.GetType() == EventType::TOUCH_MOVE || event.GetType() == EventType::TOUCH_UP)
    {
        HandleTouch(event);

        return Game::OnInputEvent(event);
    }

    const bool isKeyEvent = event.GetType() == EventType::KEYDOWN || event.GetType() == EventType::KEYUP;

    if (!isKeyEvent)
    {
        return Game::OnInputEvent(event);
    }

    const uint32 keyIndex = uint32(event.GetKeyCode());

    if (keyIndex >= 256)
    {
        return Game::OnInputEvent(event);
    }

    const bool isDown = event.GetType() == EventType::KEYDOWN;
    const bool justPressed = isDown && !m_keysDown[keyIndex];

    m_keysDown[keyIndex] = isDown;

    if (justPressed)
    {
        switch (event.GetKeyCode())
        {
        // case KeyCode::KEY_P:
        //     ToggleAutopilot();
        //     break;

        case KeyCode::KEY_I:
            ToggleInvertVertical();
            break;

        case KeyCode::KEY_SPACE:
            PressStart();
            break;

        default:
            break;
        }
    }

    return Game::OnInputEvent(event);
}

void GlideGame::PressStart()
{
    if (m_state == RunState::Ready)
    {
        StartFlying();
    }
    else if (m_state != RunState::Flying && m_stateTime > RetryDelay)
    {
        ResetRun();
    }
}

void GlideGame::HandleTouch(const Event& event)
{
    const TouchEvent touch = event.ToTouchEvent();

    if (!m_touchControls)
    {
        m_touchControls = true;
        m_hud.SetTouchControls();
    }

    switch (event.GetType())
    {
    case EventType::TOUCH_DOWN:
        if (m_steerPointer < 0)
        {
            m_steerPointer = touch.pointerId;
            m_steerAnchor = touch.position;
            m_steerDrag = Vec2f::Zero();

            PressStart();
        }
        else if (m_boostPointer < 0)
        {
            m_boostPointer = touch.pointerId;
        }
        break;

    case EventType::TOUCH_MOVE:
        if (touch.pointerId == m_steerPointer && event.GetWindow() != nullptr)
        {
            const Vec2i size = event.GetWindow()->GetSize();
            const float range = float(MathUtil::Max(MathUtil::Min(size.x, size.y), 1)) * TouchDragRange;

            m_steerDrag = (touch.position - m_steerAnchor) / range;
        }
        break;

    case EventType::TOUCH_UP:
        if (touch.pointerId == m_steerPointer)
        {
            m_steerPointer = -1;
            m_steerDrag = Vec2f::Zero();
        }
        else if (touch.pointerId == m_boostPointer)
        {
            m_boostPointer = -1;
        }
        break;

    default:
        break;
    }
}

void GlideGame::ToggleAutopilot()
{
    m_autopilot = !m_autopilot;

    m_hud.SetFlightStats(GetFlightStats());
}

void GlideGame::ToggleInvertVertical()
{
    m_invertVertical = !m_invertVertical;

    m_hud.SetInvertVertical(m_invertVertical);
}

#pragma endregion Run flow

#pragma region Flight

void GlideGame::UpdateFlight(float delta)
{
    float steer = 0.0f;
    float climb = 0.0f;

    if (m_autopilot)
    {
        GetAutopilotInput(steer, climb);
    }
    else
    {
        ReadInput(steer, climb);
    }

    // keys are all or nothing, so turns and climbs ease in and out
    m_steer = Ease(m_steer, steer, InputResponse, delta);
    m_climb = Ease(m_climb, climb, InputResponse, delta);

    m_yawRate = Ease(m_yawRate, m_steer * MaxYawRate, YawResponse, delta);
    m_yaw += m_yawRate * delta;

    float targetPitch = m_climb * MaxPitch;

    if (m_position.y > Ceiling)
    {
        targetPitch = MathUtil::Min(targetPitch, -0.2f);
    }

    m_pitch = Ease(m_pitch, targetPitch, PitchResponse, delta);
    m_roll = Ease(m_roll, m_yawRate * BankAmount, RollResponse, delta);

    const bool boostHeld = IsKeyDown(KeyCode::KEY_LSHIFT) || IsKeyDown(KeyCode::KEY_RSHIFT) || m_boostPointer >= 0;

    m_superBoostTime = MathUtil::Max(m_superBoostTime - delta, 0.0f);
    m_boosting = boostHeld && m_boostMeter > 0.0f && m_superBoostTime <= 0.0f;

    if (m_boosting)
    {
        m_boostMeter = MathUtil::Max(m_boostMeter - MeterDrain * delta, 0.0f);
    }

    float targetBoost = 0.0f;

    if (m_superBoostTime > 0.0f)
    {
        targetBoost = SuperBoostSpeed;
    }
    else if (m_boosting)
    {
        targetBoost = BoostSpeed;
    }

    m_boost = Ease(m_boost, targetBoost, BoostResponse, delta);

    const Vec3f previousPosition = m_position;

    m_position += GetForward() * ((BaseSpeed + m_boost) * delta);

    if (HitsCanyon(previousPosition))
    {
        m_position = previousPosition;
        UpdatePlaneTransform();
        Crash();

        return;
    }

    UpdatePlaneTransform();
    UpdateRings(previousPosition, delta);
    CollectPickups(previousPosition);

    if (m_finishGate.IsValid())
    {
        const Vec3f gateCenter = m_finishGate->GetWorldTranslation();
        const Vec3f gateNormal = m_finishGate->GetWorldRotation().RotateVector(Vec3f::UnitZ()).Normalized();

        if (PassedThrough(previousPosition, gateCenter, gateNormal, FinishRadius))
        {
            Finish();
        }
    }
}

void GlideGame::ReadInput(float& outSteer, float& outClimb) const
{
    const bool left = IsKeyDown(KeyCode::KEY_LEFT) || IsKeyDown(KeyCode::KEY_A);
    const bool right = IsKeyDown(KeyCode::KEY_RIGHT) || IsKeyDown(KeyCode::KEY_D);
    const bool up = IsKeyDown(KeyCode::KEY_UP) || IsKeyDown(KeyCode::KEY_W);
    const bool down = IsKeyDown(KeyCode::KEY_DOWN) || IsKeyDown(KeyCode::KEY_S);

    outSteer = float(right) - float(left);
    outClimb = float(up) - float(down);

    // screen y points down, so dragging up climbs like W
    outSteer = MathUtil::Clamp(outSteer + m_steerDrag.x, -1.0f, 1.0f);
    outClimb = MathUtil::Clamp(outClimb - m_steerDrag.y, -1.0f, 1.0f);

    if (m_invertVertical)
    {
        outClimb = -outClimb;
    }
}

void GlideGame::GetAutopilotInput(float& outSteer, float& outClimb)
{
    outSteer = 0.0f;
    outClimb = 0.0f;

    while (m_nextRing < m_rings.Size() && (m_position - m_rings[m_nextRing].center).Dot(m_rings[m_nextRing].normal) > 0.0f)
    {
        m_nextRing++;
    }

    Vec3f target;

    if (m_nextRing < m_rings.Size())
    {
        target = m_rings[m_nextRing].center;
    }
    else if (m_finishGate.IsValid())
    {
        target = m_finishGate->GetWorldTranslation();
    }
    else
    {
        return;
    }

    const Vec3f toTarget = (target - m_position).Normalized();
    const float yawError = WrapAngle(std::atan2(toTarget.x, toTarget.z) - m_yaw);
    const float desiredPitch = std::asin(MathUtil::Clamp(toTarget.y, -1.0f, 1.0f));

    // the yaw rate damps the turn so the plane settles on the ring instead of weaving
    outSteer = MathUtil::Clamp(yawError * 2.5f - m_yawRate * 0.5f, -1.0f, 1.0f);
    outClimb = MathUtil::Clamp(desiredPitch / MaxPitch, -1.0f, 1.0f);

    constexpr float ProbeLength = 85.0f;
    constexpr float ProbeAngle = 0.6f;

    const auto clearDistance = [this](float yawOffset, float pitchOffset)
    {
        const Vec3f direction = DirectionFromAngles(m_yaw + yawOffset, m_pitch + pitchOffset);

        return m_collision.GetClearDistance(m_position, direction, ProbeLength);
    };

    // the straight line to a ring can cut through a bend, so swerve toward the clearer side
    if (clearDistance(0.0f, 0.0f) < ProbeLength)
    {
        outSteer = clearDistance(ProbeAngle, 0.0f) > clearDistance(-ProbeAngle, 0.0f) ? 1.0f : -1.0f;
    }

    if (clearDistance(0.0f, -0.35f) < 26.0f)
    {
        outClimb = 1.0f;
    }
}

bool GlideGame::HitsCanyon(const Vec3f& previousPosition) const
{
    const Vec3f wingOffset = GetRight() * WingSpan;
    const Vec3f nose = m_position + GetForward();

    const bool noseHit = m_collision.HitsSegment(previousPosition, nose) || m_collision.HitsRock(nose, RockMargin);
    const bool leftWingHit = m_collision.HitsSegment(previousPosition - wingOffset, m_position - wingOffset) || m_collision.HitsRock(m_position - wingOffset, RockMargin);
    const bool rightWingHit = m_collision.HitsSegment(previousPosition + wingOffset, m_position + wingOffset) || m_collision.HitsRock(m_position + wingOffset, RockMargin);

    return noseHit || leftWingHit || rightWingHit;
}

void GlideGame::UpdatePlaneTransform()
{
    m_plane->SetWorldTranslation(m_position, TransformChangeType::Simulation);
    m_plane->SetWorldRotation(GetPlaneRotation(), TransformChangeType::Simulation);
}

Quat4f GlideGame::GetPlaneRotation() const
{
    const Quat4f yaw = Quat4f::AxisAngles(Vec3f::UnitY(), m_yaw);
    const Quat4f pitch = Quat4f::AxisAngles(Vec3f::UnitX(), -m_pitch);
    const Quat4f roll = Quat4f::AxisAngles(Vec3f::UnitZ(), -m_roll);

    return yaw * pitch * roll;
}

Vec3f GlideGame::GetForward() const
{
    return DirectionFromAngles(m_yaw, m_pitch);
}

Vec3f GlideGame::GetRight() const
{
    return Vec3f(std::cos(m_yaw), 0.0f, -std::sin(m_yaw));
}

float GlideGame::GetBoostAmount() const
{
    return MathUtil::Clamp(m_boost / BoostSpeed, 0.0f, 2.0f);
}

#pragma endregion Flight

#pragma region Rings and pickups

void GlideGame::UpdateRings(const Vec3f& previousPosition, float delta)
{
    for (Ring& ring : m_rings)
    {
        if (ring.collected)
        {
            AnimateCollectedRing(ring, delta);

            continue;
        }

        Vec3f crossing;

        if (m_state != RunState::Flying || !CrossesPlane(previousPosition, ring.center, ring.normal, crossing))
        {
            continue;
        }

        const float distance = (crossing - ring.center).Length();

        if (distance <= RingRadius)
        {
            CollectRing(ring);
        }
        else if (distance <= MissDistance && !ring.missed)
        {
            MissRing(ring);
        }
    }
}

void GlideGame::CollectRing(Ring& ring)
{
    ring.collected = true;
    ring.collectTime = 0.0f;

    m_ringsCollected++;
    m_chain++;

    const uint32 points = RingPoints * m_chain;
    m_score += points;

    m_boost += RingKick;
    m_boostMeter = MathUtil::Min(m_boostMeter + MeterPerRing, 1.0f);

    m_hud.Flash(RingFlashColor);

    if (m_chain % 3 == 0)
    {
        const String streakMessage = m_streakMessage % 2 == 0 ? "You're unstoppable!" : "HOLY CRAP!";
        m_streakMessage++;

        m_hud.ShowPopup(streakMessage, StreakColor);
    }
    else if (m_chain > 1)
    {
        m_hud.ShowPopup(HYP_FORMAT("+{}   x{}", points, m_chain), PointsColor);
    }
    else
    {
        m_hud.ShowPopup(HYP_FORMAT("+{}", points), PointsColor);
    }
}

void GlideGame::MissRing(Ring& ring)
{
    ring.missed = true;

    m_chain = 0;
}

void GlideGame::AnimateCollectedRing(Ring& ring, float delta)
{
    if (ring.collectTime >= RingCollectDuration)
    {
        return;
    }

    ring.collectTime += delta;

    // swell, then vanish
    const float progress = MathUtil::Min(ring.collectTime / RingCollectDuration, 1.0f);
    const float scale = progress < 1.0f ? 1.0f + progress * 1.5f : 0.001f;

    ring.node->SetLocalScale(Vec3f(scale), TransformChangeType::Simulation);
}

void GlideGame::CollectPickups(const Vec3f& previousPosition)
{
    if (m_autopilot)
    {
        return;
    }

    const Vec3f path = m_position - previousPosition;
    const float pathLengthSquared = MathUtil::Max(path.Dot(path), 0.0001f);

#if 0
    for (BoostPickup& pickup : m_pickups)
    {
        if (pickup.collected)
        {
            continue;
        }

        const float alongPath = MathUtil::Clamp((pickup.position - previousPosition).Dot(path) / pathLengthSquared, 0.0f, 1.0f);
        const Vec3f closestPoint = previousPosition + path * alongPath;

        if ((pickup.position - closestPoint).Length() > PickupRadius)
        {
            continue;
        }

        pickup.collected = true;
        m_superBoostTime = SuperBoostDuration;

        m_hud.Flash(BoostFlashColor);
        m_hud.ShowPopup("SUPER BOOST!", SuperBoostColor);
    }
#endif
}

bool GlideGame::CrossesPlane(const Vec3f& previousPosition, const Vec3f& center, const Vec3f& normal, Vec3f& outCrossing) const
{
    const float previousSide = (previousPosition - center).Dot(normal);
    const float currentSide = (m_position - center).Dot(normal);

    if ((previousSide > 0.0f) == (currentSide > 0.0f))
    {
        return false;
    }

    const float t = previousSide / (previousSide - currentSide);
    outCrossing = previousPosition + (m_position - previousPosition) * t;

    return true;
}

bool GlideGame::PassedThrough(const Vec3f& previousPosition, const Vec3f& center, const Vec3f& normal, float radius) const
{
    Vec3f crossing;

    return CrossesPlane(previousPosition, center, normal, crossing) && (crossing - center).Length() <= radius;
}

FlightStats GlideGame::GetFlightStats() const
{
    FlightStats stats;
    stats.score = m_score;
    stats.chain = m_chain;
    stats.ringsCollected = m_ringsCollected;
    stats.ringCount = uint32(m_rings.Size());
    stats.time = m_runTime;
    stats.speed = int(BaseSpeed + m_boost);
    stats.boostMeter = m_boostMeter;
    stats.boosting = m_boosting;
    stats.autopilot = m_autopilot;

    return stats;
}

#pragma endregion Rings and pickups

#pragma region Camera and effects

void GlideGame::UpdateCamera(float delta, bool snap)
{
    if (!m_chaseCamera.IsValid())
    {
        return;
    }

    const auto ease = [delta, snap](float current, float target, float rate)
    {
        return snap ? target : Ease(current, target, rate, delta);
    };

    m_cameraYaw = ease(m_cameraYaw, m_yaw, CameraYawRate);
    m_cameraPitch = ease(m_cameraPitch, m_pitch * CameraPitchShare, CameraPitchRate);
    m_cameraRoll = ease(m_cameraRoll, m_roll * CameraBank, CameraRollRate);

    const Quat4f view = Quat4f::AxisAngles(Vec3f::UnitY(), m_cameraYaw)
        * Quat4f::AxisAngles(Vec3f::UnitX(), -m_cameraPitch)
        * Quat4f::AxisAngles(Vec3f::UnitZ(), -m_cameraRoll);

    const Vec3f viewForward = view.RotateVector(Vec3f::UnitZ());
    const Vec3f viewRight = view.RotateVector(Vec3f::UnitX());
    const Vec3f viewUp = view.RotateVector(Vec3f::UnitY());

    const Vec3f offset = viewUp * CameraHeight - viewForward * CameraDistance;
    const float fullDistance = offset.Length();
    const Vec3f toCamera = offset / fullDistance;

    // pull in at once when rock comes between the plane and the camera, then ease back out
    const float clearDistance = m_collision.GetClearDistance(m_position, toCamera, fullDistance + CameraClearance) - CameraClearance;
    const float targetDistance = MathUtil::Clamp(clearDistance, 0.5f, fullDistance);

    if (snap || targetDistance < m_cameraDistance)
    {
        m_cameraDistance = targetDistance;
    }
    else
    {
        m_cameraDistance = ease(m_cameraDistance, targetDistance, CameraDistanceRate);
    }

    const float boostAmount = GetBoostAmount();
    const float targetFov = BaseFov + FovKick * boostAmount;
    const float fov = ease(m_cameraFov, targetFov, FovRate);

    if (snap || MathUtil::Abs(fov - m_cameraFov) > 0.01f)
    {
        m_cameraFov = fov;
        m_chaseCamera->SetToPerspectiveProjection(m_cameraFov, 0.25f, 4000.0f);
    }

    m_shakeTime += delta;

    const float shakeX = std::sin(m_shakeTime * 23.0f) + 0.5f * std::sin(m_shakeTime * 37.3f);
    const float shakeY = std::sin(m_shakeTime * 29.7f) + 0.5f * std::sin(m_shakeTime * 41.1f);
    const Vec3f shake = (viewRight * shakeX + viewUp * shakeY) * (ShakeAmount * boostAmount);

    const Vec3f cameraPosition = m_position + toCamera * m_cameraDistance + shake;
    const Vec3f lookAt = m_position + viewForward * CameraLookAhead + shake * 0.5f;

    m_chaseCamera->SetWorldTranslation(cameraPosition, TransformChangeType::Simulation);
    m_chaseCamera->SetDirection((lookAt - cameraPosition).Normalized());
    m_chaseCamera->SetUpVector(viewUp);
}

void GlideGame::UpdateEffects(float delta)
{
    const bool flying = m_state == RunState::Flying;
    const bool promptReady = m_state == RunState::Ready || m_stateTime > RetryDelay;

    m_hud.Update(delta, promptReady);

    float wind = 0.35f;

    if (flying)
    {
        wind = 1.0f + GetBoostAmount();
    }
    else if (m_state == RunState::Crashed)
    {
        wind = 0.1f;
    }

    m_paperPlane.Update(delta, wind, flying ? m_steer : 0.0f, flying ? m_climb : 0.0f);

    UpdateWindStreaks();
    UpdatePickups();
}

void GlideGame::UpdateWindStreaks()
{
    if (m_windStreaks.Empty())
    {
        return;
    }

    const Vec3f forward = GetForward();
    const float boostAmount = GetBoostAmount();

    const uint32 numVisible = m_state == RunState::Flying ? uint32(10.0f + 38.0f * MathUtil::Min(boostAmount, 1.0f)) : 0;
    const Vec3f visibleScale(0.02f, 0.02f, 0.6f + 2.0f * boostAmount);
    const Vec3f hiddenScale(0.0001f);
    const Quat4f rotation = Quat4f::LookAt(forward, Vec3f::UnitY()).Inverse();

    for (uint32 index = 0; index < m_windStreaks.Size(); index++)
    {
        WindStreak& streak = m_windStreaks[index];
        const Vec3f toStreak = streak.position - m_position;

        if (toStreak.Dot(forward) < -4.0f || toStreak.Length() > 90.0f)
        {
            RespawnWindStreak(streak, 30.0f);
        }

        streak.entity->SetWorldTranslation(streak.position, TransformChangeType::Simulation);
        streak.entity->SetWorldRotation(rotation, TransformChangeType::Simulation);
        streak.entity->SetWorldScale(index < numVisible ? visibleScale : hiddenScale, TransformChangeType::Simulation);
    }
}

void GlideGame::RespawnWindStreak(WindStreak& streak, float minAhead)
{
    const Vec3f forward = GetForward();
    const Vec3f right = GetRight();
    const Vec3f up = forward.Cross(right).Normalized();

    const float angle = Random() * 2.0f * MathUtil::pi<float>;
    const float radius = 2.5f + Random() * 7.0f;
    const float ahead = minAhead + Random() * 60.0f;

    streak.position = m_position + forward * ahead + (right * std::cos(angle) + up * std::sin(angle)) * radius;
}

void GlideGame::UpdatePickups()
{
    const float pulse = 1.0f + 0.15f * std::sin(m_stateTime * 6.0f);
    const Vec3f bob(0.0f, 0.4f * std::sin(m_stateTime * 2.0f), 0.0f);

    for (BoostPickup& pickup : m_pickups)
    {
        const Vec3f scale = pickup.collected ? Vec3f(0.0001f) : Vec3f(1.1f * pulse);

        pickup.entity->SetWorldTranslation(pickup.position + bob, TransformChangeType::Simulation);
        pickup.entity->SetWorldScale(scale, TransformChangeType::Simulation);
    }
}

#pragma endregion Camera and effects

bool GlideGame::IsKeyDown(KeyCode key) const
{
    const uint32 index = uint32(key);

    return index < 256 && m_keysDown[index];
}

float GlideGame::Random()
{
    m_random = m_random * 1664525u + 1013904223u;

    return float(m_random >> 8) / float(1u << 24);
}

#pragma region Reflection

const Class* g_clsGlideGame = nullptr;

const Class* GlideGame::StaticClass()
{
    return g_clsGlideGame;
}

// clang-format off

HYP_BEGIN_CLASS(GlideGame, -1, 0, NAME("Game"))
HYP_END_CLASS

// clang-format on

HYP_REGISTER_STATIC_CLASS(GlideGame);

#pragma endregion Reflection

} // namespace Hyperion

extern "C"
{

HYP_EXPORT const char* HypGameModule_GetGameClassName()
{
    return "GlideGame";
}

HYP_EXPORT void HypGameModule_GetEngineVersion(Hyperion::uint32* outMajor, Hyperion::uint32* outMinor, Hyperion::uint32* outPatch)
{
    *outMajor = HYP_VERSION_MAJOR;
    *outMinor = HYP_VERSION_MINOR;
    *outPatch = HYP_VERSION_PATCH;
}

}
