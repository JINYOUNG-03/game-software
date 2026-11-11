#include "stdafx.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <random>
#include <iterator>
#include <GL/glew.h>
#include "GlfwInput.h"
#include "KoreanFont.h"
#include "Renderer.h"
#include "LevelOne.h"
#include "Profiler.h"
#include "HarborWorld.h"
#include "WorldMonitor.h"
#include "CharacterPresentation.h"
#include "InventoryPresentation.h"
#include "TradePresentation.h"

namespace
{
    Monitor::Server worldMonitor;
    constexpr int ScreenWidth = 1280;
    constexpr int ScreenHeight = 800;
    constexpr int MapSize = Level::MapSize;
    constexpr float TileWidth = 96;
    constexpr float TileHeight = 48;
    constexpr float NpcNameDistance = 2.5f;
    constexpr int UiFontHeight = 15;
    constexpr float TextLineHeight = 21;
    constexpr float QuestPanelWidth = 740;
    constexpr float QuestPanelHeight = 88;
    constexpr float HelpPanelWidth = 720;
    constexpr float HelpPanelHeight = 360;
    using Vec3 = Level::Position;

    using Npc = Harbor::Npc;

    enum class UiPart
    {
        Hud,
        MiniMap,
        Stats,
        Prompt,
        Pause,
        Journal,
        SiteLabel,
        NpcLabel,
        QuestMarker,
        QuestTracker,
        GuideHelp,
        Dialogue,
        Trade
    };
    ActorId promptId = InvalidActorId;
    ActorId questMarkerId = InvalidActorId;
    ActorId inventoryPanelId = InvalidActorId;
    ActorId tradePanelId = InvalidActorId;
    float uiOriginX = 0;
    float uiOriginY = 0;

    Renderer* renderer = nullptr;
    GLFWwindow* gameWindow = nullptr;
    KoreanFont koreanFont;
    Level::FirstLevel firstLevel;
    Harbor::World harbor(firstLevel);
    Vec3 renderCamera;

    Vec3 PlayerPosition()
    {
        return firstLevel.Player().WorldPosition();
    }

    bool keys[256] = {};
    bool interactionPressed = false;
    bool potionPressed = false;
    bool tradePressed = false;
    bool sitePressed = false;
    bool revealPressed = false;
    bool journalPanel = false;
    bool worldMap = false;
    int attributePressed = 0;
    bool statsPanel = false;
    bool inventoryTogglePressed = false;
    bool inventoryClosePressed = false;
    bool inventoryUsePressed = false;
    bool inventoryClickPressed = false;
    int inventoryMovePressed = 0;
    int inventorySelection = 0;
    float inventoryClickX = 0, inventoryClickY = 0;
    std::wstring inventoryActionNote;
    bool paused = false;
    bool helpVisible = true;
    bool helpPressed = false;
    bool continueHelpPressed = false;
    bool introVisible = true;
    int introPage = 0;
    bool skipIntroPressed = false;
    bool dialogueNextPressed = false;
    bool dialogueClosePressed = false;
    int dialogueChoicePressed = -1;
    bool tradeClosePressed = false;
    bool tradeConfirmPressed = false;
    bool tradeClickPressed = false;
    bool tradeConfirmPending = false;
    int tradeSelection = 0;
    int tradeQuantity = 1;
    int tradeMovePressed = 0;
    int tradeQuantityPressed = 0;
    float tradeClickX = 0;
    float tradeClickY = 0;
    std::chrono::steady_clock::time_point previousTime{};
    int outputWidth = ScreenWidth;
    int outputHeight = ScreenHeight;
    int inputWidth = ScreenWidth;
    int inputHeight = ScreenHeight;
    float elapsed = 0;
    HDC fontDC = nullptr;
    HFONT fontHandle = nullptr;

    float Distance2D(const Vec3& a, const Vec3& b)
    {
        return std::hypot(a.x - b.x, a.y - b.y);
    }

    void WorldToScreen(const Vec3& point, float& x, float& y)
    {
        x = ScreenWidth * .5f + ((point.x - point.y) - (renderCamera.x - renderCamera.y)) * TileWidth * .5f;
        y = ScreenHeight * .52f +
            ((point.x + point.y) - (renderCamera.x + renderCamera.y)) * TileHeight * .5f - point.z;
    }

    bool Visible(float x, float y)
    {
        return x > -260 && x < ScreenWidth + 260 && y > -100 && y < ScreenHeight + 260;
    }

    bool InitializeKoreanFont()
    {
        if (!koreanFont.Initialize(UiFontHeight))
        {
            std::cerr << "[font] initialization_failed; deploy Fonts/NanumGothic-Regular.ttf\n";
            return false;
        }
        fontDC = static_cast<HDC>(koreanFont.Context());
        fontHandle = static_cast<HFONT>(koreanFont.Handle());
        return true;
    }

    void DrawText(float x,
                  float y,
                  const wchar_t* text,
                  float r = .93f,
                  float g = .88f,
                  float b = .73f,
                  float alpha = 1)
    {
        renderer->DrawText(x, y, text, r, g, b, fontDC, fontHandle, alpha);
    }

    void Say(const wchar_t* name, const wchar_t* line)
    {
        firstLevel.message = std::wstring(name) + L" : " + line;
        firstLevel.messageTime = 7;
    }

    bool InitializeModels()
    {
        wchar_t executable[32768] = {};
        DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
        if (!length || length >= 32768)
        {
            return false;
        }
        std::wstring directory(executable, length);
        directory.resize(directory.find_last_of(L"\\/") + 1);
        if (!renderer->LoadResourceSprites(directory + L"Resources"))
        {
            return false;
        }
        std::vector<std::string> required = {
            "drain", "sluice", "sluice_open", "notice", "wreck", "ledger", "key", "corpse", "bandit_sign"};
        for (int kind = 0; kind < 20; ++kind)
        {
            required.push_back("prop" + std::to_string(kind));
        }
        for (int kind = 0; kind < 6; ++kind)
        {
            required.push_back("tile" + std::to_string(kind));
        }
        for (int kind = 0; kind < 3; ++kind)
        {
            required.push_back("loot" + std::to_string(kind));
        }
        for (int direction = 0; direction < CharacterPresentation::DirectionCount; ++direction)
        {
            const std::string suffix = "@" + std::to_string(direction);
            required.push_back("hero_idle" + suffix);
            for (int frame = 0; frame < CharacterPresentation::WalkFrames; ++frame)
            {
                required.push_back("hero" + std::to_string(frame) + suffix);
            }
            for (int frame = 0; frame < CharacterPresentation::AttackFrames; ++frame)
            {
                required.push_back("hero_attack" + std::to_string(frame) + suffix);
            }
            for (int look = 0; look < WorldLayout::NpcAppearanceCount; ++look)
            {
                const std::string prefix = "citizen" + std::to_string(look) + "_";
                required.push_back(prefix + "idle" + suffix);
                for (int frame = 0; frame < CharacterPresentation::WalkFrames; ++frame)
                {
                    required.push_back(prefix + std::to_string(frame) + suffix);
                }
            }
            for (int kind = 0; kind < 3; ++kind)
            {
                const std::string prefix = "beast" + std::to_string(kind) + "_";
                required.push_back(prefix + "idle" + suffix);
                for (int frame = 0; frame < CharacterPresentation::WalkFrames; ++frame)
                {
                    required.push_back(prefix + std::to_string(frame) + suffix);
                    required.push_back(prefix + "run" + std::to_string(frame) + suffix);
                }
                for (int frame = 0; frame < CharacterPresentation::BeastAttackFrames; ++frame)
                {
                    required.push_back(prefix + "attack" + std::to_string(frame) + suffix);
                }
            }
        }
        return std::all_of(required.begin(),
                           required.end(),
                           [](const std::string& model)
                           {
                               return renderer->HasModel(model);
                           });
    }

    void InitializeSceneActors()
    {
        Perf::Scope scope(Perf::Cpu::ScenePrepare);
        SceneGraph& scene = firstLevel.Scene();
        harbor.Initialize();
        for (ActorKind kind : {ActorKind::Player, ActorKind::Npc, ActorKind::Enemy, ActorKind::Prop})
        {
            for (ActorId id : scene.FindByKind(kind))
            {
                Actor& actor = scene.Get(id);
                actor.visualHeight = renderer->ModelVisualHeight(actor.model) + 4;
                if (actor.shadowWidth > 0)
                {
                    actor.shadowHeight = actor.visualHeight;
                }
            }
        }
        for (Harbor::Landmark* site : scene.Actors<Harbor::Landmark>(ActorKind::Prop))
        {
            Actor& label = scene.Create(site->Id(), "SiteLabel", ActorKind::UI);
            label.pass = ScenePass::Overlay;
            label.variant = int(UiPart::SiteLabel);
            label.drawable = true;
            label.SetLocalPosition({0, 0, 105});
        }
        for (Npc* npc : scene.Actors<Npc>(ActorKind::Npc))
        {
            Actor& label = scene.Create(npc->Id(), "NpcIdentity", ActorKind::UI);
            label.pass = ScenePass::Overlay;
            label.variant = int(UiPart::NpcLabel);
            label.drawable = true;
            label.drawOrder = 1;
            label.SetLocalPosition({0, 0, npc->visualHeight + 36});
        }
        scene.DestroyChildren(firstLevel.OverlayRoot());
        for (UiPart part : {UiPart::Hud,
                            UiPart::MiniMap,
                            UiPart::Stats,
                            UiPart::Journal,
                            UiPart::Pause,
                            UiPart::QuestTracker,
                            UiPart::GuideHelp,
                            UiPart::Dialogue,
                            UiPart::Trade})
        {
            Actor& ui = scene.Create(firstLevel.OverlayRoot(), "UIPanel", ActorKind::UI);
            ui.variant = int(part);
            if (part == UiPart::Stats)
            {
                inventoryPanelId = ui.Id();
            }
            if (part == UiPart::Trade)
            {
                tradePanelId = ui.Id();
            }
            ui.drawable = true;
            ui.pass = ScenePass::Overlay;
            ui.drawOrder = part == UiPart::GuideHelp  ? 200
                           : part == UiPart::Dialogue ? 150
                           : part == UiPart::Trade    ? 140
                           : part == UiPart::Stats    ? 120
                           : part == UiPart::Pause    ? 100
                                                      : int(part);
        }
        helpVisible = introVisible = true;
        introPage = 0;
        helpPressed = continueHelpPressed = skipIntroPressed = false;
        dialogueNextPressed = dialogueClosePressed = false;
        dialogueChoicePressed = -1;
        promptId = InvalidActorId;
        Actor& marker = scene.Create(firstLevel.OverlayRoot(), "CurrentQuestTarget", ActorKind::UI);
        marker.variant = int(UiPart::QuestMarker);
        marker.pass = ScenePass::Overlay;
        marker.drawable = true;
        marker.drawOrder = 9;
        marker.SetVisible(false);
        questMarkerId = marker.Id();
    }

    void UpdatePromptActor()
    {
        SceneGraph& scene = firstLevel.Scene();
        if (Actor* marker = scene.Find(questMarkerId))
        {
            ActorId target = harbor.ObjectiveTarget();
            marker->SetVisible(target != InvalidActorId);
            if (target)
            {
                if (marker->ParentId() != target)
                {
                    scene.Reparent(marker->Id(), target, false);
                }
                marker->SetLocalPosition({0, 0, 125});
            }
        }
        Actor* prompt = scene.Find(promptId);
        if (!prompt)
        {
            prompt = &scene.Create(firstLevel.OverlayRoot(), "InteractionPrompt", ActorKind::UI);
            promptId = prompt->Id();
            prompt->variant = int(UiPart::Prompt);
            prompt->pass = ScenePass::Overlay;
            prompt->drawable = true;
            prompt->drawOrder = 10;
        }
        ActorId target = InvalidActorId;
        float distance = 1.4f;
        for (const auto* drop : firstLevel.Drops())
        {
            if (drop->IsVisible() && Distance2D(PlayerPosition(), drop->WorldPosition()) < 1.3f &&
                firstLevel.ClearLine(PlayerPosition(), drop->WorldPosition()))
            {
                target = drop->Id();
                break;
            }
        }
        if (!target)
        {
            for (const Npc* npc : scene.Actors<Npc>(ActorKind::Npc))
            {
                float d = Distance2D(PlayerPosition(), npc->WorldPosition());
                if (npc->IsVisible() && d < distance &&
                    firstLevel.ClearLine(PlayerPosition(), npc->WorldPosition()))
                {
                    distance = d;
                    target = npc->Id();
                }
            }
        }
        if (!target)
        {
            if (const Actor* site = harbor.NearbySite())
            {
                target = site->Id();
            }
        }
        prompt->SetVisible(target != InvalidActorId && !paused && !helpVisible &&
                           !harbor.Conversation().Active());
        if (target)
        {
            scene.Reparent(promptId, target, false);
            const Actor* owner = scene.Find(target);
            prompt->SetLocalPosition({0, 0, owner && owner->Kind() == ActorKind::Npc ? -22.0f : 70.0f});
        }
    }

