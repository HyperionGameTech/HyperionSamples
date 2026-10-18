#include <Game/GlideHud.hpp>

#include <UI/UIStage.hpp>
#include <UI/UIPanel.hpp>
#include <UI/UIText.hpp>

#include <Core/Math/MathUtil.hpp>
#include <Core/Utilities/Format.hpp>

#include <cmath>

namespace Hyperion {

namespace {

using Align = UIObjectAlignment;

namespace Colors {

const Color Gold(1.0f, 0.78f, 0.3f, 1.0f);
const Color Sand(1.0f, 0.95f, 0.88f, 1.0f);
const Color Muted(1.0f, 0.88f, 0.72f, 0.75f);
const Color Cyan(0.35f, 0.85f, 1.0f, 1.0f);
const Color BrightCyan(0.75f, 0.95f, 1.0f, 1.0f);
const Color Red(1.0f, 0.45f, 0.32f, 1.0f);
const Color Ink(0.12f, 0.07f, 0.04f, 1.0f);
const Color Shadow(0.0f, 0.0f, 0.0f, 0.55f);

const Color Card(0.07f, 0.045f, 0.035f, 0.72f);
const Color Screen(0.05f, 0.03f, 0.025f, 0.88f);
const Color Track(0.0f, 0.0f, 0.0f, 0.5f);
const Color Divider(0.07f, 0.045f, 0.035f, 0.9f);
const Color Clear(0.0f, 0.0f, 0.0f, 0.0f);

// text isn't drawn on a fully transparent panel, so the popup panel keeps a trace of tint
const Color AlmostClear(0.0f, 0.0f, 0.0f, 0.02f);

} // namespace Colors

constexpr float LabelSize = 14.0f;
constexpr float ValueSize = 40.0f;
constexpr float SmallValueSize = 34.0f;
constexpr float SubtitleSize = 20.0f;
constexpr float ControlSize = 19.0f;
constexpr float PromptSize = 22.0f;
constexpr float PopupSize = 44.0f;

constexpr uint32 CardCornerRadius = 10;
constexpr uint32 BadgeCornerRadius = 14;

constexpr int BoostMeterWidth = 174;
constexpr int BoostMeterHeight = 12;
constexpr int RingProgressWidth = 168;
constexpr int RingProgressHeight = 5;

constexpr float PopupDuration = 1.2f;
constexpr float PopupFadeTime = 0.5f;
constexpr float PopupRiseSpeed = 30.0f;
constexpr int PopupY = -120;

constexpr float FlashOpacity = 0.3f;
constexpr float FlashFadeRate = 4.0f;

UIObjectSize Pixels(int width, int height)
{
    return UIObjectSize { { width, UIObjectSize::PIXEL }, { height, UIObjectSize::PIXEL } };
}

UIObjectSize FullWidth(int height)
{
    return UIObjectSize { { 100, UIObjectSize::PERCENT }, { height, UIObjectSize::PIXEL } };
}

UIObjectSize FullScreen()
{
    return UIObjectSize { { 100, UIObjectSize::PERCENT }, { 100, UIObjectSize::PERCENT } };
}

Handle<UIPanel> AddPanel(const Handle<UIPanel>& parent, Name name, Vec2i position, UIObjectSize size, const Color& color, uint32 cornerRadius = 0, Align alignment = Align::TOP_LEFT)
{
    Handle<UIPanel> panel = parent->CreateUIObject<UIPanel>(name, position, size);
    panel->SetBackgroundColor(color);
    panel->SetBorderRadius(cornerRadius);
    panel->SetOriginAlignment(alignment);
    panel->SetParentAlignment(alignment);

    parent->AddChildUIObject(panel);

    return panel;
}

Handle<UIText> AddText(const Handle<UIPanel>& parent, Name name, Vec2i position, const String& text, float size, const Color& color, Align alignment = Align::TOP_LEFT)
{
    Handle<UIText> uiText = parent->CreateUIObject<UIText>(name, position, UIObjectSize(Vec2i::Zero(), UIObjectSize::AUTO));
    uiText->SetText(text);
    uiText->SetTextSize(size);
    uiText->SetTextColor(color);
    uiText->SetOriginAlignment(alignment);
    uiText->SetParentAlignment(alignment);

    parent->AddChildUIObject(uiText);

    return uiText;
}

void SetText(const Handle<UIText>& uiText, const String& text)
{
    if (uiText.IsValid() && uiText->GetText() != text)
    {
        uiText->SetText(text);
    }
}

void SetTextOpacity(const Handle<UIText>& uiText, float opacity)
{
    if (!uiText.IsValid())
    {
        return;
    }

    const Color color = uiText->GetTextColor();

    uiText->SetTextColor(Color(color.GetRed(), color.GetGreen(), color.GetBlue(), opacity));
}

void SetVisible(const Handle<UIPanel>& panel, bool visible)
{
    if (panel.IsValid() && panel->IsVisible() != visible)
    {
        panel->SetIsVisible(visible);
    }
}

void SetWidth(const Handle<UIPanel>& panel, int width, int height)
{
    if (panel.IsValid() && panel->GetSize().GetValue().x != width)
    {
        panel->SetSize(Pixels(width, height));
    }
}

void SetColor(const Handle<UIPanel>& panel, const Color& color)
{
    if (panel.IsValid() && panel->GetBackgroundColor() != color)
    {
        panel->SetBackgroundColor(color);
    }
}

String FormatSeconds(float seconds)
{
    const int tenths = int(seconds * 10.0f + 0.5f);

    return HYP_FORMAT("{}.{}", tenths / 10, tenths % 10);
}

} // namespace

void GlideHud::Create(UIStage& stage)
{
    m_root = stage.CreateUIObject<UIPanel>(NAME("GlideHud"), Vec2i::Zero(), FullScreen());
    m_root->SetBackgroundColor(Colors::Clear);
    stage.AddChildUIObject(m_root);

    m_flash = AddPanel(m_root, NAME("Flash"), Vec2i::Zero(), FullScreen(), Colors::Clear);
    m_flash->SetIsVisible(false);

    CreateFlightHud();
    CreatePopup();
    CreateTitleScreen();
    CreateCrashScreen();
    CreateFinishScreen();

    ShowTitleScreen();
}

void GlideHud::Destroy(UIStage& stage)
{
    if (m_root.IsValid())
    {
        stage.RemoveChildUIObject(m_root.Get());
    }

    *this = GlideHud();
}

void GlideHud::CreateFlightHud()
{
    m_flightHud = AddPanel(m_root, NAME("FlightHud"), Vec2i::Zero(), FullScreen(), Colors::Clear);

    Handle<UIPanel> scoreCard = AddPanel(m_flightHud, NAME("ScoreCard"), Vec2i(20, 16), Pixels(230, 84), Colors::Card, CardCornerRadius);
    AddPanel(scoreCard, NAME("Accent"), Vec2i(0, 16), Pixels(4, 52), Colors::Gold, 2);
    AddText(scoreCard, NAME("Label"), Vec2i(18, 10), "SCORE", LabelSize, Colors::Muted);

    m_scoreText = AddText(scoreCard, NAME("Score"), Vec2i(18, 28), "0", ValueSize, Colors::Sand);

    m_chainBadge = AddPanel(scoreCard, NAME("ChainBadge"), Vec2i(-12, 12), Pixels(58, 28), Colors::Gold, BadgeCornerRadius, Align::TOP_RIGHT);
    m_chainText = AddText(m_chainBadge, NAME("Chain"), Vec2i::Zero(), "x2", 18.0f, Colors::Ink, Align::CENTER);
    m_chainBadge->SetIsVisible(false);

    m_autopilotBadge = AddPanel(m_flightHud, NAME("AutopilotBadge"), Vec2i(20, 110), Pixels(130, 28), Colors::Cyan, BadgeCornerRadius);
    AddText(m_autopilotBadge, NAME("Autopilot"), Vec2i::Zero(), "AUTOPILOT", 15.0f, Colors::Ink, Align::CENTER);
    m_autopilotBadge->SetIsVisible(false);

    Handle<UIPanel> topStrip = AddPanel(m_flightHud, NAME("TopStrip"), Vec2i::Zero(), FullWidth(116), Colors::Clear);
    Handle<UIPanel> ringsCard = AddPanel(topStrip, NAME("RingsCard"), Vec2i::Zero(), Pixels(200, 84), Colors::Card, CardCornerRadius, Align::CENTER);
    
    AddText(ringsCard, NAME("Label"), Vec2i(0, -24), "RINGS", LabelSize, Colors::Muted, Align::CENTER);
    m_ringsText = AddText(ringsCard, NAME("Rings"), Vec2i(0, 4), "0 / 0", SmallValueSize, Colors::Sand, Align::CENTER);

    Handle<UIPanel> ringTrack = AddPanel(ringsCard, NAME("RingTrack"), Vec2i(0, 30), Pixels(RingProgressWidth, RingProgressHeight), Colors::Track, 2, Align::CENTER);
    m_ringProgress = AddPanel(ringTrack, NAME("RingProgress"), Vec2i::Zero(), Pixels(0, RingProgressHeight), Colors::Gold, 2);

    Handle<UIPanel> timeCard = AddPanel(m_flightHud, NAME("TimeCard"), Vec2i(-20, 16), Pixels(190, 84), Colors::Card, CardCornerRadius, Align::TOP_RIGHT);
    AddPanel(timeCard, NAME("Accent"), Vec2i(0, 16), Pixels(4, 52), Colors::Gold, 2, Align::TOP_RIGHT);
    AddText(timeCard, NAME("Label"), Vec2i(-18, 10), "TIME", LabelSize, Colors::Muted, Align::TOP_RIGHT);

    m_timeText = AddText(timeCard, NAME("Time"), Vec2i(-18, 28), "0.0", ValueSize, Colors::Sand, Align::TOP_RIGHT);

    Handle<UIPanel> bottomStrip = AddPanel(m_flightHud, NAME("BottomStrip"), Vec2i::Zero(), FullWidth(104), Colors::Clear, 0, Align::BOTTOM_LEFT);
    Handle<UIPanel> speedCard = AddPanel(bottomStrip, NAME("SpeedCard"), Vec2i::Zero(), Pixels(310, 72), Colors::Card, CardCornerRadius, Align::CENTER);
    AddText(speedCard, NAME("SpeedLabel"), Vec2i(18, 10), "SPEED", LabelSize, Colors::Muted);
    m_speedText = AddText(speedCard, NAME("Speed"), Vec2i(18, 26), "0", SmallValueSize, Colors::Sand);

    AddText(speedCard, NAME("BoostLabel"), Vec2i(118, 10), "BOOST", LabelSize, Colors::Muted);

    Handle<UIPanel> boostTrack = AddPanel(speedCard, NAME("BoostTrack"), Vec2i(118, 36), Pixels(BoostMeterWidth, BoostMeterHeight), Colors::Track, 3);
    m_boostFill = AddPanel(boostTrack, NAME("BoostFill"), Vec2i::Zero(), Pixels(0, BoostMeterHeight), Colors::Cyan, 3);

    for (int quarter = 1; quarter < 4; quarter++)
    {
        AddPanel(boostTrack, NAME("Divider"), Vec2i(BoostMeterWidth * quarter / 4 - 1, 0), Pixels(2, BoostMeterHeight), Colors::Divider);
    }
}

void GlideHud::CreatePopup()
{
    m_popup = AddPanel(m_root, NAME("Popup"), Vec2i(0, PopupY), Pixels(900, 80), Colors::AlmostClear, 40, Align::CENTER);
    m_popupShadow = AddText(m_popup, NAME("PopupShadow"), Vec2i(3, 4), " ", PopupSize, Colors::Shadow, Align::CENTER);
    m_popupText = AddText(m_popup, NAME("PopupText"), Vec2i::Zero(), " ", PopupSize, Colors::Sand, Align::CENTER);
    m_popup->SetIsVisible(false);
}

void GlideHud::CreateTitleScreen()
{
    m_titleScreen = CreateScreen(NAME("TitleScreen"), 340, Colors::Gold);

    CreateTitle(m_titleScreen, NAME("Title"), -112, 76.0f, Colors::Gold, "GLIDE");
    AddPanel(m_titleScreen, NAME("Underline"), Vec2i(0, -62), Pixels(90, 3), Colors::Gold, 1, Align::CENTER);

    int y = -44;

    const auto addControl = [this, &y](const char* keys, const char* action) -> Handle<UIText>
    {
        Handle<UIText> keysText = AddText(m_titleScreen, NAME("Keys"), Vec2i(-14, y), keys, ControlSize, Colors::Gold, Align::CENTER);
        keysText->SetOriginAlignment(Align::TOP_RIGHT);

        Handle<UIText> actionText = AddText(m_titleScreen, NAME("Action"), Vec2i(14, y), action, ControlSize, Colors::Sand, Align::CENTER);
        actionText->SetOriginAlignment(Align::TOP_LEFT);

        y += 28;

        return actionText;
    };

    addControl("A / D    LEFT / RIGHT", "Steer");
    addControl("W / S    UP / DOWN", "Dive / climb");
    addControl("SHIFT", "Boost");
    // addControl("P", "Autopilot");
    m_invertText = addControl("I", "Invert vertical: Off");

    CreatePrompt(m_titleScreen, NAME("Prompt"), 128, "PRESS SPACE TO FLY");
}

void GlideHud::CreateCrashScreen()
{
    m_crashScreen = CreateScreen(NAME("CrashScreen"), 240, Colors::Red);

    CreateTitle(m_crashScreen, NAME("Title"), -62, 64.0f, Colors::Red, "WIPEOUT");
    AddText(m_crashScreen, NAME("Subtitle"), Vec2i(0, -14), "You're stuck. Now what?", SubtitleSize, Colors::Muted, Align::CENTER);
    m_crashStats = AddText(m_crashScreen, NAME("Stats"), Vec2i(0, 22), " ", SubtitleSize, Colors::Sand, Align::CENTER);

    CreatePrompt(m_crashScreen, NAME("Prompt"), 74, "PRESS SPACE TO RETRY");
}

void GlideHud::CreateFinishScreen()
{
    m_finishScreen = CreateScreen(NAME("FinishScreen"), 320, Colors::Gold);

    CreateTitle(m_finishScreen, NAME("Title"), -108, 72.0f, Colors::Gold, "FINISH!");
    AddText(m_finishScreen, NAME("Subtitle"), Vec2i(0, -60), "Way to go space cowboy", SubtitleSize, Colors::Muted, Align::CENTER);

    const auto addStat = [this](int x, const char* label) -> Handle<UIText>
    {
        AddText(m_finishScreen, NAME("Label"), Vec2i(x, -22), label, LabelSize, Colors::Muted, Align::CENTER);

        return AddText(m_finishScreen, NAME("Value"), Vec2i(x, 10), " ", 38.0f, Colors::Sand, Align::CENTER);
    };

    m_finishScore = addStat(-170, "SCORE");
    m_finishRings = addStat(0, "RINGS");
    m_finishTime = addStat(170, "TIME");

    m_finishBest = AddText(m_finishScreen, NAME("Best"), Vec2i(0, 56), " ", SubtitleSize, Colors::Muted, Align::CENTER);

    CreatePrompt(m_finishScreen, NAME("Prompt"), 108, "PRESS SPACE TO FLY AGAIN");
}

Handle<UIPanel> GlideHud::CreateScreen(Name name, int height, const Color& accent)
{
    Handle<UIPanel> screen = AddPanel(m_root, name, Vec2i::Zero(), FullWidth(height), Colors::Screen, 0, Align::CENTER);

    AddPanel(screen, NAME("TopEdge"), Vec2i::Zero(), FullWidth(3), accent);
    AddPanel(screen, NAME("BottomEdge"), Vec2i::Zero(), FullWidth(3), accent, 0, Align::BOTTOM_LEFT);

    screen->SetIsVisible(false);

    return screen;
}

void GlideHud::CreateTitle(const Handle<UIPanel>& screen, Name name, int y, float size, const Color& color, const String& text)
{
    AddText(screen, NAME("TitleShadow"), Vec2i(3, y + 4), text, size, Colors::Shadow, Align::CENTER);
    AddText(screen, name, Vec2i(0, y), text, size, color, Align::CENTER);
}

void GlideHud::CreatePrompt(const Handle<UIPanel>& screen, Name name, int y, const String& text)
{
    m_prompts.PushBack(AddText(screen, name, Vec2i(0, y), text, PromptSize, Colors::Gold, Align::CENTER));
}

void GlideHud::ShowOnly(const Handle<UIPanel>& panel)
{
    for (const Handle<UIPanel>& candidate : { m_flightHud, m_titleScreen, m_crashScreen, m_finishScreen })
    {
        SetVisible(candidate, candidate == panel);
    }

    m_promptTime = 0.0f;
}

void GlideHud::ShowTitleScreen()
{
    ShowOnly(m_titleScreen);
}

void GlideHud::ShowFlightHud()
{
    ShowOnly(m_flightHud);
}

void GlideHud::ShowCrashScreen(const FlightStats& stats)
{
    SetText(m_crashStats, HYP_FORMAT("Score {}       Rings {} / {}       Time {}", stats.score, stats.ringsCollected, stats.ringCount, FormatSeconds(stats.time)));

    ShowOnly(m_crashScreen);
}

void GlideHud::ShowFinishScreen(const FlightStats& stats, const BestRun& best)
{
    SetText(m_finishScore, HYP_FORMAT("{}", stats.score));
    SetText(m_finishRings, HYP_FORMAT("{} / {}", stats.ringsCollected, stats.ringCount));
    SetText(m_finishTime, FormatSeconds(stats.time));

    if (m_finishBest.IsValid())
    {
        if (best.isNewBest)
        {
            m_finishBest->SetText("NEW BEST SCORE!");
            m_finishBest->SetTextColor(Colors::Gold);
        }
        else
        {
            m_finishBest->SetText(HYP_FORMAT("Best score {}       Best time {}", best.score, FormatSeconds(best.time)));
            m_finishBest->SetTextColor(Colors::Muted);
        }
    }

    ShowOnly(m_finishScreen);
}

void GlideHud::SetFlightStats(const FlightStats& stats)
{
    SetText(m_scoreText, HYP_FORMAT("{}", stats.score));
    SetText(m_ringsText, HYP_FORMAT("{} / {}", stats.ringsCollected, stats.ringCount));
    SetText(m_timeText, FormatSeconds(stats.time));
    SetText(m_speedText, HYP_FORMAT("{}", stats.speed));

    const bool showChain = stats.chain > 1;

    if (showChain)
    {
        SetText(m_chainText, HYP_FORMAT("x{}", stats.chain));
    }

    SetVisible(m_chainBadge, showChain);
    SetVisible(m_autopilotBadge, stats.autopilot);

    const float ringFraction = stats.ringCount > 0 ? float(stats.ringsCollected) / float(stats.ringCount) : 0.0f;

    SetWidth(m_ringProgress, int(float(RingProgressWidth) * ringFraction), RingProgressHeight);
    SetWidth(m_boostFill, int(float(BoostMeterWidth) * stats.boostMeter), BoostMeterHeight);
    SetColor(m_boostFill, stats.boosting ? Colors::BrightCyan : Colors::Cyan);
}

void GlideHud::SetInvertVertical(bool invertVertical)
{
    SetText(m_invertText, invertVertical ? "Invert vertical: On" : "Invert vertical: Off");
}

void GlideHud::ShowPopup(const String& text, const Color& color)
{
    if (!m_popup.IsValid())
    {
        return;
    }

    m_popupText->SetText(text);
    m_popupText->SetTextColor(color);

    m_popupShadow->SetText(text);
    m_popupShadow->SetTextColor(Colors::Shadow);

    m_popup->SetIsVisible(true);
    m_popupTime = PopupDuration;
}

void GlideHud::Flash(const Color& tint)
{
    m_flashTint = tint;
    m_flashAmount = 1.0f;
}

void GlideHud::Update(float delta, bool promptReady)
{
    m_flashAmount = MathUtil::Max(m_flashAmount - FlashFadeRate * delta, 0.0f);

    SetVisible(m_flash, m_flashAmount > 0.0f);

    if (m_flashAmount > 0.0f)
    {
        SetColor(m_flash, Color(m_flashTint.GetRed(), m_flashTint.GetGreen(), m_flashTint.GetBlue(), FlashOpacity * m_flashAmount));
    }

    if (m_popupTime > 0.0f && m_popup.IsValid())
    {
        m_popupTime = MathUtil::Max(m_popupTime - delta, 0.0f);

        const float age = PopupDuration - m_popupTime;
        const float opacity = MathUtil::Min(m_popupTime / PopupFadeTime, 1.0f);

        SetTextOpacity(m_popupText, opacity);
        SetTextOpacity(m_popupShadow, Colors::Shadow.GetAlpha() * opacity);

        m_popup->SetPosition(Vec2i(0, PopupY - int(age * PopupRiseSpeed)));
        SetVisible(m_popup, m_popupTime > 0.0f);
    }

    m_promptTime += delta;

    const float promptOpacity = promptReady ? 0.6f + 0.4f * std::sin(m_promptTime * 4.0f) : 0.0f;

    for (const Handle<UIText>& prompt : m_prompts)
    {
        SetTextOpacity(prompt, promptOpacity);
    }
}

} // namespace Hyperion
