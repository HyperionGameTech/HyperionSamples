#pragma once

#include <Core/Reflection/Handle.hpp>
#include <Core/Containers/Array.hpp>
#include <Core/Containers/String.hpp>
#include <Core/Math/Color.hpp>
#include <Core/Name/Name.hpp>

namespace Hyperion {

class UIStage;
class UIPanel;
class UIText;

struct FlightStats
{
    uint32 score = 0;
    uint32 chain = 0;
    uint32 ringsCollected = 0;
    uint32 ringCount = 0;
    float time = 0.0f;
    int speed = 0;
    float boostMeter = 0.0f;
    bool boosting = false;
    bool autopilot = false;
};

struct BestRun
{
    uint32 score = 0;
    float time = 0.0f;
    bool isNewBest = false;
};

class GlideHud
{
public:
    void Create(UIStage& stage);
    void Destroy(UIStage& stage);

    void ShowTitleScreen();
    void ShowFlightHud();
    void ShowCrashScreen(const FlightStats& stats);
    void ShowFinishScreen(const FlightStats& stats, const BestRun& best);

    void SetFlightStats(const FlightStats& stats);
    void SetInvertVertical(bool invertVertical);
    void SetTouchControls();

    void ShowPopup(const String& text, const Color& color);
    void Flash(const Color& tint);

    void Update(float delta, bool promptReady);

private:
    void CreateFlightHud();
    void CreatePopup();
    void CreateTitleScreen();
    void CreateCrashScreen();
    void CreateFinishScreen();

    Handle<UIPanel> CreateScreen(Name name, int height, const Color& accent);
    void CreateTitle(const Handle<UIPanel>& screen, Name name, int y, float size, const Color& color, const String& text);
    void CreatePrompt(const Handle<UIPanel>& screen, Name name, int y, const String& text);

    void ShowOnly(const Handle<UIPanel>& panel);

    Handle<UIPanel> m_root;

    Handle<UIPanel> m_flash;
    Color m_flashTint;
    float m_flashAmount = 0.0f;

    Handle<UIPanel> m_flightHud;
    Handle<UIText> m_scoreText;
    Handle<UIPanel> m_chainBadge;
    Handle<UIText> m_chainText;
    Handle<UIPanel> m_autopilotBadge;
    Handle<UIText> m_ringsText;
    Handle<UIPanel> m_ringProgress;
    Handle<UIText> m_timeText;
    Handle<UIText> m_speedText;
    Handle<UIPanel> m_boostFill;

    Handle<UIPanel> m_popup;
    Handle<UIText> m_popupText;
    Handle<UIText> m_popupShadow;
    float m_popupTime = 0.0f;

    Handle<UIPanel> m_titleScreen;
    Array<Handle<UIText>> m_controlKeys;
    Handle<UIText> m_invertText;

    Handle<UIPanel> m_crashScreen;
    Handle<UIText> m_crashStats;

    Handle<UIPanel> m_finishScreen;
    Handle<UIText> m_finishScore;
    Handle<UIText> m_finishRings;
    Handle<UIText> m_finishTime;
    Handle<UIText> m_finishBest;

    Array<Handle<UIText>> m_prompts;
    float m_promptTime = 0.0f;
};

} // namespace Hyperion