    void DrawEffectActor(const Actor& actor, float x, float y)
    {
        renderer->SetMaterial(0);
        switch (ActorEffect(actor.variant))
        {
        case ActorEffect::HitImpact:
            if (const auto* hit = dynamic_cast<const Level::HitImpact*>(&actor))
            {
                Perf::Scope scope(Perf::Cpu::HitFeedbackRender);
                const float progress = hit->age / hit->duration;
                renderer->SetSurfaceData(
                    {{progress, hit->directionX, hit->directionY, float(hit->Id() % 17)}});
                renderer->SetMaterial(28);
                renderer->DrawSolidRect(x,
                                        y,
                                        hit->outgoing ? 106.0f : 82.0f,
                                        82,
                                        1,
                                        hit->incoming ? .30f : .78f,
                                        hit->incoming ? .18f : .37f,
                                        hit->outgoing || hit->incoming ? 1.0f : .65f);
            }
            break;
        case ActorEffect::WarmLight:
            renderer->SetMaterial(17);
            renderer->DrawSolidRect(x, y, 150, 110, 1, .66f, .29f, .065f);
            break;
        case ActorEffect::HarborMist:
            renderer->SetMaterial(16);
            renderer->DrawSolidRect(x, y, 290, 70, .63f, .72f, .67f, .085f);
            break;
        case ActorEffect::Flame:
            renderer->SetMaterial(17);
            renderer->DrawSolidRect(x, y + 4, 70, 65, 1, .54f, .19f, .11f);
            renderer->SetMaterial(8);
            renderer->DrawSolidRect(x, y, 10, 18, 1, 1, 1, .92f);
            break;
        case ActorEffect::Smoke:
            renderer->SetMaterial(9);
            renderer->DrawSolidRect(x, y, 75, 95, .47f, .52f, .50f, .19f);
            break;
        case ActorEffect::PlayerHurt:
            if (firstLevel.hurtFlash > 0)
            {
                renderer->DrawEllipse(x, y - 25, 40, 60, 1, .14f, .09f, .35f);
            }
            break;
        case ActorEffect::SwordTrail:
            if (firstLevel.attackAnimation > 0)
            {
                float frame = CharacterPresentation::Clamp01(1 - firstLevel.attackAnimation /
                                                                     CharacterPresentation::AttackDuration) *
                              (CharacterPresentation::AttackFrames - 1);
                renderer->DrawResourceSwordTrail(
                    frame,
                    firstLevel.Player().visualDirection,
                    x,
                    y,
                    CharacterPresentation::AttackTrailStrength(firstLevel.attackAnimation));
            }
            break;
        case ActorEffect::EnemyStatus:
            if (const auto* enemy =
                    dynamic_cast<const Level::Enemy*>(firstLevel.Scene().Find(actor.ParentId())))
            {
                if (enemy->attackTime >= 0 &&
                    enemy->attackTime < CharacterPresentation::BeastAttackDuration(enemy->kind) *
                                            CharacterPresentation::BeastImpactFraction)
                {
                    // Existing status Actor also carries the wind-up warning; no new scene placement.
                    renderer->DrawEllipse(x, y + 2, 62, 25, .95f, .48f, .15f, .27f);
                }
                if (enemy->flash > 0)
                {
                    renderer->DrawEllipse(x, y - 20, 48, 40, 1, .7f, .45f, .4f);
                }
                if (enemy->health < enemy->maxHealth ||
                    Distance2D(enemy->WorldPosition(), PlayerPosition()) < 4)
                {
                    const float barY = y - enemy->visualHeight - 8;
                    renderer->DrawSolidRect(x, barY, 42, 5, .07f, .055f, .05f, 1);
                    float width = 40.0f * enemy->health / enemy->maxHealth;
                    if (width > 0)
                    {
                        renderer->DrawSolidRect(x - 20 + width * .5f, barY, width, 3, .73f, .25f, .18f, 1);
                    }
                }
            }
            break;
        case ActorEffect::NpcStatus:
            if (const auto* npc = dynamic_cast<const Npc*>(firstLevel.Scene().Find(actor.ParentId())))
            {
                constexpr float flashDuration = .16f;
                using namespace CharacterPresentation;
                if (npc->attackTime >= 0 && npc->attackTime < NpcAttackWindup + flashDuration)
                {
                    const bool warning = npc->attackTime < NpcAttackWindup;
                    const float strength =
                        warning ? .18f + .20f * npc->attackTime / NpcAttackWindup
                                : .48f * (1 - (npc->attackTime - NpcAttackWindup) / flashDuration);
                    const float fx = npc->attackForward.x, fy = npc->attackForward.y;
                    const float sx = (fx - fy) * TileWidth * .5f * NpcAttackRange;
                    const float sy = (fx + fy) * TileHeight * .5f * NpcAttackRange;
                    const float side = std::sqrt(1 - NpcAttackConeDot * NpcAttackConeDot);
                    renderer->DrawQuad(x,
                                       y,
                                       x + sx * NpcAttackConeDot - sy * (TileWidth / TileHeight) * side,
                                       y + sy * NpcAttackConeDot + sx * (TileHeight / TileWidth) * side,
                                       x + sx,
                                       y + sy,
                                       x + sx * NpcAttackConeDot + sy * (TileWidth / TileHeight) * side,
                                       y + sy * NpcAttackConeDot - sx * (TileHeight / TileWidth) * side,
                                       1.0f,
                                       warning ? .52f : .25f,
                                       .16f,
                                       strength);
                    Perf::Count(Perf::Counter::NpcAttackCuesDrawn);
                }
                if (npc->health > 0 &&
                    (npc->health < npc->maxHealth || Distance2D(npc->WorldPosition(), PlayerPosition()) < 2))
                {
                    float barY = y - npc->visualHeight - 12;
                    renderer->DrawSolidRect(x, barY, 42, 5, .07f, .055f, .05f, 1);
                    float width = 40.0f * npc->health / npc->maxHealth;
                    renderer->DrawSolidRect(x - 20 + width * .5f,
                                            barY,
                                            width,
                                            3,
                                            npc->threatTime > 0 ? .8f : .35f,
                                            .58f,
                                            .34f,
                                            1);
                }
            }
            break;
        }
        renderer->SetMaterial(0);
    }

    void UpdateWorldLights()
    {
        Perf::Scope scope(Perf::Cpu::Lighting);
        std::array<float, 32> lights{};
        std::array<float, 8> distances{};
        distances.fill(100000000.0f);
        int count = 0;
        for (const Actor* effect : firstLevel.Scene().FrameList(ScenePass::Effects))
        {
            if (effect->Kind() != ActorKind::Effect || (effect->variant != int(ActorEffect::WarmLight) &&
                                                        effect->variant != int(ActorEffect::Flame)))
            {
                continue;
            }
            Perf::Count(Perf::Counter::LightsConsidered);
            float x, y;
            WorldToScreen(effect->WorldPosition(), x, y);
            float distance = (x - ScreenWidth * .5f) * (x - ScreenWidth * .5f) +
                             (y - ScreenHeight * .52f) * (y - ScreenHeight * .52f);
            int slot = 0;
            if (count < 8)
            {
                slot = count++;
            }
            else
            {
                for (int i = 1; i < 8; ++i)
                {
                    if (distances[size_t(i)] > distances[size_t(slot)])
                    {
                        slot = i;
                    }
                }
                if (distance >= distances[size_t(slot)])
                {
                    continue;
                }
            }
            distances[size_t(slot)] = distance;
            size_t at = size_t(slot) * 4;
            lights[at] = x;
            lights[at + 1] = y;
            lights[at + 2] = effect->variant == int(ActorEffect::Flame) ? 180.0f : 150.0f;
            lights[at + 3] = effect->variant == int(ActorEffect::Flame) ? .72f : .52f;
        }
        renderer->SetWorldLights(lights, count);
        Perf::Count(Perf::Counter::LightsSelected, std::uint64_t(count));
    }

    void RenderWorldPass(ScenePass pass)
    {
        Perf::Scope scope(pass == ScenePass::Ground  ? Perf::Cpu::RenderGround
                          : pass == ScenePass::World ? Perf::Cpu::RenderWorld
                                                     : Perf::Cpu::RenderEffects);
        const SceneGraph& scene = firstLevel.Scene();
        if (pass == ScenePass::World)
        {
            Perf::Scope shadows(Perf::Cpu::RenderShadow);
            renderer->SetSurfaceData({{0, 0, -1, 0}});
            renderer->DrawScene(scene,
                                pass,
                                [&](const Actor& actor)
                                {
                                    float x, y;
                                    WorldToScreen(actor.WorldPosition(), x, y);
                                    if (actor.shadowWidth <= 0)
                                    {
                                        return;
                                    }
                                    Perf::Count(Perf::Counter::ShadowCasters);
                                    float width = actor.shadowWidth * .42f;
                                    float height = actor.shadowHeight > 0 ? actor.shadowHeight : 38.0f;
                                    float dx = height * .66f, dy = height * .30f;
                                    renderer->SetMaterial(19);
                                    renderer->DrawQuad(x - width,
                                                       y,
                                                       x + width,
                                                       y + 8,
                                                       x + dx + width * .65f,
                                                       y + dy + 10,
                                                       x + dx - width * .65f,
                                                       y + dy,
                                                       .065f,
                                                       .10f,
                                                       .11f,
                                                       .27f);
                                    renderer->DrawEllipse(
                                        x + 3, y + 2, actor.shadowWidth * .72f, 15, .04f, .065f, .07f, .32f);
                                });
            renderer->Flush();
            Perf::Profiler::Get().GpuMark("shadow");
        }
        renderer->DrawScene(scene,
                            pass,
                            [&](const Actor& actor)
                            {
                                float x, y;
                                WorldToScreen(actor.WorldPosition(), x, y);
                                renderer->SetSurfaceData(actor.surfaceData);
                                if (!actor.model.empty())
                                {
                                    renderer->DrawModel(actor.model,
                                                        x,
                                                        y,
                                                        actor.mirrorModel,
                                                        actor.visualDirection,
                                                        firstLevel.HitFlash(actor),
                                                        actor.nextModel,
                                                        actor.poseBlend);
                                }
                                if (actor.Kind() == ActorKind::Effect)
                                {
                                    DrawEffectActor(actor, x, y);
                                }
                            });
        if (pass == ScenePass::Effects)
        {
            // All burst quads precede all numbers: avoid atlas/font switches for every hit.
            Perf::Scope feedback(Perf::Cpu::HitFeedbackRender);
            for (const Actor* actor : scene.FrameList(ScenePass::Effects))
            {
                const auto* hit = dynamic_cast<const Level::HitImpact*>(actor);
                if (!hit)
                {
                    continue;
                }
                float x, y;
                WorldToScreen(hit->WorldPosition(), x, y);
                const float progress = hit->age / hit->duration;
                const std::wstring damage = L"-" + std::to_wstring(hit->damage);
                DrawText(x - renderer->TextWidth(damage.c_str(), fontDC, fontHandle) * .5f,
                         y - 17 - progress * 32,
                         damage.c_str(),
                         1,
                         hit->incoming ? .55f : .93f,
                         hit->incoming ? .42f : .62f,
                         std::pow(1 - progress, .7f));
                Perf::Count(Perf::Counter::HitNumbersDrawn);
            }
        }
        renderer->Flush();
        Perf::Profiler::Get().GpuMark(pass == ScenePass::Ground  ? "ground"
                                      : pass == ScenePass::World ? "world"
                                                                 : "effects");
    }

    void TryInteract()
    {
        Perf::Scope scope(Perf::Cpu::Interaction);
        if (!firstLevel.Player().IsActive())
        {
            return;
        }
        const auto guide = harbor.Guide();
        if (guide.ready && guide.interactionKey == 'E')
        {
            harbor.Talk();
            return;
        }
        if (firstLevel.PickUp())
        {
            return;
        }
        harbor.Talk();
    }

