#include "stdafx.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <random>
#include <iterator>
#include "Dependencies/glew.h"
#include "Dependencies/freeglut.h"
#include "Renderer.h"
#include "LevelOne.h"
#include "Profiler.h"
#include "HarborWorld.h"
#include "WorldMonitor.h"
#include "CharacterPresentation.h"

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
        Dialogue
    };
    ActorId promptId = InvalidActorId;
    ActorId questMarkerId = InvalidActorId;
    float uiOriginX = 0;
    float uiOriginY = 0;

    Renderer* renderer = nullptr;
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
    int previousTime = 0;
    int outputWidth = ScreenWidth;
    int outputHeight = ScreenHeight;
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

    void InitializeKoreanFont()
    {
        Perf::Scope scope(Perf::Cpu::FontInit);
        fontDC = wglGetCurrentDC();
        fontHandle = CreateFontW(-UiFontHeight,
                                 0,
                                 0,
                                 0,
                                 FW_NORMAL,
                                 FALSE,
                                 FALSE,
                                 FALSE,
                                 HANGUL_CHARSET,
                                 OUT_TT_PRECIS,
                                 CLIP_DEFAULT_PRECIS,
                                 ANTIALIASED_QUALITY,
                                 FF_DONTCARE,
                                 L"Malgun Gothic");
    }

    void DrawText(float x, float y, const wchar_t* text, float r = .93f, float g = .88f, float b = .73f)
    {
        renderer->DrawText(x, y, text, r, g, b, fontDC, fontHandle);
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
        for (int direction = 0; direction < 4; ++direction)
        {
            const std::string suffix = "@" + std::to_string(direction);
            required.push_back("hero_idle" + suffix);
            for (int frame = 0; frame < 8; ++frame)
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
                for (int frame = 0; frame < 8; ++frame)
                {
                    required.push_back(prefix + std::to_string(frame) + suffix);
                }
            }
            for (int kind = 0; kind < 3; ++kind)
            {
                const std::string prefix = "beast" + std::to_string(kind) + "_";
                required.push_back(prefix + "idle" + suffix);
                for (int frame = 0; frame < 8; ++frame)
                {
                    required.push_back(prefix + std::to_string(frame) + suffix);
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
                            UiPart::Dialogue})
        {
            Actor& ui = scene.Create(firstLevel.OverlayRoot(), "UIPanel", ActorKind::UI);
            ui.variant = int(part);
            ui.drawable = true;
            ui.pass = ScenePass::Overlay;
            ui.drawOrder = part == UiPart::GuideHelp  ? 200
                           : part == UiPart::Dialogue ? 150
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
                int frame = CharacterPresentation::AttackFrame(firstLevel.attackAnimation);
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
                                    renderer->DrawModel(
                                        actor.model, x, y, actor.mirrorModel, actor.visualDirection);
                                }
                                if (actor.Kind() == ActorKind::Effect)
                                {
                                    DrawEffectActor(actor, x, y);
                                }
                            });
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
            return guide.interactionKey ? L"\ub3c4\ucc29\ud588\ub2e4. " + guide.action : guide.action;
        }
        if (Distance2D(target->WorldPosition(), PlayerPosition()) < 1.8f)
        {
            if (guide.step == 4 && !guide.interactionKey)
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
                                   L"\ubc24\uc0c8 \ub0b4\ub9b0 \ube44",
                                   L"\ubb3c\uae38 \uc544\ub798\uc758 \uc774\ub984\ub4e4"};
        const wchar_t* stories[] = {
            L"\uccad\ud608\ubcd1\uc774 \ubc88\uc9c4 \ub4a4, \uad00\ubb38\uc744 \uc9c0\ub098\ub294 \uc57d\uc7ac\uc640 \uace1\ubb3c\uc774 \uc904\uc5c8\ub2e4. \uc57d\uac12\uc740 \uc624\ub974\uace0 \uc77c\uac10\uc740 \ub4dc\ubb3c\uc5b4\uc84c\ub2e4. \uadf8\ub798\ub3c4 \ubb34\uc5f0\ud56d\uc758 \uc0ac\ub78c\ub4e4\uc740 \uc0c8\ubcbd\uc7a5\uc744 \uc5f4\uace0, \uc544\ud508 \uc2dd\uad6c\ub97c \uba39\uc774\uace0, \uc624\ub298 \ud558\ub8e8\ub97c \ubc84\ud2f4\ub2e4.\n\n\ub2f9\uc2e0\ub3c4 \uc2dd\ub7c9\uacfc \uc57d\uac12\uc744 \ubc8c \uc77c\uac70\ub9ac\ub97c \ucc3e\uc544 \uc774 \ud56d\uad6c\uc5d0 \ub4e4\uc5b4\uc130\ub2e4.", L"\ubc24\uc0c8 \ud3ed\ud48d\uc774 \ubab0\uc544\ucce4\ub2e4. \uad6c\uc2dc\uac00\uc9c0 \uc544\ub798\uc758 \ub0a1\uc740 \ubc30\uc218\ub85c\uac00 \ubb34\ub108\uc84c\uace0, \ube57\ubb3c\uc774 \ub0ae\uc740 \uace8\ubaa9\uc5d0 \uace0\uc600\ub2e4. \ube44\uac00 \uadf8\uce5c \ub4a4 \uc77c\ubd80 \ubb3c\uc774 \ube60\uc9c0\uba74\uc11c \ubcbd \uc548\ucabd\uc758 \uc624\ub798\ub41c \ud654\ubb3c \uc0c1\uc790\uac00 \ub4dc\ub7ec\ub0ac\ub2e4.\n\n\uc7a0\uae34 \uc61b \uc218\ubb38\uc744 \uc5f4\uc9c0 \ubabb\ud558\uba74 \ub0a8\uc740 \ubb3c\ub3c4 \ube60\uc9c0\uc9c0 \uc54a\ub294\ub2e4. \uae30\uc220\uc790 \ubbf8\ub77c\ub294 \ubb34\ub108\uc9c4 \uc785\uad6c\ub97c \uc0b4\ud53c\uace0 \uc788\ub2e4.", L"\uc0c1\uc790\ub294 \uc5f4\ub450 \ud574 \uc804 \uce68\ubab0\ud55c \ubc00\uc218\uc120\uc758 \uc9d0\uc774\ub77c\uace0 \ud55c\ub2e4. \uc548\uc5d0\ub294 \uc61b \uc218\ubb38 \uc5f4\uc1e0\uc640, \uc2e4\uc885\ub41c \uc0ac\ub78c\ub4e4\uc758 \uc774\ub984\uc774 \uc801\ud78c \uc7a5\ubd80\uac00 \uc788\ub2e4\ub294 \uc18c\ubb38\uc774 \ub3c8\ub2e4. \uac00\uc871\ub4e4\uc740 \ub2f5\uc744 \uc6d0\ud558\uace0, \uad00\uccad\uc740 \uc9c8\uc11c\ub97c \ub9d0\ud558\uace0, \uc911\uac1c\uc0c1\uc740 \uac12\uc744 \ub9e4\uae34\ub2e4.\n\n\uc544\uc9c1 \ub204\uad6c\uc758 \ub9d0\uc744 \ubbff\uc744\uc9c0\ub294 \uc815\ud558\uc9c0 \uc54a\uc544\ub3c4 \ub41c\ub2e4. \uba3c\uc800 \ubbf8\ub77c\ub97c \ucc3e\uc544 \ubb34\uc2a8 \uc77c\uc774 \uc788\uc5c8\ub294\uc9c0 \ub4e4\uc5b4\ubcf4\uc790."};
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
                : L"ENTER: \ubbf8\ub77c\ub97c \ub9cc\ub098\ub7ec \uac00\uae30 \u00b7 H: \ubc14\ub85c \uc2dc\uc791",
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
                 guide.step == 0 ? L"\ubb34\uc5f0\ud56d\uc5d0 \ub3c4\ucc29\ud588\ub2e4"
                                 : L"\uc9c0\uae08 \ubb34\uc5c7\uc744 \ud558\uba74 \ub420\uae4c?",
                 1,
                 .83f,
                 .45f);
        DrawWrapped(304, 276, guide.reason, 672, 2);
        if (guide.step == 0)
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
        else if (guide.step == 3)
        {
            DrawWrapped(
                304,
                338,
                L"\uacf5\uac1c [\uc7a5\ud130 R]: \uac00\uc871\ub4e4\uc774 \uc774\ub984\uc744 \ub4e3\uc9c0\ub9cc, \ub4dc\ub7ec\ub098\uae38 \uc6d0\uce58 \uc54a\ub294 \uc0ac\ub78c\ub3c4 \uc788\uc2b5\ub2c8\ub2e4.",
                672,
                2);
            DrawWrapped(
                304,
                386,
                L"\uad00\uccad [\uc138\ub77c\ud2b8 \uacc1 T]: 30\ub0e5\uc744 \ubc1b\uc2b5\ub2c8\ub2e4. \ub300\uc2e0 \ud604\uc7a5\uc774 \ud1b5\uc81c\ub429\ub2c8\ub2e4.",
                672,
                2);
            DrawWrapped(
                304,
                434,
                L"\ud310\ub9e4 [\uc774\uc548 \uacc1 T]: 40\ub0e5\uc744 \ubc1b\uc2b5\ub2c8\ub2e4. \uae30\ub85d\uc740 \uc0ac\uc801\uc778 \uac70\ub798\uc5d0 \ubb36\uc785\ub2c8\ub2e4.",
                672,
                2);
            DrawWrapped(
                304,
                482,
                L"\uc5ec\uae30\uc11c\ub294 \uacb0\uc815\ud558\uc9c0 \uc54a\uc2b5\ub2c8\ub2e4. \ub3cc\uc544\uac00\uc11c \uc6d0\ud558\ub294 \uc0ac\ub78c\uc774\ub098 \uc7a5\uc18c\ub97c \ucc3e\uc544 \ud589\ub3d9\ud558\uc138\uc694.",
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
        std::wstring points = L"TAB \ub2a5\ub825 \uc810\uc218 " + std::to_wstring(stats.points);
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
            L"WASD \uc774\ub3d9  E \ub300\ud654\u00b7\uc90d\uae30  F \uc870\uc0ac  H \uc9c0\uae08 \ud560 \uc77c  SPACE \uacf5\uaca9  Q \uc57d  M \uc9c0\ub3c4  J \uc77c\uc9c0  TAB \ub2a5\ub825  ESC",
            1220,
            1);
    }

    void DrawStatsPanel()
    {
        const auto& stats = firstLevel.stats;

        if (statsPanel)
        {
            renderer->DrawSolidRect(1060, 445, 390, 354, .03f, .055f, .06f, .96f);
            DrawText(885, 300, L"\ub2a5\ub825\uce58", .97f, .79f, .44f);
            std::wstring points = L"\ub0a8\uc740 \uc810\uc218: " + std::to_wstring(stats.points);
            DrawText(885, 337, points.c_str());
            DrawText(885, 374, L"1  \ud798 \uc62c\ub9ac\uae30       \uacf5\uaca9 +3");
            DrawText(885, 409, L"2  \uccb4\ub825 \uc62c\ub9ac\uae30     \ucd5c\ub300 \uccb4\ub825 +15");
            DrawText(885, 444, L"3  \ub9f7\uc9d1 \uc62c\ub9ac\uae30     \ubc29\uc5b4 +2");
            DrawText(885, 487, L"\ub808\ubca8\uc5c5: \uccb4\ub825 +20 / \uacf5\uaca9 +4");
            DrawText(885, 519, L"\ubc29\uc5b4 +1 / \ubc30\ubd84 \uc810\uc218 +2");
            DrawText(
                885, 553, L"\ub808\ubca8\uc5c5\ud558\uba74 \uccb4\ub825\ub3c4 \ud68c\ubcf5\ub41c\ub2e4.");
            std::wstring seed = L"\uc9c0\ub3c4 \ubc88\ud638: " + std::to_wstring(firstLevel.seed);
            DrawText(885, 590, seed.c_str(), .65f, .72f, .71f);
        }
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
        bool blocked = paused || helpVisible || harbor.Conversation().Active() || journalPanel ||
                       statsPanel || bounds.left < 20 || bounds.right > 1000 || bounds.top < 226 ||
                       bounds.bottom > 540;
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
                    if (paused || helpVisible || harbor.Conversation().Active() || journalPanel || statsPanel)
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

    void ClearGameplayInput()
    {
        interactionPressed = potionPressed = tradePressed = sitePressed = revealPressed = false;
        attributePressed = 0;
        std::fill(std::begin(keys), std::end(keys), false);
    }

    void Update(float dt)
    {
        Perf::Scope scope(Perf::Cpu::Update);
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
    int now = glutGet(GLUT_ELAPSED_TIME);
    float dt = previousTime ? float(now - previousTime) / 1000 : 0;
    previousTime = now;
    Update((std::min)(dt, .05f));
    const Vec3 camera = PlayerPosition();
    renderCamera = camera;
    firstLevel.UpdateVisibleChunks();
    profiler.SetSceneState(firstLevel.seed,
                           camera.x,
                           camera.y,
                           (std::min)(dt, .05f),
                           paused || helpVisible || harbor.Conversation().Active(),
                           outputWidth,
                           outputHeight);
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
        glutSwapBuffers();
    }
    worldMonitor.Tick(harbor, paused || helpVisible || harbor.Conversation().Active(), dt);
    renderer->EndFrame();
}

