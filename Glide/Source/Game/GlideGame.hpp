#pragma once

#include <Framework/Game.hpp>

#include <Input/Keyboard.hpp>

#include <Core/Math/Vector3.hpp>
#include <Core/Math/Quat4f.hpp>

#include <Game/GlideHud.hpp>
#include <Game/PaperPlane.hpp>
#include <Game/CanyonCollision.hpp>

namespace Hyperion {

class Node;
class Camera;
class Entity;

class GlideGame : public Game
{
    HYP_OBJECT_BODY(GlideGame);

public:
    GlideGame();
    virtual ~GlideGame() override;

protected:
    virtual void BeforeConnectingToServer() override;

    virtual void BeforeContentLoaded() override;
    virtual void AfterContentLoaded() override;

    virtual void OnLaunch() override;
    virtual void OnUpdate(float delta) override;
    virtual bool OnInputEvent(const Event& event) override;

    virtual void BeforeShutdown() override;

private:
    enum class RunState : uint8
    {
        Ready,
        Flying,
        Crashed,
        Finished
    };

    struct Ring
    {
        Handle<Node> node;
        Vec3f center;
        Vec3f normal;
        bool collected = false;
        bool missed = false;
        float collectTime = 0.0f;
    };

    struct BoostPickup
    {
        Handle<Entity> entity;
        Vec3f position;
        bool collected = false;
    };

    struct WindStreak
    {
        Handle<Entity> entity;
        Vec3f position;
    };

    void ApplyCVarOverrides();
    void RestoreCVarOverrides();

    void FindLevel();
    void ReleaseLevel();

    void CreateChaseCamera();
    void CreateWindStreaks();
    void CreatePickups();

    void ResetRun();
    void StartFlying();
    void Crash();
    void Finish();

    void UpdateFlight(float delta);
    void ReadInput(float& outSteer, float& outClimb) const;
    void GetAutopilotInput(float& outSteer, float& outClimb);
    bool HitsCanyon(const Vec3f& previousPosition) const;

    void UpdateRings(const Vec3f& previousPosition, float delta);
    void CollectRing(Ring& ring);
    void MissRing(Ring& ring);
    void AnimateCollectedRing(Ring& ring, float delta);
    void CollectPickups(const Vec3f& previousPosition);

    void UpdatePlaneTransform();
    void UpdateCamera(float delta, bool snap);
    void UpdateEffects(float delta);
    void UpdateWindStreaks();
    void RespawnWindStreak(WindStreak& streak, float minAhead);
    void UpdatePickups();

    void ToggleAutopilot();
    void ToggleInvertVertical();

    FlightStats GetFlightStats() const;
    Vec3f GetForward() const;
    Vec3f GetRight() const;
    Quat4f GetPlaneRotation() const;
    float GetBoostAmount() const;

    bool CrossesPlane(const Vec3f& previousPosition, const Vec3f& center, const Vec3f& normal, Vec3f& outCrossing) const;
    bool PassedThrough(const Vec3f& previousPosition, const Vec3f& center, const Vec3f& normal, float radius) const;

    bool IsKeyDown(KeyCode key) const;
    float Random();

    Handle<Camera> m_chaseCamera;
    Handle<Node> m_plane;
    Handle<Node> m_finishGate;
    Array<Ring> m_rings;

    GlideHud m_hud;
    PaperPlane m_paperPlane;
    CanyonCollision m_collision;

    Array<WindStreak> m_windStreaks;
    Array<BoostPickup> m_pickups;

    RunState m_state = RunState::Ready;
    float m_stateTime = 0.0f;
    float m_runTime = 0.0f;

    Vec3f m_startPosition;
    float m_startYaw = 0.0f;

    Vec3f m_position;
    float m_yaw = 0.0f;
    float m_pitch = 0.0f;
    float m_roll = 0.0f;
    float m_yawRate = 0.0f;
    float m_steer = 0.0f;
    float m_climb = 0.0f;

    float m_boost = 0.0f;
    float m_boostMeter = 0.0f;
    bool m_boosting = false;
    float m_superBoostTime = 0.0f;

    float m_cameraYaw = 0.0f;
    float m_cameraPitch = 0.0f;
    float m_cameraRoll = 0.0f;
    float m_cameraDistance = 0.0f;
    float m_cameraFov = 0.0f;
    float m_shakeTime = 0.0f;

    uint32 m_score = 0;
    uint32 m_chain = 0;
    uint32 m_streakMessage = 0;
    uint32 m_ringsCollected = 0;
    uint32 m_nextRing = 0;

    uint32 m_bestScore = 0;
    float m_bestTime = 0.0f;

    bool m_autopilot = false;
    bool m_invertVertical = false;

    bool m_keysDown[256] = {};
    uint32 m_random = 0x9e3779b9u;

    Array<Pair<String, String>> m_cvarsToRestore;
};

} // namespace Hyperion