    void RenderMiniMap()
    {
        Perf::Scope scope(Perf::Cpu::MiniMap);
        constexpr float cx = 1150, cy = 163, extent = 208;
        const int span = worldMap ? MapSize : 64;
        const int originX =
            worldMap ? 0
                     : (std::max)(0, (std::min)(MapSize - span, int(PlayerPosition().x) / 8 * 8 - span / 2));
        const int originY =
            worldMap ? 0
                     : (std::max)(0, (std::min)(MapSize - span, int(PlayerPosition().y) / 8 * 8 - span / 2));
        const float scale = extent / static_cast<float>(span);
        const float left = cx - extent * .5f, top = cy - extent * .5f;
        renderer->SetMaterial(0);
        renderer->DrawSolidRect(cx, 164, 240, 300, .16f, .21f, .18f, .98f);
        // Snapped local windows reuse the terrain texture; the atlas is available without shrinking local markers.
        const std::uint64_t revision = firstLevel.MapRevision() ^ (std::uint64_t(originX) << 48) ^
                                       (std::uint64_t(originY) << 40) ^
                                       (worldMap ? (std::uint64_t(1) << 63) : 0);
        renderer->DrawMiniMapLayer(revision,
                                   cx,
                                   cy,
                                   [&]()
                                   {
                                       renderer->DrawSolidRect(cx, cy, extent, extent, .67f, .72f, .51f, 1);
                                       const int stride = worldMap ? 2 : 1;
                                       for (int y = originY; y < originY + span; y += stride)
                                       {
                                           for (int x = originX; x < originX + span; x += stride)
                                           {
                                               bool water = false, road = false, blocked = false;
                                               for (int sy = 0; sy < stride; ++sy)
                                               {
                                                   for (int sx = 0; sx < stride; ++sx)
                                                   {
                                                       const auto& tile = firstLevel.At(x + sx, y + sy);
                                                       water = water || tile.terrain == Level::Terrain::Water;
                                                       road = road || tile.terrain == Level::Terrain::Road;
                                                       blocked = blocked || tile.blocked;
                                                   }
                                               }
                                               renderer->DrawSolidRect(
                                                   left + (x - originX + stride * .5f) * scale,
                                                   top + (y - originY + stride * .5f) * scale,
                                                   stride * scale + .1f,
                                                   stride * scale + .1f,
                                                   road      ? .82f
                                                   : water   ? .26f
                                                   : blocked ? .38f
                                                             : .61f,
                                                   road      ? .71f
                                                   : water   ? .56f
                                                   : blocked ? .49f
                                                             : .70f,
                                                   road      ? .48f
                                                   : water   ? .72f
                                                   : blocked ? .31f
                                                             : .46f,
                                                   1);
                                           }
                                       }
                                   });
        auto marker = [&](const Vec3& p, float r, float g, float b, float size, bool edge)
        {
            float x = left + (p.x - originX) * scale;
            float y = top + (p.y - originY) * scale;
            if (!edge && (x < left || x > left + extent || y < top || y > top + extent))
            {
                return;
            }
            x = (std::max)(left + size, (std::min)(left + extent - size, x));
            y = (std::max)(top + size, (std::min)(top + extent - size, y));
            renderer->DrawEllipse(x, y, size + 3, size + 3, .14f, .18f, .12f, 1);
            renderer->DrawEllipse(x, y, size, size, r, g, b, 1);
        };
        for (ActorId id : harbor.LandmarkIds())
        {
            const auto* site = dynamic_cast<const Harbor::Landmark*>(firstLevel.Scene().Find(id));
            if (site)
            {
                marker(site->WorldPosition(),
                       site->site == 7 ? 1.0f : .9f,
                       site->site == 7 ? .2f : .73f,
                       site->site == 7 ? .15f : .42f,
                       6,
                       false);
            }
        }
        for (const Npc* npc : firstLevel.Scene().Actors<Npc>(ActorKind::Npc))
        {
            if (npc->health > 0)
            {
                bool raider = npc->job == Harbor::Job::Raider;
                marker(npc->WorldPosition(), raider ? 1.0f : .3f, raider ? .2f : .45f, .2f, 3, false);
            }
        }
        for (const auto* enemy : firstLevel.Enemies())
        {
            if (!worldMap && enemy->health > 0 && Distance2D(enemy->WorldPosition(), PlayerPosition()) < 25)
            {
                marker(enemy->WorldPosition(), .9f, .28f, .15f, 3, false);
            }
        }
        if (const Actor* goal = firstLevel.Scene().Find(harbor.ObjectiveTarget()))
        {
            marker(goal->WorldPosition(), 1, .85f, .15f, 8 + std::sin(elapsed * 4), true);
        }
        marker(PlayerPosition(), 1, 1, 1, 6, true);
        std::wstring title = worldMap ? L"\uc804\uccb4 \uc9c0\ub3c4 " + std::to_wstring(MapSize) + L"x" +
                                            std::to_wstring(MapSize) + L" [M]"
                                      : L"\uc8fc\ubcc0 \uc9c0\ub3c4 64x64 [M]";
        DrawText(1040, 38, title.c_str(), .97f, .89f, .67f);
        DrawText(1040, 290, L"\uae08\ube5b \ubaa9\ud45c / \ud770\uc0c9 \ub098", .97f, .9f, .7f);
        DrawText(1040,
                 313,
                 worldMap ? L"\ubd89\uc740 \uac70\uc810: \uc57d\ud0c8\uc790"
                          : L"\uc804\uccb4 \uc6d4\ub4dc \uc911 \uc77c\ubd80",
                 .83f,
                 .87f,
                 .71f);
    }

    float DrawWrapped(float x, float y, const std::wstring& text, float width, int maxLines = 2)
    {
        Perf::Scope scope(Perf::Cpu::Text);
        // Use actual cached glyph advances and a bounded line budget, not approximate Korean column counts.
        std::wstring line;
        float used = 0;
        int lines = 0;
        for (size_t i = 0; i < text.size(); ++i)
        {
            wchar_t ch = text[i];
            float advance = ch == L'\n' ? 0 : renderer->TextAdvance(ch, fontDC, fontHandle);
            if (ch == L'\n' || used + advance > width)
            {
                if (lines + 1 == maxLines)
                {
                    while (!line.empty() &&
                           renderer->TextWidth((line + L"...").c_str(), fontDC, fontHandle) > width)
                    {
                        line.pop_back();
                    }
                    line += L"...";
                    DrawText(x, y, line.c_str());
                    return float(lines + 1) * TextLineHeight;
                }
                DrawText(x, y, line.c_str());
                y += TextLineHeight;
                ++lines;
                line.clear();
                used = 0;
                if (ch == L'\n')
                {
                    continue;
                }
            }
            line += ch;
            used += advance;
        }
        if (!line.empty())
        {
            DrawText(x, y, line.c_str());
            ++lines;
        }
        return float(lines) * TextLineHeight;
    }

    std::wstring TravelHint(const Harbor::ObjectiveGuide& guide)
    {
        const Actor* target = firstLevel.Scene().Find(guide.target);
        if (!target)
        {
            return L"\ud070\uae38\uc744 \ub530\ub77c \uc8fc\ubcc0\uc744 \ud0d0\ud5d8\ud558\uc790.";
        }
        if (guide.ready)
        {
            if (guide.step == Harbor::GatherHides)
            {
                return L"\uc0ac\ub0e5\ud130\uc5d0 \ub3c4\ucc29\ud588\ub2e4. \ud070\uae38 \uc8fc\ubcc0\uc5d0\uc11c \uc0ac\uc2b4\uc744 \ucc3e\uc544\ubcf4\uc790.";
            }
            return guide.interactionKey ? L"\ub3c4\ucc29\ud588\ub2e4. " + guide.action : guide.action;
        }
        if (Distance2D(target->WorldPosition(), PlayerPosition()) < 1.8f)
        {
            if (guide.step == Harbor::OpenSluice && !guide.interactionKey)
            {
                return guide.action;
            }
            return firstLevel.ClearLine(PlayerPosition(), target->WorldPosition())
                       ? L"\uc870\uae08\ub9cc \ub354 \uac00\uae4c\uc774 \uac00\uc790. " + guide.action
                       : L"\ub2f4\uc774\ub098 \uac74\ubb3c \ubc18\ub300\ud3b8\uc5d0 \uc788\ub2e4. \uae38\uc744 \ub530\ub77c \ub3cc\uc544\uac00\uc790.";
        }
        float dx = ((target->WorldPosition().x - target->WorldPosition().y) -
                    (PlayerPosition().x - PlayerPosition().y)) *
                   TileWidth * .5f;
        float dy = ((target->WorldPosition().x + target->WorldPosition().y) -
                    (PlayerPosition().x + PlayerPosition().y)) *
                   TileHeight * .5f;
        std::wstring direction;
        if (std::abs(dx) > std::abs(dy) * 2)
        {
            direction = dx < 0 ? L"A \uc67c\ucabd" : L"D \uc624\ub978\ucabd";
        }
        else if (std::abs(dy) > std::abs(dx) * 2)
        {
            direction = dy < 0 ? L"W \uc704\ucabd" : L"S \uc544\ub798\ucabd";
        }
        else
        {
            direction =
                dy < 0 ? (dx < 0 ? L"W+A \uc67c\ucabd \uc704" : L"W+D \uc624\ub978\ucabd \uc704")
                       : (dx < 0 ? L"S+A \uc67c\ucabd \uc544\ub798" : L"S+D \uc624\ub978\ucabd \uc544\ub798");
        }
        return guide.destination + L" \u00b7 \ud654\uba74 " + direction + L" \ubc29\ud5a5 \u00b7 \uc57d " +
               std::to_wstring(int(std::ceil(Distance2D(target->WorldPosition(), PlayerPosition())))) +
               L"\ud0c0\uc77c (\ub9c9\ud788\uba74 \uae38\uc744 \ub530\ub77c \ub3cc\uc544\uac00\uc790)";
    }

    void DrawQuestTracker()
    {
        const auto guide = harbor.Guide();
        renderer->SetMaterial(0);
        renderer->DrawSolidRect(390, 166, QuestPanelWidth, QuestPanelHeight, .035f, .055f, .06f, .94f);
        DrawText(32, 142, L"\uc9c0\uae08 \ud560 \uc77c \u00b7 H \uc790\uc138\ud788", 1, .83f, .4f);
        DrawWrapped(32, 164, guide.action, 716, 1);
        DrawWrapped(32, 188, TravelHint(guide), 716, 1);
        if (guide.ready && guide.interactionKey)
        {
            renderer->DrawSolidRect(701, 138, 94, 24, .22f, .36f, .18f, 1);
            std::wstring key = L"[";
            key += wchar_t(guide.interactionKey);
            key += L"] \ub204\ub974\uae30";
            DrawText(662, 144, key.c_str(), 1, .94f, .55f);
        }
    }

    void DrawStoryIntro()
    {
        const wchar_t* titles[] = {L"\uc81c1\uc77c \uc0c8\ubcbd \u00b7 \ubb34\uc5f0\ud56d",
                                   L"\ube44\uac00 \uadf8\uce5c \uc7a5\ud130",
                                   L"\uac00\uc8fd \ub124 \uc7a5"};
        const wchar_t* stories[] = {
            L"\uccad\ud608\ubcd1\uc774 \ubc88\uc9c4 \ub4a4 \uc57d\uac12\uc740 \uc62c\ub790\uace0 \uc77c\uac10\uc740 \uc904\uc5c8\ub2e4. \ubb34\uc5f0\ud56d\uc5d0 \ub3c4\ucc29\ud55c \ub2f9\uc2e0\ub3c4 \uc57d\uac12\uacfc \ub07c\ub2c8\ub97c \ubc8c \uc77c\uc774 \ud544\uc694\ud558\ub2e4.",
            L"\ubc24\uc0c8 \ube44\uac00 \uc3df\uc544\uc84c\ub2e4. \uc7a5\ud130 \uc0ac\ub78c\ub4e4\uc740 \uc816\uc740 \ud3ec\ub300\ub97c \ud587\ubcd5\uc5d0 \ub110\uace0, \uc57d \uc0c1\uc790\ub97c \ub9c8\ub978 \uc218\ub808\ub85c \uc62e\uae34\ub2e4. \uc9d0\uafbc \ud558\ub098\uac00 \ub04a\uc5b4\uc9c4 \ub048\uc744 \ub4e4\uace0 \uc7a5\ud130 \uac8c\uc2dc\ud310 \uc55e\uc73c\ub85c \uac04\ub2e4.",
            L"\uac8c\uc2dc\ud310\uc5d0 \uc0c8 \uc77c\uac10\uc774 \ubd99\uc5c8\ub2e4. \uc57d \uc0c1\uc790\ub97c \ubb36\uc744 \uac00\uc8fd\uc744 \ub124 \uc7a5 \uac00\uc838\uc624\uba74 \ud488\uc0af\uc744 \uc900\ub2e4. \uba3c\uc800 \uac00\uae4c\uc774 \uac00\uc11c \ubb34\uc2a8 \uc77c\uc778\uc9c0 \uc77d\uc5b4 \ubcf4\uc790."};
        renderer->SetMaterial(0);
        renderer->DrawSolidRect(640, 400, 1280, 800, .015f, .025f, .025f, .60f);
        renderer->DrawSolidRect(640, 390, HelpPanelWidth, HelpPanelHeight, .055f, .085f, .075f, 1);
        DrawText(304, 244, titles[introPage], 1, .83f, .45f);
        DrawWrapped(304, 286, stories[introPage], 672, 9);
        std::wstring page = std::to_wstring(introPage + 1) + L" / 3";
        DrawText(930, 244, page.c_str());
        DrawText(
            304,
            548,
            introPage < 2
                ? L"ENTER: \ub2e4\uc74c \uc774\uc57c\uae30 \u00b7 H: \ubc14\ub85c \uc2dc\uc791"
                : L"ENTER: \uc7a5\ud130 \uac8c\uc2dc\ud310\uc73c\ub85c \u00b7 H: \ubc14\ub85c \uc2dc\uc791",
            .86f,
            .9f,
            .75f);
    }