void Idle()
{
    glutPostRedisplay();
}

void KeyDown(unsigned char key, int, int)
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
    if (key == 27)
    {
        paused = !paused;
    }
    else if (key == '\t')
    {
        statsPanel = !statsPanel;
        if (statsPanel)
        {
            journalPanel = false;
        }
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

void KeyUp(unsigned char key, int, int)
{
    if (key >= 'A' && key <= 'Z')
    {
        key += 32;
    }
    keys[key] = false;
}

void Entry(int state)
{
    if (state == GLUT_LEFT)
    {
        std::fill(std::begin(keys), std::end(keys), false);
    }
}

void Reshape(int w, int h)
{
    outputWidth = w > 0 ? w : 1;
    outputHeight = h > 0 ? h : 1;
    if (renderer)
    {
        renderer->SetOutputSize(outputWidth, outputHeight);
    }
}

void Close()
{
    worldMonitor.Stop();
    Perf::Profiler::Get().Shutdown();
    delete renderer;
    renderer = nullptr;
}

int main(int argc, char** argv)
{
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
    glutInitWindowSize(ScreenWidth, ScreenHeight);
    glutCreateWindow("Muyeon Harbor - Living World");
    SetWindowTextW(GetActiveWindow(), L"\ubb34\uc5f0\ud56d \u00b7 \uc0b4\uc544 \uc788\ub294 \ud56d\uad6c");
    if (glewInit() != GLEW_OK || !glewIsSupported("GL_VERSION_3_3"))
    {
        return 1;
    }
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    Perf::Profiler::Get().Initialize();
    Perf::Profiler::Get().BeginFrame();
    {
        Perf::Scope scope(Perf::Cpu::RendererInit);
        renderer = new Renderer(ScreenWidth, ScreenHeight);
    }
    if (!renderer->IsInitialized())
    {
        Close();
        return 1;
    }
    InitializeKoreanFont();
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
        std::cerr
            << "Resource atlas missing or invalid. Rebuild to deploy Resources/world_atlas.png and world_sprites.bin.\n";
        Close();
        return 1;
    }
    InitializeSceneActors();
    UpdatePromptActor();
    worldMonitor.Start();
    Perf::Profiler::Get().EndFrame("startup");
    glutIgnoreKeyRepeat(1);
    glutSetOption(GLUT_ACTION_ON_WINDOW_CLOSE, GLUT_ACTION_GLUTMAINLOOP_RETURNS);
    glutReshapeFunc(Reshape);
    glutCloseFunc(Close);
    glutEntryFunc(Entry);
    glutDisplayFunc(RenderScene);
    glutIdleFunc(Idle);
    glutKeyboardFunc(KeyDown);
    glutKeyboardUpFunc(KeyUp);
    glutMainLoop();
    if (renderer && wglGetCurrentContext())
    {
        Close();
    }
    if (fontHandle)
    {
        DeleteObject(fontHandle);
    }
    return 0;
}