    void DrawDialogue()
    {
        const auto& dialogue = harbor.Conversation();
        if (!dialogue.Active())
        {
            return;
        }
        const auto& page = dialogue.pages[dialogue.page];
        renderer->SetMaterial(0);
        renderer->DrawSolidRect(430, 624, 820, 176, .035f, .055f, .06f, .98f);
        DrawText(32, 558, page.speaker.c_str(), 1, .83f, .45f);
        DrawWrapped(32, 584, page.text, 796, 3);
        if (page.choices.empty())
        {
            DrawText(
                32,
                690,
                L"E / ENTER: \ub2e4\uc74c \ub300\ud654 \u00b7 ESC: \ub300\ud654 \uadf8\ub9cc\ub450\uae30",
                .86f,
                .9f,
                .75f);
        }
        else
        {
            for (size_t i = 0; i < page.choices.size() && i < 3; ++i)
            {
                std::wstring choice = std::to_wstring(i + 1) + L". " + page.choices[i].text;
                DrawWrapped(32, 648 + float(i) * TextLineHeight, choice, 796, 1);
            }
            DrawText(
                32,
                708,
                L"\uc22b\uc790 \ud0a4\ub85c \uc9c1\uc811 \uc120\ud0dd \u00b7 ESC: \uacb0\uc815\ud558\uc9c0 \uc54a\uace0 \ub300\ud654 \uc885\ub8cc",
                .86f,
                .9f,
                .75f);
        }
    }

    void DrawGuideHelp()
    {
        if (!helpVisible)
        {
            return;
        }
        if (introVisible)
        {
            DrawStoryIntro();
            return;
        }
        const auto guide = harbor.Guide();
        renderer->SetMaterial(0);
        renderer->DrawSolidRect(640, 400, 1280, 800, .015f, .025f, .025f, .60f);
        renderer->DrawSolidRect(640, 390, HelpPanelWidth, HelpPanelHeight, .055f, .085f, .075f, 1);
        DrawText(304,
                 244,
                 guide.step == Harbor::TakeHideJob
                     ? L"\ubb34\uc5f0\ud56d\uc5d0 \ub3c4\ucc29\ud588\ub2e4"
                     : L"\uc9c0\uae08 \ubb34\uc5c7\uc744 \ud558\uba74 \ub420\uae4c?",
                 1,
                 .83f,
                 .45f);
        DrawWrapped(304, 276, guide.reason, 672, 2);
        if (guide.step == Harbor::TakeHideJob)
        {
            DrawWrapped(304,
                        338,
                        L"\uccab \ubaa9\ud45c: " + guide.destination + L" \ucc3e\uc544\uac00\uae30",
                        672,
                        1);
            DrawWrapped(
                304,
                370,
                L"1. WASD\ub85c \uc774\ub984\uc774 \uc801\ud78c \uae08\ube5b \ud45c\uc2dd\uc744 \ub530\ub77c\uac00\uc138\uc694.",
                672,
                1);
            DrawWrapped(
                304,
                400,
                L"2. \uac00\uae4c\uc774 \uac00\uba74 \ud45c\uc2dc\ub418\ub294 \ud589\ub3d9\uc744 \ud558\uc138\uc694: " +
                    guide.action,
                672,
                1);
            DrawWrapped(
                304,
                440,
                L"\ub300\ud654\ub294 E / ENTER\ub85c \ub118\uae30\uace0 \uc22b\uc790 \ud0a4\ub85c \ub2f5\ud558\uc138\uc694. \uc218\ub77d\ud558\uae30 \uc804\uc5d0\ub294 \uc758\ub8b0\uac00 \uc2dc\uc791\ub418\uc9c0 \uc54a\uc2b5\ub2c8\ub2e4.",
                672,
                2);
        }
        else if (guide.step == Harbor::DecideLedger)
        {
            DrawWrapped(
                304,
                338,
                L"\uacf5\uac1c [\uc7a5\ud130 R] \u00b7 \ub3c4\ud558\ub97c \ubcf8 \uc0ac\ub78c\uc744 \ucc3e\uc744 \uc218 \uc788\ub2e4. \ubcf4\uc99d\uc778 \uc625\ubd84\ub3c4 \uc57d\uc744 \ube7c\ub3cc\ub838\ub2e4\ub294 \uc758\uc2ec\uc744 \ubc1b\uc744 \uc218 \uc788\ub2e4.",
                672,
                2);
            DrawWrapped(
                304,
                386,
                L"\uad00\uccad [\uc138\ub77c\ud2b8 \uacc1 T] \u00b7 30\ub0e5. \uc6d0\ubcf8\uc744 \uad00\uccad \uae30\ub85d\uacfc \ub300\uc870\ud55c\ub2e4. \ud604\uc7a5\uc740 \ub9c9\ud788\uace0 \uacf5\uac1c \uc2dc\uc810\uc740 \uad00\uccad\uc774 \uc815\ud55c\ub2e4.",
                672,
                2);
            DrawWrapped(
                304,
                434,
                L"\ud310\ub9e4 [\uc774\uc548 \uacc1 T] \u00b7 40\ub0e5. \uc6d0\ubcf8\uc740 \ubd80\ub450 \uc0c1\ud68c\ub85c \uac04\ub2e4. \uc0c1\ud68c\uac00 \uc625\ubd84\uc5d0\uac8c \uc61b \uc57d\uac12\uc744 \ubb3c\uc744 \uc218\ub3c4 \uc788\ub2e4.",
                672,
                2);
            DrawWrapped(
                304,
                482,
                L"J\uc5d0\uc11c \ub124 \uc0ac\ub78c\uc758 \uc774\uc57c\uae30\ub97c \ub2e4\uc2dc \uc77d\uc744 \uc218 \uc788\ub2e4.",
                672,
                2);
        }
        else
        {
            DrawWrapped(304, 338, L"\ucc3e\uc544\uac08 \uacf3: " + guide.destination, 672, 1);
            DrawWrapped(304, 370, L"\ud574\uc57c \ud560 \ud589\ub3d9: " + guide.action, 672, 2);
            DrawWrapped(304, 420, TravelHint(guide), 672, 2);
            DrawWrapped(
                304,
                474,
                L"\uae08\ube5b \ud45c\uc2dc\ub294 \uc9c0\uae08 \ucc3e\uc744 \ub300\uc0c1\uc785\ub2c8\ub2e4. \ub3c4\ucc29\ud558\uba74 \ud654\uba74\uc5d0 \ub204\ub97c \ud0a4\uac00 \ud45c\uc2dc\ub429\ub2c8\ub2e4.",
                672,
                2);
        }
        DrawText(
            304,
            548,
            L"ENTER \ub610\ub294 H: \uac8c\uc784\uc73c\ub85c \u00b7 \uc124\uba85\uc744 \uc77d\ub294 \ub3d9\uc548 \uc2dc\uac04\uc740 \uba48\ucda5\ub2c8\ub2e4.",
            .86f,
            .9f,
            .75f);
    }

    void DrawHud()
    {
        const auto& stats = firstLevel.stats;
        renderer->SetMaterial(0);
        renderer->DrawSolidRect(510, 61, 980, 102, .035f, .055f, .06f, .94f);
        DrawText(35,
                 38,
                 L"\ubb34\uc5f0\ud56d \u00b7 \ubb3c\uae38 \uc544\ub798\uc758 \uc774\ub984\ub4e4",
                 .92f,
                 .76f,
                 .48f);
        DrawText(785, 38, harbor.Clock().c_str(), .84f, .82f, .7f);
        std::wstring status = L"\ub808\ubca8 " + std::to_wstring(stats.level) + L"   \uccb4\ub825 " +
                              std::to_wstring(stats.health) + L"/" + std::to_wstring(stats.MaxHealth()) +
                              L"   \uacf5\uaca9 " + std::to_wstring(stats.Attack()) + L"   \ubc29\uc5b4 " +
                              std::to_wstring(stats.Defense());
        DrawText(35, 65, status.c_str());
        float hp = 260.0f * stats.health / stats.MaxHealth();
        renderer->DrawSolidRect(165, 84, 260, 7, .18f, .13f, .12f, 1);
        if (hp > 0)
        {
            renderer->DrawSolidRect(35 + hp * .5f, 84, hp, 7, .66f, .28f, .21f, 1);
        }
        std::wstring xp = L"\uacbd\ud5d8\uce58 " + std::to_wstring(stats.experience) + L"/" +
                          std::to_wstring(stats.RequiredExperience());
        DrawText(325, 95, xp.c_str());
        float xpWidth = stats.level == Level::MaximumLevel
                            ? 260.0f
                            : 260.0f * stats.experience / stats.RequiredExperience();
        renderer->DrawSolidRect(690, 84, 260, 7, .16f, .18f, .17f, 1);
        if (xpWidth > 0)
        {
            renderer->DrawSolidRect(560 + xpWidth * .5f, 84, xpWidth, 7, .45f, .66f, .47f, 1);
        }
        std::wstring points = L"I / TAB \uac00\ubc29 \u00b7 \uc810\uc218 " + std::to_wstring(stats.points);
        DrawText(780, 65, points.c_str(), .97f, .8f, .49f);

        if (!journalPanel && !harbor.Conversation().Active())
        {
            renderer->DrawSolidRect(390, 583, 740, 54, .035f, .055f, .06f, .93f);
            DrawWrapped(35, 573, harbor.AreaName(), 710, 1);
            if (Npc* npc = harbor.Nearest(2.2f))
            {
                std::wstring info = npc->name + L" \u00b7 " + Harbor::World::ActionLabel(npc->action) +
                                    L"  [E \ub300\ud654 / T \uac70\ub798]  \uccb4\ub825 " +
                                    std::to_wstring(npc->health);
                DrawWrapped(35, 603, info, 710, 1);
            }
            if (firstLevel.messageTime > 0)
            {
                renderer->DrawSolidRect(430, 672, 820, 80, .035f, .05f, .055f, .97f);
                DrawWrapped(32, 650, firstLevel.message, 796, 3);
            }
        }
        std::wstring inventory = L"\ud68c\ubcf5\uc57d " + std::to_wstring(stats.potions) +
                                 L"   \uc5fd\uc804 " + std::to_wstring(stats.coins) + L"   \uac00\uc8fd " +
                                 std::to_wstring(stats.hides) + L"   \ucc98\uce58 " +
                                 std::to_wstring(stats.kills);
        DrawWrapped(35, 744, inventory, 950, 1);
        DrawWrapped(
            28,
            777,
            L"WASD \uc774\ub3d9  E \ub300\ud654\u00b7\uc90d\uae30  F \uc870\uc0ac  H \uc9c0\uae08 \ud560 \uc77c  SPACE \uacf5\uaca9  Q \uc57d  M \uc9c0\ub3c4  J \uc77c\uc9c0  I \uac00\ubc29  ESC",
            1220,
            1);
    }

    const wchar_t* InventoryName(Inventory::Item item)
    {
        switch (item)
        {
        case Inventory::Item::Potion:
            return L"\ud68c\ubcf5\uc57d";
        case Inventory::Item::Coins:
            return L"\uc5fd\uc804";
        case Inventory::Item::Hide:
            return L"\uac00\uc8fd";
        case Inventory::Item::Ledger:
            return L"\uc61b \uc7a5\ubd80";
        case Inventory::Item::Key:
            return L"\uc218\ubb38 \uc5f4\uc1e0";
        case Inventory::Item::LedgerCopy:
            return L"\uc7a5\ubd80 \uc0ac\ubcf8";
        default:
            return L"\ube48 \uce78";
        }
    }

    const wchar_t* InventoryDescription(Inventory::Item item)
    {
        switch (item)
        {
        case Inventory::Item::Potion:
            return L"\uc4f4 \uc57d\uc774\uc9c0\ub9cc \uc0c1\ucc98\uc5d0\ub294 \uc798 \ub4e4\uc5b4\uc694. \uccb4\ub825\uc744 45\ub9cc\ud07c \ud68c\ubcf5\ud574\uc694.";
        case Inventory::Item::Coins:
            return L"\uc7a5\ud130\uc5d0\uc11c \ubb3c\uac74\uc744 \uc0ac\uace0\ud314 \ub54c \uc4f0\ub294 \ub3c8\uc774\uc5d0\uc694.";
        case Inventory::Item::Hide:
            return L"\uc0ac\ub0e5\ud574\uc11c \uc5bb\uc740 \uac00\uc8fd\uc774\uc5d0\uc694. \ub9c8\uc744 \uc0c1\uc778\uc5d0\uac8c \ud314 \uc218 \uc788\uc5b4\uc694.";
        case Inventory::Item::Ledger:
            return L"\ubb34\uc5f0\ud56d\uc758 \uc61b \uc774\ub984\ub4e4\uc774 \uc801\ud600 \uc788\uc5b4\uc694. \ub204\uad6c\uc5d0\uac8c \ub9e1\uae38\uc9c0\ub294 \ub2f9\uc2e0 \ubaab\uc774\uc5d0\uc694.";
        case Inventory::Item::Key:
            return L"\uc61b \uc218\ubb38\uc758 \uc7a0\uae08\uc7a5\uce58\ub97c \uc5ec\ub294 \uc5f4\uc1e0\uc608\uc694. \uc218\ubb38\uc5d0\uc11c F\ub85c \uc870\uc0ac\ud574 \ubcf4\uc138\uc694.";
        case Inventory::Item::LedgerCopy:
            return L"\uc6d0\ubcf8\uc744 \ub118\uaca8\ub3c4 \ub0a8\uaca8 \ub458 \uc218 \uc788\ub294 \uae30\ub85d\uc774\uc5d0\uc694. \uc801\ud78c \uc774\ub984\uc744 \uc78a\uc9c0 \ub9c8\uc138\uc694.";
        default:
            return L"\uc544\uc9c1 \ube44\uc5b4 \uc788\uc5b4\uc694. \uc804\ub9ac\ud488\uc740 \uac00\uae4c\uc774 \ub2e4\uac00\uac00 E\ub85c \ucc59\uae38 \uc218 \uc788\uc5b4\uc694.";
        }
    }

    std::array<Inventory::Stack, Inventory::SlotCount> InventorySlots()
    {
        return Inventory::Snapshot(firstLevel.stats,
                                   harbor.PlayerOwnsLedger(),
                                   harbor.PlayerOwnsKey(),
                                   harbor.PlayerHasLedgerCopy());
    }

    void DrawInventoryIcon(Inventory::Item item, float x, float y)
    {
        if (item == Inventory::Item::Potion)
        {
            renderer->DrawSolidRect(x, y, 22, 29, .30f, .65f, .48f, 1);
            renderer->DrawSolidRect(x, y - 18, 12, 9, .67f, .54f, .33f, 1);
            renderer->DrawSolidRect(x, y + 1, 16, 11, .88f, .86f, .66f, 1);
            renderer->DrawSolidRect(x - 6, y - 5, 3, 13, .75f, .91f, .76f, .8f);
        }
        else if (item == Inventory::Item::Coins)
        {
            renderer->DrawEllipse(x, y, 32, 29, .85f, .62f, .22f, 1);
            renderer->DrawEllipse(x, y, 24, 22, 1, .79f, .34f, 1);
            renderer->DrawSolidRect(x, y, 7, 7, .16f, .19f, .17f, 1);
        }
        else if (item == Inventory::Item::Key)
        {
            renderer->DrawEllipse(x - 9, y - 7, 18, 18, .88f, .72f, .37f, 1);
            renderer->DrawEllipse(x - 9, y - 7, 9, 9, .12f, .17f, .17f, 1);
            renderer->DrawSolidRect(x + 5, y + 1, 23, 5, .88f, .72f, .37f, 1);
            renderer->DrawSolidRect(x + 14, y + 5, 5, 10, .88f, .72f, .37f, 1);
        }
        else if (item == Inventory::Item::Ledger || item == Inventory::Item::LedgerCopy)
        {
            renderer->DrawSolidRect(x, y, 27, 34, .73f, .68f, .48f, 1);
            renderer->DrawSolidRect(x - 11, y, 4, 34, .29f, .39f, .41f, 1);
            for (int line = 0; line < 4; ++line)
            {
                renderer->DrawSolidRect(x + 2, y - 9 + float(line) * 6, 15, 2, .28f, .29f, .24f, 1);
            }
        }
        else if (item == Inventory::Item::Hide)
        {
            renderer->DrawModelPreview("loot2", x, y, 43, 37);
        }
    }

    void DrawStatsPanel()
    {
        if (!statsPanel)
        {
            return;
        }
        Perf::Scope scope(Perf::Cpu::InventoryUI);
        const auto& stats = firstLevel.stats;
        const auto slots = InventorySlots();
        renderer->SetMaterial(0);
        renderer->DrawSolidRect(640, 400, 1280, 800, .015f, .025f, .03f, .72f);
        renderer->DrawSolidRect(640, 420, 1004, 604, .46f, .39f, .25f, 1);
        renderer->DrawSolidRect(640, 420, 1000, 600, .055f, .085f, .09f, .99f);
        renderer->DrawSolidRect(640, 180, 950, 1, .42f, .38f, .26f, 1);
        renderer->DrawSolidRect(300, 440, 274, 490, .035f, .057f, .065f, 1);
        renderer->DrawEllipse(300, 395, 100, 20, .1f, .15f, .15f, 1);
        renderer->DrawModelPreview("hero_idle", 300, 306, 190, 192, 1);
        renderer->DrawSolidRect(300, 464, 214, 10, .18f, .21f, .20f, 1);
        const float hpWidth = 210.0f * stats.health / stats.MaxHealth();
        if (hpWidth > 0)
        {
            renderer->DrawSolidRect(195 + hpWidth * .5f, 464, hpWidth, 6, .62f, .33f, .28f, 1);
        }
        renderer->DrawSolidRect(300, 562, 214, 8, .18f, .21f, .20f, 1);
        const float xpWidth = stats.level == Level::MaximumLevel
                                  ? 210.0f
                                  : 210.0f * stats.experience / stats.RequiredExperience();
        if (xpWidth > 0)
        {
            renderer->DrawSolidRect(195 + xpWidth * .5f, 562, xpWidth, 4, .46f, .62f, .47f, 1);
        }
        for (int index = 0; index < Inventory::SlotCount; ++index)
        {
            const float x = Inventory::GridX + float(index % Inventory::Columns) * Inventory::SlotStride;
            const float y = Inventory::GridY + float(index / Inventory::Columns) * Inventory::SlotStride;
            const bool selected = index == inventorySelection;
            renderer->DrawSolidRect(x + 40,
                                    y + 40,
                                    84,
                                    84,
                                    selected ? .80f : .26f,
                                    selected ? .67f : .31f,
                                    selected ? .37f : .29f,
                                    1);
            renderer->DrawSolidRect(x + 40, y + 40, 80, 80, .075f, .11f, .115f, 1);
            DrawInventoryIcon(slots[size_t(index)].item, x + 40, y + 27);
            Perf::Count(Perf::Counter::InventorySlotsDrawn);
            if (slots[size_t(index)].quantity > 0)
            {
                Perf::Count(Perf::Counter::InventoryStacksDrawn);
            }
        }
        renderer->DrawSolidRect(795, 639, 610, 90, .035f, .057f, .065f, 1);
        renderer->DrawSolidRect(1040, 674, 118, 27, .25f, .34f, .29f, 1);

        // Text follows all icons so the atlas/font texture does not change for every slot.
        DrawText(166, 158, L"\uc18c\uc9c0\ud488\uacfc \ub0b4 \ubab8 \uc0c1\ud0dc", .98f, .83f, .52f);
        DrawText(860, 158, L"I / TAB / ESC  \ub2eb\uae30", .73f, .79f, .76f);
        DrawText(1090, 158, L"X", .98f, .83f, .52f);
        DrawText(
            188, 211, (L"\ub5a0\ub3cc\uc774  \u00b7  \ub808\ubca8 " + std::to_wstring(stats.level)).c_str());
        DrawText(188, 419, L"\uc190\uc5d0 \ub4e0 \uce7c", .72f, .78f, .75f);
        DrawText(
            188,
            451,
            (L"\uccb4\ub825  " + std::to_wstring(stats.health) + L" / " + std::to_wstring(stats.MaxHealth()))
                .c_str());
        DrawText(188, 495, (L"\uacf5\uaca9\ub825  " + std::to_wstring(stats.Attack())).c_str());
        DrawText(188, 520, (L"\ubc29\uc5b4\ub825  " + std::to_wstring(stats.Defense())).c_str());
        DrawText(188,
                 550,
                 (L"\uacbd\ud5d8\uce58  " + std::to_wstring(stats.experience) + L" / " +
                  std::to_wstring(stats.RequiredExperience()))
                     .c_str());
        DrawText(188,
                 592,
                 (L"\ub0a8\uc740 \ub2a5\ub825 \uc810\uc218  " + std::to_wstring(stats.points)).c_str(),
                 .95f,
                 .80f,
                 .46f);
        DrawText(188, 620, L"1 \ud798   2 \uccb4\ub825   3 \ub9f7\uc9d1");
        DrawWrapped(188,
                    647,
                    L"\uc810\uc218\ub97c \uc368\uc11c \ub2a5\ub825\uc744 \uc62c\ub824 \ubcf4\uc138\uc694.",
                    220,
                    2);
        DrawText(
            490,
            194,
            L"\uac00\ubc29  \u00b7  \uac19\uc740 \ubb3c\uac74\uc740 \ud55c \uce78\uc5d0 \ubaa8\uc544\uc694.",
            .82f,
            .85f,
            .75f);
        for (int index = 0; index < Inventory::SlotCount; ++index)
        {
            const auto stack = slots[size_t(index)];
            if (stack.quantity <= 0)
            {
                continue;
            }
            const float x = Inventory::GridX + float(index % Inventory::Columns) * Inventory::SlotStride;
            const float y = Inventory::GridY + float(index / Inventory::Columns) * Inventory::SlotStride;
            DrawText(x + 6, y + 58, InventoryName(stack.item), .87f, .88f, .77f);
            const std::wstring count = stack.quantity > 9999 ? L"9999+" : std::to_wstring(stack.quantity);
            DrawText(x + 74 - renderer->TextWidth(count.c_str(), fontDC, fontHandle),
                     y + 75,
                     count.c_str(),
                     1,
                     .84f,
                     .48f);
        }
        const auto selected = slots[size_t(inventorySelection)];
        DrawText(
            502,
            613,
            (std::wstring(InventoryName(selected.item)) + L"  " + std::to_wstring(selected.quantity)).c_str(),
            .98f,
            .84f,
            .52f);
        DrawWrapped(502, 637, InventoryDescription(selected.item), 580, 2);
        DrawText(992,
                 679,
                 selected.item == Inventory::Item::Potion ? L"E / ENTER \uc0ac\uc6a9"
                                                          : L"Q  \ud68c\ubcf5\uc57d",
                 .87f,
                 .91f,
                 .79f);
        if (inventoryActionNote.empty())
        {
            DrawText(
                490,
                705,
                L"\ud074\ub9ad / WASD / \ubc29\ud5a5\ud0a4\ub85c \uc120\ud0dd  \u00b7  \ud68c\ubcf5\uc57d\uc740 Q\ub85c\ub3c4 \ub9c8\uc2e4 \uc218 \uc788\uc5b4\uc694.",
                .72f,
                .79f,
                .75f);
        }
        else
        {
            DrawWrapped(490, 705, inventoryActionNote, 610, 1);
        }
    }

    Inventory::Item TradeIcon(Harbor::TradeAction action)
    {
        switch (action)
        {
        case Harbor::TradeAction::BuyPotion:
            return Inventory::Item::Potion;
        case Harbor::TradeAction::SellHide:
            return Inventory::Item::Hide;
        case Harbor::TradeAction::BuyLedger:
        case Harbor::TradeAction::SellLedger:
            return Inventory::Item::Ledger;
        case Harbor::TradeAction::GiveKey:
            return Inventory::Item::Key;
        default:
            return Inventory::Item::Coins;
        }
    }

    void DrawTradePanel()
    {
        if (!harbor.Trading().Active())
        {
            return;
        }
        Perf::Scope scope(Perf::Cpu::TradeUI);
        const auto offers = harbor.TradeOffers();
        const auto* npc = dynamic_cast<const Npc*>(firstLevel.Scene().Find(harbor.Trading().partner));
        renderer->SetMaterial(0);
        renderer->DrawSolidRect(640, 400, 1280, 800, .015f, .025f, .03f, .65f);
        renderer->DrawSolidRect(640, 410, 944, 584, .46f, .39f, .25f, 1);
        renderer->DrawSolidRect(640, 410, 940, 580, .055f, .085f, .09f, .99f);
        renderer->DrawSolidRect(640, 211, 890, 1, .42f, .38f, .26f, 1);
        renderer->DrawSolidRect(905, 405, 370, 350, .035f, .057f, .065f, 1);
        const int selected = offers.empty() ? -1 : (std::min)(tradeSelection, int(offers.size()) - 1);
        for (size_t i = 0; i < offers.size(); ++i)
        {
            const float y = Shop::RowsY + float(i) * Shop::RowStride;
            renderer->DrawSolidRect(440,
                                    y + 32,
                                    484,
                                    68,
                                    int(i) == selected ? .80f : .26f,
                                    int(i) == selected ? .67f : .31f,
                                    .29f,
                                    1);
            renderer->DrawSolidRect(440, y + 32, 480, 64, .075f, .11f, .115f, 1);
            Perf::Count(Perf::Counter::ShopRowsDrawn);
        }
        for (const auto button : {Shop::Less, Shop::More, Shop::Maximum, Shop::Confirm, Shop::Close})
        {
            const bool confirm = button.y == Shop::Confirm.y;
            const bool enabled = selected >= 0 && offers[size_t(selected)].maximum > 0;
            renderer->DrawSolidRect(button.x + button.width * .5f,
                                    button.y + button.height * .5f,
                                    button.width,
                                    button.height,
                                    confirm && tradeConfirmPending ? .48f : .18f,
                                    enabled ? .31f : .20f,
                                    .24f,
                                    1);
        }
        if (selected >= 0)
        {
            if (offers[size_t(selected)].action == Harbor::TradeAction::BuyMeal)
            {
                renderer->DrawEllipse(905, 314, 46, 25, .39f, .33f, .25f, 1);
                renderer->DrawEllipse(905, 304, 50, 17, .74f, .66f, .48f, 1);
                renderer->DrawEllipse(905, 304, 40, 11, .52f, .32f, .15f, 1);
            }
            else
            {
                DrawInventoryIcon(TradeIcon(offers[size_t(selected)].action), 905, 308);
            }
        }
        DrawText(195,
                 157,
                 (npc ? npc->name + L" \u00b7 \uac70\ub798" : L"\uac70\ub798").c_str(),
                 .98f,
                 .83f,
                 .52f);
        DrawText(1074, 159, L"X");
        DrawText(195,
                 190,
                 (L"\ub0b4 \ub3c8 " + std::to_wstring(firstLevel.stats.coins) +
                  L"\ub0e5    \ud68c\ubcf5\uc57d " + std::to_wstring(firstLevel.stats.potions) +
                  L"\ubcd1    \uac00\uc8fd " + std::to_wstring(firstLevel.stats.hides) + L"\uc7a5")
                     .c_str());
        DrawText(740,
                 190,
                 (L"\uc0c1\ub300\uc758 \ub3c8 " + std::to_wstring(npc ? npc->coins : 0) + L"\ub0e5").c_str());
        DrawText(200,
                 232,
                 L"\uac70\ub798\ud560 \ubb3c\uac74\uc744 \uace8\ub77c \uc8fc\uc138\uc694.",
                 .82f,
                 .85f,
                 .75f);
        for (size_t i = 0; i < offers.size(); ++i)
        {
            const auto& offer = offers[i];
            const float y = Shop::RowsY + float(i) * Shop::RowStride;
            DrawText(213, y + 24, offer.name.c_str(), offer.maximum > 0 ? .96f : .64f, .83f, .65f);
            const std::wstring line =
                (Harbor::IsSale(offer.action) ? L"\ubc1b\ub294 \ub3c8 " : L"\ub0b4\ub294 \ub3c8 ") +
                std::to_wstring(offer.price) + L"\ub0e5 / 1\uac1c    " +
                (Harbor::IsSale(offer.action) ? L"\ub0b4 \ubcf4\uc720 " : L"\uc7ac\uace0 ") +
                std::to_wstring(offer.stock);
            DrawText(213, y + 49, line.c_str(), .72f, .79f, .75f);
        }
        if (selected >= 0)
        {
            const auto& offer = offers[size_t(selected)];
            const int quantity = Shop::Quantity(tradeQuantity, offer.maximum);
            DrawWrapped(740, 260, offer.name, 330, 1);
            DrawWrapped(740, 357, offer.description, 330, 3);
            DrawText(740,
                     435,
                     (L"\uac70\ub798 \uac00\ub2a5 \uc218\ub7c9: " + std::to_wstring(offer.maximum)).c_str());
            if (!offer.unavailable.empty())
            {
                DrawWrapped(740, 463, offer.unavailable, 330, 2);
            }
            const std::wstring total =
                (Harbor::IsSale(offer.action) ? L"\ubc1b\uc744 \ub3c8: " : L"\ub0bc \ub3c8: ") +
                std::to_wstring(offer.price * quantity) + L"\ub0e5";
            DrawText(740, 559, total.c_str(), .98f, .83f, .52f);
            DrawText(837, 517, std::to_wstring(quantity).c_str());
            DrawText(760, 517, L"-");
            DrawText(910, 517, L"+");
            DrawText(990, 517, L"\ucd5c\ub300");
            DrawText(752,
                     611,
                     tradeConfirmPending  ? L"ENTER / \ud074\ub9ad: \uc815\ub9d0 \ub118\uae30\uae30"
                     : offer.maximum <= 0 ? L"\uc9c0\uae08\uc740 \uac70\ub798\ud560 \uc218 \uc5c6\uc74c"
                     : Harbor::IsSale(offer.action)
                         ? L"ENTER / \ud074\ub9ad: \uc120\ud0dd\ud55c \uc218\ub7c9 \ud310\ub9e4"
                     : offer.action == Harbor::TradeAction::GiveKey
                         ? L"ENTER / \ud074\ub9ad: \uc5f4\uc1e0 \ub9e1\uae30\uae30"
                         : L"ENTER / \ud074\ub9ad: \uc120\ud0dd\ud55c \uc218\ub7c9 \uad6c\ub9e4");
        }
        else
        {
            DrawWrapped(
                200,
                274,
                L"\uc9c0\uae08 \uc774 \uc0ac\ub78c\uacfc \uac70\ub798\ud560 \uc218 \uc788\ub294 \ubb3c\uac74\uc774 \uc5c6\ub2e4.",
                470,
                2);
        }
        DrawWrapped(
            195,
            657,
            tradeConfirmPending
                ? L"\uc774 \uc120\ud0dd\uc740 \uc774\uc57c\uae30\uc758 \uc9c4\ud589\uc744 \ubc14\uafbc\ub2e4. \ud55c \ubc88 \ub354 \ud655\uc778\ud558\uba74 \uc6d0\ubcf8\uc774\ub098 \uc5f4\uc1e0\ub97c \ub118\uae34\ub2e4."
                : harbor.Trading().message,
            890,
            1);
        DrawText(
            195,
            685,
            L"W/S\u00b7\u2191/\u2193 \uc120\ud0dd   A/D\u00b7\u2190/\u2192 \uc218\ub7c9   ENTER \uad6c\ub9e4\u00b7\ud310\ub9e4   T / ESC \ub2eb\uae30",
            .72f,
            .79f,
            .75f);
    }

    void DrawJournal()
    {
        if (!journalPanel)
        {
            return;
        }
        renderer->DrawSolidRect(510, 496, 980, 442, .03f, .055f, .065f, .98f);
        float y = 305;
        for (const auto& line : harbor.Journal())
        {
            int budget = (std::min)(3, int((680 - y) / TextLineHeight));
            if (budget <= 0)
            {
                break;
            }
            y += DrawWrapped(38, y, line, 938, budget) + 5;
        }
        DrawText(
            38,
            705,
            L"J \ub2eb\uae30 \u00b7 \uc9c0\ub098\uac04 \uc0ac\uac74\uacfc \ub0b4 \uc120\ud0dd\uc758 \uacb0\uacfc",
            .8f,
            .83f,
            .72f);
    }

    void DrawPausePanel()
    {
        if (paused)
        {
            renderer->DrawSolidRect(640, 400, 390, 90, .03f, .05f, .06f, .96f);
            DrawText(498, 405, L"\uc7a0\uc2dc \uc26c\ub294 \uc911 - ESC\ub85c \ub3cc\uc544\uac00\uae30");
        }
    }

    struct LabelBounds
    {
        float left, top, right, bottom;
    };

    std::vector<LabelBounds> worldLabelBounds;

    bool ReserveWorldLabel(float x, float y, float width, float height)
    {
        LabelBounds bounds = {x - width * .5f, y - height * .5f, x + width * .5f, y + height * .5f};
        bool blocked = paused || helpVisible || harbor.Conversation().Active() || harbor.Trading().Active() ||
                       journalPanel || statsPanel || bounds.left < 20 || bounds.right > 1000 ||
                       bounds.top < 226 || bounds.bottom > 540;
        for (const auto& other : worldLabelBounds)
        {
            blocked = blocked || (bounds.left < other.right + 4 && bounds.right + 4 > other.left &&
                                  bounds.top < other.bottom + 4 && bounds.bottom + 4 > other.top);
        }
        if (blocked)
        {
            Perf::Count(Perf::Counter::UiLabelsCulled);
            return false;
        }
        worldLabelBounds.push_back(bounds);
        return true;
    }

    bool CompactNpcMarker(const Actor& marker)
    {
        const Actor* target = firstLevel.Scene().Find(marker.ParentId());
        return target && target->Kind() == ActorKind::Npc &&
               Distance2D(target->WorldPosition(), PlayerPosition()) < NpcNameDistance;
    }

    void QuestMarkerPosition(const Actor& marker, float& x, float& y, bool& edge)
    {
        if (CompactNpcMarker(marker))
        {
            const Actor& target = firstLevel.Scene().Get(marker.ParentId());
            WorldToScreen(target.WorldPosition(), x, y);
            y -= target.visualHeight + 30;
            edge = false;
            return;
        }
        WorldToScreen(marker.WorldPosition(), x, y);
        edge = x < 180 || x > 850 || y < 248 || y > 500;
        x = (std::max)(180.0f, (std::min)(850.0f, x));
        y = (std::max)(248.0f, (std::min)(500.0f, y));
    }

    void RenderOverlay()
    {
        Perf::Scope scope(Perf::Cpu::RenderUI);
        renderer->SetSurfaceData({{0, 0, -1, 0}});
        worldLabelBounds.clear();
        const Npc* focusedNpc = harbor.Nearest(NpcNameDistance);
        const ActorId focusedNpcId = focusedNpc ? focusedNpc->Id() : InvalidActorId;
        if (const Actor* marker = firstLevel.Scene().Find(questMarkerId))
        {
            if (marker->IsVisible())
            {
                float x, y;
                bool edge;
                QuestMarkerPosition(*marker, x, y, edge);
                if (CompactNpcMarker(*marker))
                {
                    worldLabelBounds.push_back({x - 8, y - 8, x + 8, y + 8});
                }
                else
                {
                    worldLabelBounds.push_back({x - 145, y - 17, x + 145, y + 42});
                }
            }
        }
        renderer->DrawScene(
            firstLevel.Scene(),
            ScenePass::Overlay,
            [&](const Actor& actor)
            {
                UiPart part = UiPart(actor.variant);
                if (part == UiPart::QuestMarker)
                {
                    if (paused || helpVisible || harbor.Conversation().Active() ||
                        harbor.Trading().Active() || journalPanel || statsPanel)
                    {
                        return;
                    }
                    const Actor* goal = firstLevel.Scene().Find(actor.ParentId());
                    if (!goal)
                    {
                        return;
                    }
                    const auto guide = harbor.Guide();
                    float x, y;
                    bool offscreen;
                    QuestMarkerPosition(actor, x, y, offscreen);
                    if (CompactNpcMarker(actor))
                    {
                        // Nearby characters stay unobscured; the tracker/prompt still shows the action.
                        if (y > 236 && y < 530)
                        {
                            renderer->DrawSolidDiamond(x, y, 10, 12, 1, .84f, .35f, 1);
                        }
                        return;
                    }
                    renderer->DrawEllipse(
                        x, y, 24 + std::sin(elapsed * 4) * 4, 24, guide.ready ? .45f : 1.0f, .85f, .16f, .8f);
                    if (offscreen)
                    {
                        const Vec3 p = goal->WorldPosition(), hero = PlayerPosition();
                        float dx = ((p.x - p.y) - (hero.x - hero.y)) * TileWidth * .5f;
                        float dy = ((p.x + p.y) - (hero.x + hero.y)) * TileHeight * .5f;
                        float length = (std::max)(.001f, std::hypot(dx, dy));
                        dx /= length;
                        dy /= length;
                        renderer->DrawQuad(x + dx * 15,
                                           y + dy * 15,
                                           x - dx * 7 - dy * 8,
                                           y - dy * 7 + dx * 8,
                                           x - dx * 7 + dy * 8,
                                           y - dy * 7 - dx * 8,
                                           x + dx * 15,
                                           y + dy * 15,
                                           1,
                                           .9f,
                                           .35f);
                    }
                    else
                    {
                        renderer->DrawSolidDiamond(x, y, 13, 17, 1, .9f, .35f, 1);
                    }
                    std::wstring label = guide.destination;
                    if (guide.ready && guide.interactionKey)
                    {
                        label += L" [";
                        label += wchar_t(guide.interactionKey);
                        label += L"]";
                    }
                    else
                    {
                        label += L" \u00b7 " +
                                 std::to_wstring(
                                     int(std::ceil(Distance2D(goal->WorldPosition(), PlayerPosition())))) +
                                 L"\ud0c0\uc77c";
                    }
                    renderer->DrawSolidRect(x, y + 26, 280, 30, .04f, .06f, .05f, .96f);
                    DrawWrapped(x - 132, y + 32, label, 264, 1);
                    return;
                }
                if (part == UiPart::NpcLabel)
                {
                    const auto* npc = dynamic_cast<const Npc*>(firstLevel.Scene().Find(actor.ParentId()));
                    if (!npc || npc->health <= 0 || npc->Id() != focusedNpcId ||
                        Distance2D(npc->WorldPosition(), PlayerPosition()) >= NpcNameDistance ||
                        npc->Id() == harbor.ObjectiveTarget())
                    {
                        return;
                    }
                    float x, y;
                    WorldToScreen(actor.WorldPosition(), x, y);
                    float width = renderer->TextWidth(npc->name.c_str(), fontDC, fontHandle) + 16;
                    if (ReserveWorldLabel(x, y, width, 24))
                    {
                        bool danger = npc->job == Harbor::Job::Raider;
                        renderer->DrawSolidRect(x, y, width, 24, .035f, .055f, .06f, .92f);
                        DrawText(x - width * .5f + 8,
                                 y + 5,
                                 npc->name.c_str(),
                                 danger ? 1.0f : .9f,
                                 danger ? .42f : .88f,
                                 danger ? .32f : .67f);
                    }
                    return;
                }
                if (part == UiPart::SiteLabel)
                {
                    const auto* site =
                        dynamic_cast<const Harbor::Landmark*>(firstLevel.Scene().Find(actor.ParentId()));
                    float x, y;
                    WorldToScreen(actor.WorldPosition(), x, y);
                    if (site && Distance2D(site->WorldPosition(), PlayerPosition()) < 9)
                    {
                        float width = (std::min)(
                            300.0f, renderer->TextWidth(site->label.c_str(), fontDC, fontHandle) + 16);
                        if (ReserveWorldLabel(x, y, width, 28))
                        {
                            renderer->DrawSolidRect(x, y, width, 28, .04f, .06f, .07f, .92f);
                            DrawWrapped(x - width * .5f + 8, y + 6, site->label, width - 16, 1);
                        }
                    }
                    return;
                }
                if (part == UiPart::Prompt)
                {
                    const Actor* owner = firstLevel.Scene().Find(actor.ParentId());
                    if (!owner)
                    {
                        return;
                    }
                    float x, y;
                    WorldToScreen(actor.WorldPosition(), x, y);
                    if (!ReserveWorldLabel(x, y, 170, 30))
                    {
                        return;
                    }
                    renderer->DrawSolidRect(x, y, 170, 30, .04f, .06f, .07f, .96f);
                    const Npc* npc = dynamic_cast<const Npc*>(owner);
                    const wchar_t* promptText =
                        owner->Kind() == ActorKind::Loot   ? L"E \uc804\ub9ac\ud488 \uc90d\uae30"
                        : owner->Kind() == ActorKind::Prop ? L"F \uc870\uc0ac\u00b7\ud68d\ub4dd"
                        : npc && npc->health <= 0          ? L"E \uc720\ud488 \ucc59\uae30\uae30"
                                                           : L"E \ub9d0 \uac78\uae30";
                    DrawText(x - 76, y + 6, promptText, 1, .84f, .43f);
                    return;
                }

                Vec3 origin = actor.WorldPosition();
                uiOriginX = origin.x;
                uiOriginY = origin.y;
                renderer->SetScreenOffset(uiOriginX, uiOriginY);
                switch (part)
                {
                case UiPart::Hud:
                    DrawHud();
                    break;
                case UiPart::Dialogue:
                    DrawDialogue();
                    break;
                case UiPart::GuideHelp:
                    DrawGuideHelp();
                    break;
                case UiPart::QuestTracker:
                    DrawQuestTracker();
                    break;
                case UiPart::MiniMap:
                    RenderMiniMap();
                    break;
                case UiPart::Stats:
                    DrawStatsPanel();
                    break;
                case UiPart::Trade:
                    DrawTradePanel();
                    break;
                case UiPart::Journal:
                    DrawJournal();
                    break;
                case UiPart::Pause:
                    DrawPausePanel();
                    break;
                default:
                    break;
                }
                renderer->SetScreenOffset(0, 0);
                uiOriginX = uiOriginY = 0;
            });
        renderer->FinishOverlay();
    }

    void ClearGameplayInput(bool keepTradeKeys = false)
    {
        const bool tradeHeld = keys['t'], escapeHeld = keys[27];
        interactionPressed = potionPressed = tradePressed = sitePressed = revealPressed = false;
        attributePressed = 0;
        inventoryTogglePressed = inventoryClosePressed = inventoryUsePressed = inventoryClickPressed = false;
        inventoryMovePressed = 0;
        tradeClosePressed = tradeConfirmPressed = tradeClickPressed = false;
        tradeMovePressed = tradeQuantityPressed = 0;
        std::fill(std::begin(keys), std::end(keys), false);
        if (keepTradeKeys)
        {
            keys['t'] = tradeHeld;
            keys[27] = escapeHeld;
        }
    }

    bool WorldPaused()
    {
        return paused || helpVisible || statsPanel || harbor.Conversation().Active() ||
               harbor.Trading().Active();
    }

    void UpdateInventoryInput()
    {
        Perf::Scope scope(Perf::Cpu::InventoryInput);
        inventorySelection = Inventory::MoveSelection(inventorySelection, inventoryMovePressed);
        if (inventoryClickPressed)
        {
            const Vec3 origin = firstLevel.Scene().Get(inventoryPanelId).WorldPosition();
            const float clickX = inventoryClickX - origin.x, clickY = inventoryClickY - origin.y;
            const int slot = Inventory::HitSlot(clickX, clickY);
            if (slot >= 0)
            {
                inventorySelection = slot;
            }
            if (clickX >= 1070 && clickX <= 1120 && clickY >= 135 && clickY <= 170)
            {
                inventoryClosePressed = true;
            }
            if (clickX >= 981 && clickX <= 1099 && clickY >= 660 && clickY <= 688)
            {
                potionPressed = true;
            }
        }
        const auto slots = InventorySlots();
        if (potionPressed ||
            (inventoryUsePressed && slots[size_t(inventorySelection)].item == Inventory::Item::Potion))
        {
            firstLevel.UsePotion();
            inventoryActionNote = firstLevel.message;
        }
        if (attributePressed)
        {
            firstLevel.Allocate(attributePressed);
            inventoryActionNote = firstLevel.message;
        }
        inventoryMovePressed = attributePressed = 0;
        inventoryClickPressed = inventoryUsePressed = potionPressed = false;
    }

    void UpdateTradeInput()
    {
        Perf::Scope scope(Perf::Cpu::TradeInput);
        if (tradeClosePressed)
        {
            harbor.EndTrade();
            tradeConfirmPending = false;
            ClearGameplayInput(true);
            return;
        }
        auto offers = harbor.TradeOffers();
        auto select = [&](int row)
        {
            if (!offers.empty())
            {
                tradeSelection = (std::max)(0, (std::min)(row, int(offers.size()) - 1));
                tradeQuantity = 1;
                tradeConfirmPending = false;
            }
        };
        if (tradeMovePressed)
        {
            select(tradeSelection + tradeMovePressed);
        }
        if (tradeClickPressed)
        {
            const Vec3 origin = firstLevel.Scene().Get(tradePanelId).WorldPosition();
            const float x = tradeClickX - origin.x, y = tradeClickY - origin.y;
            if (Shop::Close.Contains(x, y))
            {
                harbor.EndTrade();
                tradeConfirmPending = false;
                ClearGameplayInput(true);
                return;
            }
            const int row = Shop::RowAt(x, y, int(offers.size()));
            if (row >= 0)
            {
                select(row);
            }
            if (Shop::Less.Contains(x, y))
            {
                tradeQuantityPressed = -1;
            }
            if (Shop::More.Contains(x, y))
            {
                tradeQuantityPressed = 1;
            }
            if (Shop::Maximum.Contains(x, y))
            {
                tradeQuantityPressed = Shop::QuantityLimit;
            }
            if (Shop::Confirm.Contains(x, y))
            {
                tradeConfirmPressed = true;
            }
        }
        if (!offers.empty())
        {
            tradeSelection = (std::max)(0, (std::min)(tradeSelection, int(offers.size()) - 1));
            const auto& offer = offers[size_t(tradeSelection)];
            if (tradeQuantityPressed)
            {
                tradeQuantity = Shop::Quantity(tradeQuantity + tradeQuantityPressed, offer.maximum);
                tradeConfirmPending = false;
            }
            tradeQuantity = Shop::Quantity(tradeQuantity, offer.maximum);
            if (tradeConfirmPressed)
            {
                if (offer.maximum > 0 && Harbor::NeedsTradeConfirmation(offer.action) && !tradeConfirmPending)
                {
                    tradeConfirmPending = true;
                }
                else
                {
                    harbor.ExecuteTrade(offer.action, tradeQuantity);
                    tradeConfirmPending = false;
                    tradeQuantity = 1;
                }
            }
        }
        tradeClosePressed = tradeConfirmPressed = tradeClickPressed = false;
        tradeMovePressed = tradeQuantityPressed = 0;
    }

    void Update(float dt)
    {
        Perf::Scope scope(Perf::Cpu::Update);
        if (harbor.Trading().Active())
        {
            UpdateTradeInput();
            UpdatePromptActor();
            return;
        }
        if (harbor.Conversation().Active())
        {
            if (dialogueClosePressed)
            {
                harbor.EndDialogue();
            }
            else if (dialogueChoicePressed >= 0)
            {
                harbor.ChooseDialogue(size_t(dialogueChoicePressed));
            }
            else if (dialogueNextPressed)
            {
                harbor.AdvanceDialogue();
            }
            dialogueClosePressed = dialogueNextPressed = false;
            dialogueChoicePressed = -1;
            interactionPressed = potionPressed = tradePressed = sitePressed = revealPressed = false;
            attributePressed = 0;
            if (!harbor.Conversation().Active())
            {
                ClearGameplayInput();
            }
            UpdatePromptActor();
            return;
        }
        if (helpPressed)
        {
            helpVisible = true;
            introVisible = false;
            helpPressed = false;
            journalPanel = statsPanel = false;
        }
        if (inventoryTogglePressed || inventoryClosePressed)
        {
            statsPanel = inventoryClosePressed || paused || helpVisible ? false : !statsPanel;
            inventoryActionNote.clear();
            journalPanel = false;
            ClearGameplayInput();
        }
        if (helpVisible && (continueHelpPressed || skipIntroPressed))
        {
            if (introVisible && continueHelpPressed && !skipIntroPressed && introPage < 2)
            {
                ++introPage;
            }
            else
            {
                helpVisible = introVisible = false;
            }
            continueHelpPressed = skipIntroPressed = false;
            ClearGameplayInput();
            UpdatePromptActor();
            return;
        }
        if (statsPanel && !paused && !helpVisible)
        {
            UpdateInventoryInput();
            UpdatePromptActor();
            return;
        }
        if (paused || helpVisible)
        {
            continueHelpPressed = false;
            interactionPressed = false;
            potionPressed = false;
            tradePressed = sitePressed = revealPressed = false;
            attributePressed = 0;
            UpdatePromptActor();
            return;
        }
        elapsed += dt;
        float sx = (keys['d'] ? 1.0f : 0) - (keys['a'] ? 1.0f : 0);
        float sy = (keys['s'] ? 1.0f : 0) - (keys['w'] ? 1.0f : 0);
        firstLevel.MovePlayer(sx, sy, dt);
        firstLevel.Update(dt);
        if (firstLevel.ConsumeAttackImpact())
        {
            harbor.PlayerAttack();
        }
        harbor.Update(dt);
        if (keys[' '])
        {
            firstLevel.Attack();
        }
        if (interactionPressed)
        {
            TryInteract();
            interactionPressed = false;
            if (harbor.Conversation().Active())
            {
                journalPanel = statsPanel = false;
                ClearGameplayInput();
                UpdatePromptActor();
                return;
            }
        }
        if (tradePressed)
        {
            harbor.Trade();
            tradePressed = false;
            if (harbor.Trading().Active())
            {
                tradeSelection = 0;
                tradeQuantity = 1;
                tradeConfirmPending = false;
                journalPanel = statsPanel = false;
                ClearGameplayInput(true);
                UpdatePromptActor();
                return;
            }
        }
        if (sitePressed)
        {
            harbor.InteractSite();
            sitePressed = false;
            if (harbor.Conversation().Active())
            {
                journalPanel = statsPanel = false;
                ClearGameplayInput();
                UpdatePromptActor();
                return;
            }
        }
        if (revealPressed)
        {
            harbor.Reveal();
            revealPressed = false;
        }
        if (potionPressed)
        {
            firstLevel.UsePotion();
            potionPressed = false;
        }
        if (attributePressed)
        {
            firstLevel.Allocate(attributePressed);
            attributePressed = 0;
        }
        if (harbor.Conversation().Active())
        {
            journalPanel = statsPanel = false;
            ClearGameplayInput();
        }
        UpdatePromptActor();
    }
} // namespace

void RenderScene()
{
    if (!renderer)
    {
        return;
    }
    auto& profiler = Perf::Profiler::Get();
    profiler.BeginFrame();
    {
        Perf::Scope events(Perf::Cpu::WindowEvents);
        glfwPollEvents();
    }
    if (glfwWindowShouldClose(gameWindow) || glfwGetWindowAttrib(gameWindow, GLFW_ICONIFIED) ||
        outputWidth <= 0 || outputHeight <= 0)
    {
        const Vec3 player = PlayerPosition();
        profiler.SetSceneState(firstLevel.seed, player.x, player.y, 0, true, outputWidth, outputHeight);
        profiler.EndFrame("window_suspended");
        profiler.ResetFrameClock();
        return;
    }
    renderer->SetOutputSize(outputWidth, outputHeight);
    const auto now = std::chrono::steady_clock::now();
    float dt = previousTime.time_since_epoch().count()
                   ? std::chrono::duration<float>(now - previousTime).count()
                   : 0;
    previousTime = now;
    Update((std::min)(dt, .05f));
    const Vec3 player = PlayerPosition();
    Vec3 camera = player;
    if (!WorldPaused())
    {
        const Vec3 kick = firstLevel.HitCameraOffset();
        camera.x += kick.x;
        camera.y += kick.y;
    }
    renderCamera = camera;
    firstLevel.UpdateVisibleChunks();
    profiler.SetSceneState(
        firstLevel.seed, player.x, player.y, (std::min)(dt, .05f), WorldPaused(), outputWidth, outputHeight);
    firstLevel.Scene().PrepareFrame(
        [&](const Actor& actor)
        {
            auto p = actor.WorldPosition();
            float x = ScreenWidth * .5f + ((p.x - p.y) - (camera.x - camera.y)) * TileWidth * .5f;
            float y = ScreenHeight * .52f + ((p.x + p.y) - (camera.x + camera.y)) * TileHeight * .5f - p.z;
            return Visible(x, y);
        });
    renderer->SetWorldCamera(camera.x, camera.y);
    UpdateWorldLights();
    profiler.GpuMark("render_begin");
    renderer->SetTime(elapsed);
    renderer->BeginFrame(.075f, .12f, .14f, 1);
    RenderWorldPass(ScenePass::Ground);
    RenderWorldPass(ScenePass::World);
    RenderWorldPass(ScenePass::Effects);
    renderer->EndWorld(elapsed);
    RenderOverlay();
    {
        Perf::Scope swap(Perf::Cpu::Swap);
        glfwSwapBuffers(gameWindow);
    }
    worldMonitor.Tick(harbor, WorldPaused(), dt);
    renderer->EndFrame();
}

void KeyDown(unsigned char key)
{
    if (key >= 'A' && key <= 'Z')
    {
        key += 32;
    }
    if (keys[key])
    {
        return;
    }
    keys[key] = true;
    if (harbor.Trading().Active())
    {
        if (key == 27 || key == 't')
        {
            tradeClosePressed = true;
        }
        if (key == 'e' || key == 13)
        {
            tradeConfirmPressed = true;
        }
        if (key == 'w')
        {
            tradeMovePressed = -1;
        }
        if (key == 's')
        {
            tradeMovePressed = 1;
        }
        if (key == 'a')
        {
            tradeQuantityPressed = -1;
        }
        if (key == 'd')
        {
            tradeQuantityPressed = 1;
        }
        return;
    }
    if (harbor.Conversation().Active())
    {
        if (key == 27)
        {
            dialogueClosePressed = true;
        }
        else if (key >= '1' && key <= '3')
        {
            dialogueChoicePressed = key - '1';
        }
        else if (key == 'e' || key == 13)
        {
            dialogueNextPressed = true;
        }
        return;
    }
    if (helpVisible)
    {
        if (key == 13)
        {
            continueHelpPressed = true;
        }
        else if (key == 'h')
        {
            skipIntroPressed = true;
        }
        return;
    }
    if (key == 'h' && !paused)
    {
        helpPressed = true;
        return;
    }
    if ((key == 'i' || key == '\t') && !paused)
    {
        inventoryTogglePressed = true;
        return;
    }
    if (statsPanel)
    {
        if (key == 27)
        {
            inventoryClosePressed = true;
        }
        if (key == 'e' || key == 13)
        {
            inventoryUsePressed = true;
        }
        if (key == 'q')
        {
            potionPressed = true;
        }
        if (key == 'a')
        {
            inventoryMovePressed = -1;
        }
        if (key == 'd')
        {
            inventoryMovePressed = 1;
        }
        if (key == 'w')
        {
            inventoryMovePressed = -Inventory::Columns;
        }
        if (key == 's')
        {
            inventoryMovePressed = Inventory::Columns;
        }
        if (key >= '1' && key <= '3')
        {
            attributePressed = key - '0';
        }
        return;
    }
    if (key == 27)
    {
        paused = !paused;
    }
    else if (key == 'j')
    {
        journalPanel = !journalPanel;
        if (journalPanel)
        {
            statsPanel = false;
        }
    }
    else if (key == 'm')
    {
        worldMap = !worldMap;
    }
    else if (!paused)
    {
        if (key == 'e')
        {
            interactionPressed = true;
        }
        if (key == 't')
        {
            tradePressed = true;
        }
        if (key == 'f')
        {
            sitePressed = true;
        }
        if (key == 'r')
        {
            revealPressed = true;
        }
        if (key == 'q')
        {
            potionPressed = true;
        }
        if (key >= '1' && key <= '3')
        {
            attributePressed = key - '0';
        }
    }
}

void KeyUp(unsigned char key)
{
    if (key >= 'A' && key <= 'Z')
    {
        key += 32;
    }
    keys[key] = false;
}

void InventoryMouse(int button, int state, double x, double y)
{
    if (harbor.Trading().Active() && button == GLFW_MOUSE_BUTTON_LEFT && state == GLFW_PRESS)
    {
        tradeClickX = float(x) * ScreenWidth / (std::max)(1, inputWidth);
        tradeClickY = float(y) * ScreenHeight / (std::max)(1, inputHeight);
        tradeClickPressed = true;
        return;
    }
    if (statsPanel && button == GLFW_MOUSE_BUTTON_LEFT && state == GLFW_PRESS)
    {
        inventoryClickX = float(x) * ScreenWidth / (std::max)(1, inputWidth);
        inventoryClickY = float(y) * ScreenHeight / (std::max)(1, inputHeight);
        inventoryClickPressed = true;
    }
}

void InventorySpecialKey(int key)
{
    if (harbor.Trading().Active())
    {
        if (key == GLFW_KEY_UP)
        {
            tradeMovePressed = -1;
        }
        if (key == GLFW_KEY_DOWN)
        {
            tradeMovePressed = 1;
        }
        if (key == GLFW_KEY_LEFT)
        {
            tradeQuantityPressed = -1;
        }
        if (key == GLFW_KEY_RIGHT)
        {
            tradeQuantityPressed = 1;
        }
        return;
    }
    if (!statsPanel)
    {
        return;
    }
    if (key == GLFW_KEY_LEFT)
    {
        inventoryMovePressed = -1;
    }
    if (key == GLFW_KEY_RIGHT)
    {
        inventoryMovePressed = 1;
    }
    if (key == GLFW_KEY_UP)
    {
        inventoryMovePressed = -Inventory::Columns;
    }
    if (key == GLFW_KEY_DOWN)
    {
        inventoryMovePressed = Inventory::Columns;
    }
}

void Entry(GLFWwindow*, int entered)
{
    if (!entered)
    {
        ClearGameplayInput();
    }
}

void FocusChanged(GLFWwindow*, int focused)
{
    Perf::Count(Perf::Counter::WindowFocusEvents);
    if (!focused)
    {
        ClearGameplayInput();
    }
}

void Reshape(GLFWwindow*, int width, int height)
{
    outputWidth = width;
    outputHeight = height;
    Perf::Count(Perf::Counter::WindowResizeEvents);
}

void WindowSizeChanged(GLFWwindow*, int width, int height)
{
    inputWidth = width;
    inputHeight = height;
}

void WindowKey(GLFWwindow*, int key, int, int action, int)
{
    Perf::Count(Perf::Counter::WindowKeyEvents);
    if (action == GLFW_REPEAT)
    {
        return;
    }
    const unsigned char command = WindowInput::CommandKey(key);
    if (command)
    {
        if (action == GLFW_PRESS)
        {
            KeyDown(command);
        }
        else if (action == GLFW_RELEASE)
        {
            KeyUp(command);
        }
    }
    else if (action == GLFW_PRESS)
    {
        InventorySpecialKey(key);
    }
}

void WindowMouse(GLFWwindow* window, int button, int action, int)
{
    Perf::Count(Perf::Counter::WindowMouseEvents);
    double x = 0, y = 0;
    glfwGetCursorPos(window, &x, &y);
    InventoryMouse(button, action, x, y);
}

void WindowError(int code, const char* message)
{
    std::cerr << "[window] error_code=" << code << " message=" << (message ? message : "") << '\n';
}

void Close()
{
    // Stop the reader and release every GL resource while the GLFW context is still current.
    if (gameWindow)
    {
        glfwMakeContextCurrent(gameWindow);
    }
    worldMonitor.Stop();
    Perf::Profiler::Get().Shutdown();
    delete renderer;
    renderer = nullptr;
    koreanFont.Shutdown();
    fontDC = nullptr;
    fontHandle = nullptr;
    if (gameWindow)
    {
        glfwDestroyWindow(gameWindow);
        gameWindow = nullptr;
    }
    glfwTerminate();
}

int main()
{
    {
        Perf::Scope windowInit(Perf::Cpu::WindowInit);
        glfwSetErrorCallback(WindowError);
        if (!glfwInit())
        {
            return 1;
        }
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);
        glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);
        glfwWindowHint(GLFW_RED_BITS, 8);
        glfwWindowHint(GLFW_GREEN_BITS, 8);
        glfwWindowHint(GLFW_BLUE_BITS, 8);
        glfwWindowHint(GLFW_ALPHA_BITS, 0);
        glfwWindowHint(GLFW_DEPTH_BITS, 0);
        glfwWindowHint(GLFW_STENCIL_BITS, 0);
        glfwWindowHint(GLFW_SAMPLES, 0);
        glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_FALSE);
        glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_FALSE);
        gameWindow = glfwCreateWindow(ScreenWidth,
                                      ScreenHeight,
                                      u8"\ubb34\uc5f0\ud56d \u00b7 \uc0b4\uc544 \uc788\ub294 \ud56d\uad6c",
                                      nullptr,
                                      nullptr);
        if (!gameWindow)
        {
            Close();
            return 1;
        }
        glfwMakeContextCurrent(gameWindow);
        glewExperimental = GL_TRUE;
        const GLenum loader = glewInit();
        if (loader != GLEW_OK || !glewIsSupported("GL_VERSION_3_3"))
        {
            std::cerr << "[window] OpenGL_3_3_required; glew_status=" << loader << '\n';
            Close();
            return 1;
        }
        // Preserve the continuously rendered loop; opt into vsync with SIMPLEGAME_VSYNC=1.
        char vsync[8]{};
        GetEnvironmentVariableA("SIMPLEGAME_VSYNC", vsync, sizeof(vsync));
        glfwSwapInterval(vsync[0] == '1' ? 1 : 0);
        glfwGetWindowSize(gameWindow, &inputWidth, &inputHeight);
        glfwGetFramebufferSize(gameWindow, &outputWidth, &outputHeight);
        glfwSetWindowSizeCallback(gameWindow, WindowSizeChanged);
        glfwSetFramebufferSizeCallback(gameWindow, Reshape);
        glfwSetKeyCallback(gameWindow, WindowKey);
        glfwSetMouseButtonCallback(gameWindow, WindowMouse);
        glfwSetCursorEnterCallback(gameWindow, Entry);
        glfwSetWindowFocusCallback(gameWindow, FocusChanged);
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        Perf::Profiler::Get().Initialize(vsync[0] == '1' ? 1 : 0);
        Perf::Profiler::Get().BeginFrame();
    }
    {
        Perf::Scope scope(Perf::Cpu::RendererInit);
        renderer = new Renderer(ScreenWidth, ScreenHeight);
    }
    if (!renderer->IsInitialized() || !InitializeKoreanFont())
    {
        Close();
        return 1;
    }
    std::random_device random;
    firstLevel.Generate(random());
    if (!firstLevel.AllWalkableConnected())
    {
        std::cerr << "Generated world failed connectivity validation.\n";
        Close();
        return 1;
    }
    if (!InitializeModels())
    {
        std::cerr << "Resource atlas missing or invalid. Rebuild to deploy Resources assets.\n";
        Close();
        return 1;
    }
    InitializeSceneActors();
    UpdatePromptActor();
    worldMonitor.Start();
    Perf::Profiler::Get().EndFrame("startup");
    while (!glfwWindowShouldClose(gameWindow))
    {
        if (glfwGetWindowAttrib(gameWindow, GLFW_ICONIFIED) || outputWidth <= 0 || outputHeight <= 0)
        {
            glfwWaitEventsTimeout(.05);
            previousTime = std::chrono::steady_clock::now();
            Perf::Profiler::Get().ResetFrameClock();
            worldMonitor.Tick(harbor, true, .05);
            continue;
        }
        RenderScene();
    }
    Close();
    return 0;
}
