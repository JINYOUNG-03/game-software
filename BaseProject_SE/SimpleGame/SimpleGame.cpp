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
        return x > -160 && x < ScreenWidth + 160 && y > -100 && y < ScreenHeight + 180;
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

    void DrawTree(float x, float y, int species = 7)
    {
        renderer->SetMaterial(3);
        renderer->DrawQuad(x - 5, y, x + 6, y, x + 2, y - 100, x - 5, y - 102, .27f, .24f, .16f);
        renderer->DrawQuad(x, y - 50, x + 3, y - 54, x + 28, y - 82, x + 25, y - 83, .24f, .22f, .15f);
        renderer->DrawQuad(x, y - 64, x - 3, y - 66, x - 23, y - 86, x - 24, y - 82, .24f, .22f, .15f);
        renderer->SetMaterial(13);
        if (species == 16)
        {
            // Broad, asymmetric pine tiers rather than repeated diamond crowns.
            for (int tier = 0; tier < 4; ++tier)
            {
                float width = 69.0f - float(tier) * 10;
                float yy = y - 50 - float(tier) * 18;
                renderer->DrawSolidRect(x + (tier % 2 ? -9 : 6),
                                        yy,
                                        width,
                                        27,
                                        .16f + float(tier) * .018f,
                                        .29f + float(tier) * .019f,
                                        .22f,
                                        1);
                renderer->DrawSolidRect(x - 18, yy - 6, width * .57f, 24, .22f, .36f, .26f, 1);
            }
        }
        else
        {
            const float lobes[][4] = {{-22, -66, 53, 46},
                                      {19, -63, 55, 42},
                                      {-7, -91, 62, 49},
                                      {24, -88, 43, 37},
                                      {-27, -92, 38, 34},
                                      {0, -111, 41, 32}};
            for (int i = 0; i < 6; ++i)
            {
                const auto& l = lobes[i];
                renderer->DrawSolidRect(x + l[0],
                                        y + l[1],
                                        l[2],
                                        l[3],
                                        .20f + float(i % 3) * .024f,
                                        .32f + float(i % 3) * .027f,
                                        species == 17 ? .25f : .20f,
                                        1);
            }
            if (species == 17)
            {
                for (int i = -3; i <= 3; ++i)
                {
                    renderer->DrawSolidRect(
                        x + float(i) * 11, y - 48 + float(std::abs(i)) * 3, 10, 55, .23f, .36f, .27f, .92f);
                }
            }
        }
        renderer->SetMaterial(0);
    }

    void DrawHouse(float x, float y, bool straw)
    {
        renderer->SetMaterial(2);
        renderer->DrawQuad(x - 62, y - 17, x + 11, y + 17, x + 59, y - 10, x - 12, y - 43, .36f, .38f, .33f);
        renderer->DrawQuad(x - 62, y - 17, x + 11, y + 17, x + 11, y + 23, x - 62, y - 11, .25f, .28f, .25f);
        renderer->SetMaterial(12);
        renderer->DrawQuad(x - 53,
                           y - 20,
                           x + 10,
                           y + 7,
                           x + 10,
                           y - 49,
                           x - 53,
                           y - 76,
                           straw ? .58f : .73f,
                           straw ? .50f : .68f,
                           straw ? .34f : .53f);
        renderer->DrawQuad(x + 10,
                           y + 7,
                           x + 51,
                           y - 15,
                           x + 51,
                           y - 71,
                           x + 10,
                           y - 49,
                           straw ? .39f : .51f,
                           straw ? .37f : .51f,
                           straw ? .27f : .43f);
        renderer->SetMaterial(3);
        for (int i = 0; i < 4; ++i)
        {
            float px = x - 52 + float(i) * 20, py = y - 47 + float(i) * 8.5f;
            renderer->DrawQuad(
                px - 2, py - 28, px + 2, py - 26, px + 2, py + 27, px - 2, py + 25, .27f, .22f, .15f);
        }
        renderer->DrawQuad(x - 54, y - 52, x + 11, y - 24, x + 11, y - 29, x - 54, y - 57, .30f, .23f, .15f);
        renderer->SetMaterial(0);
        // Paper lattice windows: warm interiors, dark frames, visible depth.
        for (int i = 0; i < 2; ++i)
        {
            float px = x - 43 + float(i) * 34, py = y - 44 + float(i) * 14;
            renderer->DrawQuad(px, py - 15, px + 20, py - 7, px + 20, py + 12, px, py + 4, .23f, .20f, .14f);
            renderer->DrawQuad(
                px + 2, py - 12, px + 18, py - 5, px + 18, py + 9, px + 2, py + 2, .88f, .70f, .39f);
            for (int k = 1; k <= 3; ++k)
            {
                float xx = px + float(k) * 4.5f;
                renderer->DrawSolidRect(xx, py - 4 + float(k) * 1.8f, 1, 15, .34f, .27f, .17f, 1);
            }
        }
        // Deep shade immediately below the eaves, independent of postprocessing.
        renderer->DrawQuad(
            x - 56, y - 75, x + 12, y - 46, x + 12, y - 37, x - 56, y - 66, .09f, .13f, .13f, .35f);
        renderer->SetMaterial(straw ? 5 : 4);
        renderer->DrawQuad(x - 17,
                           y - 116,
                           x + 3,
                           y - 126,
                           x + 88,
                           y - 90,
                           x + 69,
                           y - 80,
                           straw ? .47f : .24f,
                           straw ? .42f : .34f,
                           straw ? .27f : .36f);
        renderer->DrawQuad(x - 74,
                           y - 74,
                           x - 17,
                           y - 116,
                           x + 69,
                           y - 80,
                           x + 12,
                           y - 35,
                           straw ? .65f : .29f,
                           straw ? .55f : .39f,
                           straw ? .34f : .40f);
        renderer->SetMaterial(3);
        renderer->DrawQuad(x - 78, y - 77, x + 12, y - 43, x + 12, y - 35, x - 74, y - 70, .25f, .23f, .18f);
        renderer->DrawQuad(x + 12, y - 43, x + 73, y - 84, x + 70, y - 75, x + 12, y - 35, .19f, .21f, .19f);
        renderer->SetMaterial(0);
        renderer->DrawQuad(
            x - 20, y - 117, x + 70, y - 81, x + 73, y - 85, x - 17, y - 121, .43f, .48f, .43f);
        if (!straw)
        {
            for (int i = 0; i < 10; ++i)
            {
                float xx = x - 72 + float(i) * 8.5f, yy = y - 73 + float(i) * 3.5f;
                renderer->DrawEllipse(xx, yy, 5, 4, .47f, .51f, .43f, 1);
            }
        }
        for (int k = 0; k < 3; ++k)
        {
            float xx = x - 60 + float(k) * 11, yy = y + float(k) * 4;
            renderer->DrawEllipse(xx, yy - 8, 14, 19, .30f, .23f, .16f, 1);
            renderer->DrawEllipse(xx - 2, yy - 10, 4, 10, .45f, .35f, .22f, .65f);
            renderer->DrawEllipse(xx, yy - 18, 12, 5, .19f, .17f, .12f, 1);
        }
    }

    void DrawGiwaHouse(float x, float y)
    {
        DrawHouse(x, y, false);
    }

    void DrawChogaHouse(float x, float y)
    {
        DrawHouse(x, y, true);
    }

    void DrawMarketStall(float x, float y)
    {
        renderer->DrawSolidRect(x, y - 10.0f, 53.0f, 20.0f, 0.33f, 0.20f, 0.12f, 1.0f);
        renderer->DrawSolidDiamond(x, y - 29.0f, 66.0f, 25.0f, 0.48f, 0.16f, 0.12f, 1.0f);
        renderer->DrawSolidRect(x - 20.0f, y - 23.0f, 3.0f, 24.0f, 0.18f, 0.10f, 0.06f, 1.0f);
        renderer->DrawSolidRect(x + 20.0f, y - 23.0f, 3.0f, 24.0f, 0.18f, 0.10f, 0.06f, 1.0f);
    }

    void DrawJangseung(float x, float y)
    {
        renderer->DrawSolidRect(x, y - 28.0f, 11.0f, 50.0f, 0.31f, 0.19f, 0.10f, 1.0f);
        renderer->DrawSolidDiamond(x, y - 55.0f, 18.0f, 20.0f, 0.39f, 0.25f, 0.13f, 1.0f);
        renderer->DrawSolidRect(x, y - 56.0f, 8.0f, 2.0f, 0.08f, 0.05f, 0.03f, 1.0f);
    }

    void DrawLantern(float x, float y)
    {
        const float glow = 0.72f;
        renderer->DrawSolidRect(x, y - 19.0f, 3.0f, 26.0f, 0.16f, 0.10f, 0.06f, 1.0f);
        renderer->DrawSolidDiamond(x, y - 33.0f, 12.0f, 17.0f, 1.0f, glow, 0.25f, 0.95f);
    }

    void DrawStoneWell(float x, float y)
    {
        renderer->DrawSolidDiamond(x, y - 8.0f, 33.0f, 17.0f, 0.30f, 0.34f, 0.33f, 1.0f);
        renderer->DrawSolidDiamond(x, y - 13.0f, 23.0f, 11.0f, 0.07f, 0.12f, 0.15f, 1.0f);
        renderer->DrawSolidRect(x, y - 34.0f, 3.0f, 34.0f, 0.19f, 0.13f, 0.08f, 1.0f);
        renderer->DrawSolidRect(x, y - 44.0f, 28.0f, 3.0f, 0.19f, 0.13f, 0.08f, 1.0f);
    }

    void DrawFence(float x, float y, bool horizontal)
    {
        float width = horizontal ? 42.0f : 10.0f;
        float height = horizontal ? 8.0f : 36.0f;
        renderer->DrawSolidRect(x, y - 13.0f, width, height, 0.26f, 0.16f, 0.09f, 1.0f);
        if (horizontal)
        {
            renderer->DrawSolidRect(x - 16.0f, y - 18.0f, 3.0f, 22.0f, 0.17f, 0.10f, 0.05f, 1.0f);
            renderer->DrawSolidRect(x + 16.0f, y - 18.0f, 3.0f, 22.0f, 0.17f, 0.10f, 0.05f, 1.0f);
        }
    }

    void CharacterCloth(float x0,
                        float y0,
                        float x1,
                        float y1,
                        float x2,
                        float y2,
                        float x3,
                        float y3,
                        float r,
                        float g,
                        float b)
    {
        // Only executed when the versioned primitive cache is built.
        float cx = (x0 + x1 + x2 + x3) * .25f, cy = (y0 + y1 + y2 + y3) * .25f;
        renderer->SetMaterial(0);
        renderer->DrawQuad(cx + (x0 - cx) * 1.08f,
                           cy + (y0 - cy) * 1.055f,
                           cx + (x1 - cx) * 1.08f,
                           cy + (y1 - cy) * 1.055f,
                           cx + (x2 - cx) * 1.08f,
                           cy + (y2 - cy) * 1.055f,
                           cx + (x3 - cx) * 1.08f,
                           cy + (y3 - cy) * 1.055f,
                           .10f,
                           .13f,
                           .13f);
        renderer->SetMaterial(20);
        renderer->DrawQuad(x0, y0, x1, y1, x2, y2, x3, y3, r, g, b);
    }

    void CharacterFace(float y, int look)
    {
        float tone = float(look % 4) * .033f;
        float width = look == 4 ? 15.0f : look == 7 ? 20.0f : 18.0f;
        renderer->SetMaterial(0);
        renderer->DrawEllipse(-1, y - 3, width + 5, 23, .12f, .14f, .13f, 1);
        renderer->DrawEllipse(-width * .5f, y + 1, 5, 7, .67f - tone, .48f - tone, .31f - tone, 1);
        renderer->DrawEllipse(width * .5f, y + 1, 5, 7, .78f - tone, .59f - tone, .39f - tone, 1);
        renderer->SetMaterial(21);
        renderer->DrawSolidRect(1, y + 1, width, 21, .84f - tone, .66f - tone, .45f - tone, 1);
        renderer->SetMaterial(0);
        renderer->DrawEllipse(-2, y - 8, width + 1, 8, .14f, .15f, .13f, 1);
        renderer->DrawSolidRect(-2, y - 3, 4, 1.3f, .20f, .19f, .15f, 1);
        renderer->DrawSolidRect(5, y - 3, 4, 1.3f, .20f, .19f, .15f, 1);
        renderer->DrawSolidRect(-2, y, 1.5f, 2, .10f, .12f, .11f, 1);
        renderer->DrawSolidRect(5, y, 1.7f, 2, .10f, .12f, .11f, 1);
        renderer->DrawSolidRect(7, y + 3, 2, 3, .66f - tone, .45f - tone, .29f - tone, 1);
        renderer->DrawSolidRect(3, y + 7, 4, 1, .43f, .29f, .21f, 1);
        if (look == 11 || look == 17)
        {
            renderer->DrawEllipse(2, y + 9, 8, 5, .30f, .30f, .26f, .85f);
        }
    }

    void CharacterBody(float r,
                       float g,
                       float b,
                       float width,
                       float height,
                       int frame,
                       bool longCoat,
                       bool apron,
                       const CharacterPresentation::SwordPose* sword = nullptr,
                       float hemOverride = 0,
                       int identity = -1)
    {
        float phase = float(frame) * 6.2831853f / 8;
        float stride = sword ? sword->stance : std::sin(phase) * 3.5f;
        float bob = std::abs(std::sin(phase)) * .9f;
        float dip = sword ? sword->bodyDip : 0;
        float shoulder = -height * .64f - bob + dip, waist = -height * .37f - bob + dip * .5f;
        renderer->SetMaterial(0);
        // Opposing feet, visible soles and wrapped shins keep the gait grounded.
        renderer->DrawEllipse(-width * .23f + 1, -2 + stride, 10, 5, .08f, .12f, .13f, 1);
        renderer->DrawEllipse(width * .23f + 2, -2 - stride, 10, 5, .08f, .12f, .13f, 1);
        CharacterCloth(-width * .42f,
                       -23 + stride * .25f,
                       -1,
                       -22,
                       -2,
                       -5 + stride,
                       -width * .37f,
                       -5 + stride,
                       .52f,
                       .51f,
                       .41f);
        CharacterCloth(1,
                       -22,
                       width * .42f,
                       -24 - stride * .25f,
                       width * .38f,
                       -5 - stride,
                       2,
                       -5 - stride,
                       .66f,
                       .64f,
                       .51f);
        renderer->SetMaterial(0);
        renderer->DrawSolidRect(-width * .25f, -9 + stride, 6, 2, .76f, .72f, .57f, 1);
        renderer->DrawSolidRect(width * .26f, -9 - stride, 6, 2, .76f, .72f, .57f, 1);
        // Far sleeve is behind the robe; its hand swings opposite the near foot.
        CharacterCloth(-width * .42f,
                       shoulder + 1,
                       -width * .68f,
                       shoulder + 4,
                       -width * .72f,
                       waist + 6 - stride,
                       -width * .43f,
                       waist + 8 - stride,
                       r * .74f,
                       g * .76f,
                       b * .76f);
        renderer->SetMaterial(0);
        renderer->DrawEllipse(-width * .59f, waist + 10 - stride, 6, 7, .68f, .50f, .33f, 1);
        float hem = hemOverride != 0 ? hemOverride : longCoat ? -8.0f : -14.0f;
        CharacterCloth(-width * .44f,
                       shoulder,
                       1,
                       shoulder - 2,
                       1,
                       waist + 4,
                       -width * .5f,
                       waist + 7,
                       r * .80f,
                       g * .82f,
                       b * .82f);
        CharacterCloth(
            1, shoulder - 2, width * .44f, shoulder + 2, width * .52f, waist + 6, 1, waist + 4, r, g, b);
        CharacterCloth(-width * .49f,
                       waist + 1,
                       1,
                       waist + 1,
                       -1,
                       hem - stride * .28f,
                       -width * .62f,
                       hem - 3 + stride * .3f,
                       r * .73f,
                       g * .77f,
                       b * .78f);
        CharacterCloth(1,
                       waist + 1,
                       width * .5f,
                       waist + 2,
                       width * .63f,
                       hem - 1 - stride * .3f,
                       3,
                       hem + stride * .25f,
                       r * .90f,
                       g * .93f,
                       b * .94f);
        renderer->SetMaterial(0);
        // Crossed ivory collar and tied goreum, not a diamond painted over the chest.
        renderer->DrawQuad(
            -7, shoulder - 2, -3, shoulder - 4, 7, shoulder + 9, 3, shoulder + 10, .87f, .83f, .67f);
        renderer->DrawQuad(
            6, shoulder - 3, 9, shoulder - 1, 1, shoulder + 13, -2, shoulder + 10, .73f, .72f, .60f);
        renderer->DrawSolidRect(0, waist, width + 1, 3, .24f, .23f, .18f, 1);
        renderer->DrawSolidDiamond(5, shoulder + 13, 6, 4, .76f, .67f, .45f, 1);
        renderer->DrawQuad(5, shoulder + 13, 7, shoulder + 14, 9, waist + 2, 5, waist + 4, .64f, .57f, .39f);
        // Narrow seams and asymmetric folds keep all citizens from sharing a flat torso.
        renderer->DrawQuad(-width * .32f,
                           waist + 4,
                           -width * .27f,
                           waist + 3,
                           -width * .38f,
                           hem - 2,
                           -width * .42f,
                           hem - 2,
                           r * .57f,
                           g * .62f,
                           b * .65f);
        renderer->DrawQuad(width * .28f,
                           waist + 3,
                           width * .33f,
                           waist + 4,
                           width * .45f,
                           hem - 2,
                           width * .40f,
                           hem - 2,
                           r * 1.08f,
                           g * 1.06f,
                           b);
        float armX = sword ? sword->handX - 1 : width * .55f;
        float armY = sword ? sword->handY - 2 : waist + 8 + stride;
        CharacterCloth(width * .39f,
                       shoulder + 2,
                       width * .65f,
                       shoulder + 4,
                       armX + 4,
                       armY - 2,
                       armX - 3,
                       armY + 1,
                       r * .94f,
                       g * .95f,
                       b * .93f);
        renderer->SetMaterial(0);
        renderer->DrawEllipse(armX + 1, armY + 2, 7, 8, .82f, .63f, .42f, 1);
        if (apron)
        {
            CharacterCloth(-width * .35f,
                           waist + 2,
                           width * .33f,
                           waist + 2,
                           width * .38f,
                           -10,
                           -width * .40f,
                           -12,
                           identity == 0    ? .90f
                           : identity == 12 ? .67f
                                            : .54f,
                           identity == 0    ? .86f
                           : identity == 12 ? .22f
                                            : .52f,
                           identity == 0    ? .70f
                           : identity == 12 ? .18f
                                            : .40f);
            renderer->SetMaterial(0);
            renderer->DrawSolidRect(-4, -20, 6, 6, .48f, .43f, .31f, 1);
            renderer->DrawSolidRect(-4, -18, 5, 1, .79f, .72f, .51f, 1);
        }
    }

    void BuildActor(float r, float g, float b, bool hero, int frame, int attackPose = -1, int vertical = 0)
    {
        Perf::Scope scope(Perf::Cpu::CharacterBuild);
        Perf::Count(Perf::Counter::CharacterModelsBuilt);
        float phase = float(frame) * 6.2831853f / 8;
        float bob = std::abs(std::sin(phase)) * .9f;
        CharacterPresentation::SwordPose sword = CharacterPresentation::SampleSwordPose(attackPose, vertical);
        const auto* attack = attackPose >= 0 ? &sword : nullptr;
        float dip = attack ? sword.bodyDip : 0;
        // A weathered indigo overcoat, medicine satchel and tied scabbard identify the traveller.
        CharacterCloth(-11, -47 - bob, 3, -48 - bob, -8, -9, -22, -14, r * .55f, g * .60f, b * .66f);
        renderer->SetMaterial(3);
        renderer->DrawSolidRect(-15, -25, 12, 19, .39f, .31f, .20f, 1);
        renderer->SetMaterial(0);
        renderer->DrawSolidRect(-15, -31, 10, 4, .61f, .53f, .34f, 1);
        renderer->DrawSolidRect(-17, -22, 4, 6, .78f, .72f, .53f, 1);
        renderer->DrawQuad(-13, -32, -9, -33, -22, -5, -25, -7, .17f, .21f, .23f);
        CharacterBody(r, g, b, 24, 68, frame, true, false, attack);
        CharacterFace(-59 - bob + dip, 18);
        renderer->SetMaterial(0);
        renderer->DrawEllipse(-1, -69 - bob + dip, hero ? 37.0f : 25.0f, 8, .12f, .17f, .19f, 1);
        renderer->DrawSolidRect(-2, -75 - bob + dip, 17, 10, .15f, .21f, .23f, 1);
        renderer->DrawSolidRect(-2, -79 - bob + dip, 13, 2, .26f, .31f, .29f, 1);
        renderer->DrawQuad(-12, -46 - bob, -9, -47 - bob, -5, -26, -8, -25, .56f, .43f, .25f);
        renderer->DrawSolidRect(0, -25, 24, 3, .51f, .27f, .19f, 1);
        renderer->DrawSolidDiamond(5, -25, 6, 5, .75f, .60f, .34f, 1);
        renderer->DrawQuad(6, -24, 9, -23, 12, -14 + std::sin(phase), 8, -13, .61f, .33f, .22f);
        if (attackPose >= 0)
        {
            float hx = sword.handX, hy = sword.handY;
            float dx = std::cos(sword.angle), dy = std::sin(sword.angle);
            // The hilt is centred in the same hand used by the sleeve, not a floating pivot.
            renderer->DrawQuad(hx - dx * 6 - dy * 2,
                               hy - dy * 6 + dx * 2,
                               hx - dx * 6 + dy * 2,
                               hy - dy * 6 - dx * 2,
                               hx + dx * 3 + dy * 2,
                               hy + dy * 3 - dx * 2,
                               hx + dx * 3 - dy * 2,
                               hy + dy * 3 + dx * 2,
                               .29f,
                               .22f,
                               .16f);
            renderer->DrawQuad(hx + dx * 5 - dy * 2,
                               hy + dy * 5 + dx * 2,
                               hx + dx * 5 + dy * 2,
                               hy + dy * 5 - dx * 2,
                               hx + dx * 43,
                               hy + dy * 43,
                               hx + dx * 37 - dy * 2,
                               hy + dy * 37 + dx * 2,
                               .72f,
                               .78f,
                               .74f);
            renderer->DrawQuad(hx + dx * 3 - dy * 6,
                               hy + dy * 3 + dx * 6,
                               hx + dx * 3 + dy * 6,
                               hy + dy * 3 - dx * 6,
                               hx + dx * 5 + dy * 6,
                               hy + dy * 5 - dx * 6,
                               hx + dx * 5 - dy * 6,
                               hy + dy * 5 + dx * 6,
                               .62f,
                               .49f,
                               .29f);
            renderer->DrawEllipse(hx, hy, 6, 6, .82f, .63f, .42f, 1);
        }
    }

    void CitizenBackSilhouette(int look, float width, float height, float bob)
    {
        renderer->SetMaterial(20);
        if (look == 2 || look == 6 || look == 7 || look == 8)
        {
            // These broad, asymmetric silhouettes remain readable without colour or labels.
            float spread = look == 7 ? 15.0f : look == 8 ? 7.0f : 10.0f;
            float hem = look == 8 ? -3.0f : look == 6 ? -22.0f : -11.0f;
            float r = look == 2 ? .67f : look == 6 ? .24f : .19f;
            float g = look == 2 ? .19f : look == 6 ? .42f : .19f;
            CharacterCloth(-width * .6f - spread,
                           -height * .64f - bob,
                           width * .65f + spread,
                           -height * .60f - bob,
                           width * .7f,
                           hem,
                           -width * .8f - spread,
                           hem - 4,
                           r,
                           g,
                           .22f);
            if (look == 7)
            {
                renderer->SetMaterial(0);
                renderer->DrawEllipse(-12, -height * .65f, 30, 16, .58f, .49f, .33f, 1);
                renderer->DrawEllipse(12, -height * .65f, 30, 16, .58f, .49f, .33f, 1);
            }
        }
        if (look == 9 || look == 17)
        {
            renderer->SetMaterial(3);
            float yy = look == 17 ? -height + 8 : -height * .6f;
            renderer->DrawSolidRect(-width * .7f, yy + 15, 22, 36, .45f, .32f, .18f, 1);
            renderer->DrawSolidRect(-width * .7f, yy + 15, 25, 4, .83f, .72f, .44f, 1);
            renderer->DrawSolidRect(-width * .7f, yy + 15, 3, 38, .83f, .72f, .44f, 1);
        }
        if (look == 14 || look == 16)
        {
            renderer->SetMaterial(3);
            float xx = -width * .7f;
            renderer->DrawSolidRect(xx, -37, look == 16 ? 3.0f : 12.0f, 66, .61f, .45f, .25f, 1);
            if (look == 16)
            {
                renderer->DrawSolidDiamond(xx, -64, 14, 24, .69f, .52f, .30f, 1);
            }
            else
            {
                renderer->DrawSolidRect(xx, -24, 17, 3, .28f, .24f, .17f, 1);
                renderer->DrawSolidRect(xx, -46, 17, 3, .28f, .24f, .17f, 1);
            }
        }
        renderer->SetMaterial(0);
    }

    void CitizenIdentity(int look, float width, float head, float stride)
    {
        renderer->SetMaterial(0);
        switch (look)
        {
        case 0: // Mira: white work wrap, broad ivory apron, rolled waterway plans.
            renderer->DrawEllipse(-1, head - 8, 27, 13, .91f, .86f, .69f, 1);
            renderer->DrawSolidRect(-2, head - 5, 26, 4, .69f, .73f, .61f, 1);
            renderer->DrawQuad(-11, head - 5, -7, head - 4, -17, head + 16, -21, head + 12, .90f, .85f, .69f);
            renderer->DrawSolidRect(-width * .76f, -29 - stride, 10, 28, .88f, .82f, .62f, 1);
            renderer->DrawEllipse(-width * .76f, -43 - stride, 10, 5, .57f, .52f, .38f, 1);
            renderer->DrawSolidRect(-width * .76f, -28 - stride, 11, 3, .22f, .43f, .42f, 1);
            break;
        case 1: // Doyun: tall square clerk's cap and a large, blue bound ledger.
            renderer->DrawSolidRect(-1, head - 15, 19, 23, .12f, .19f, .25f, 1);
            renderer->DrawSolidRect(-1, head - 27, 23, 4, .22f, .30f, .37f, 1);
            renderer->DrawSolidRect(-1, head - 5, 25, 3, .12f, .19f, .25f, 1);
            renderer->DrawSolidRect(width * .72f, -28 + stride, 17, 23, .18f, .32f, .55f, 1);
            renderer->DrawSolidRect(width * .72f + 5, -28 + stride, 4, 20, .92f, .87f, .69f, 1);
            renderer->DrawSolidRect(width * .72f - 5, -28 + stride, 2, 23, .64f, .67f, .60f, 1);
            break;
        case 2: // Serat: high red plume, broad crimson shoulder plates, long spear.
            renderer->DrawSolidDiamond(1, head - 24, 11, 21, .81f, .23f, .16f, 1);
            renderer->DrawSolidRect(-14, -43, 16, 9, .70f, .22f, .17f, 1);
            renderer->DrawSolidRect(14, -42, 16, 9, .70f, .22f, .17f, 1);
            renderer->DrawSolidDiamond(0, -30, 10, 10, .89f, .72f, .36f, 1);
            break;
        case 3: // Ian: exceptionally broad pale gat, plum coat, brass tally board.
            renderer->DrawEllipse(-1, head - 9, 46, 8, .83f, .76f, .52f, 1);
            renderer->DrawSolidRect(-1, head - 17, 17, 13, .58f, .48f, .27f, 1);
            renderer->DrawSolidRect(-1, head - 23, 13, 2, .90f, .82f, .55f, 1);
            renderer->DrawSolidRect(width * .80f, -27 + stride, 17, 21, .33f, .23f, .14f, 1);
            for (int row = 0; row < 3; ++row)
            {
                renderer->DrawSolidRect(
                    width * .80f, -33 + stride + float(row) * 6, 13, 3, .84f, .65f, .30f, 1);
            }
            break;
        case 4: // Haram: small short coat, trailing red headband and oversized wooden mallet.
            renderer->DrawSolidRect(0, head - 5, 22, 5, .77f, .25f, .16f, 1);
            renderer->DrawQuad(-10, head - 5, -7, head - 2, -19, head + 16, -22, head + 13, .77f, .25f, .16f);
            renderer->DrawSolidRect(width * .8f, -22 + stride, 4, 28, .50f, .33f, .19f, 1);
            renderer->DrawSolidRect(width * .8f, -39 + stride, 20, 11, .66f, .45f, .25f, 1);
            renderer->DrawSolidRect(width * .8f - 6, -39 + stride, 3, 10, .86f, .70f, .42f, 1);
            break;
        case 5:  // Solgae: pointed straw hat; quiver remains visible behind the shoulder.
        case 16: // Boatman: flat straw brim and a tall paddle, not another bow silhouette.
            renderer->DrawSolidDiamond(
                -1, head - 11, look == 5 ? 36.0f : 43.0f, look == 5 ? 18.0f : 9.0f, .72f, .61f, .35f, 1);
            renderer->DrawSolidRect(-1, head - 6, look == 5 ? 36.0f : 43.0f, 3, .43f, .36f, .22f, 1);
            break;
        case 6: // Yeondu: uncovered hair, long braid and short green shoulder cloak.
            renderer->DrawEllipse(-7, head - 10, 18, 9, .13f, .18f, .14f, 1);
            renderer->DrawSolidRect(-10, head + 11, 5, 25, .14f, .19f, .15f, 1);
            renderer->DrawSolidRect(-10, head + 22, 8, 4, .85f, .72f, .38f, 1);
            break;
        case 7: // Meoksoe: heavy fur collar and a broad cleaver.
            renderer->DrawSolidRect(width * .8f, -17 + stride, 10, 21, .68f, .70f, .61f, 1);
            renderer->DrawSolidRect(width * .8f, -32 + stride, 4, 10, .29f, .21f, .16f, 1);
            break;
        case 8: // Heukbi: pointed hood and long split scarlet scarf.
            renderer->DrawSolidDiamond(-1, head - 10, 26, 24, .16f, .21f, .25f, 1);
            CharacterFace(head, look);
            renderer->DrawQuad(-9, head + 4, 9, head + 4, 7, head + 11, -7, head + 11, .61f, .22f, .20f);
            renderer->DrawQuad(-8, head + 8, -4, head + 9, -18, -8, -23, -15, .61f, .22f, .20f);
            break;
        case 9: // Jang: low orange cap and a broad basket on the back.
            renderer->DrawEllipse(-1, head - 9, 25, 12, .75f, .42f, .18f, 1);
            renderer->DrawSolidRect(-1, head - 5, 29, 3, .89f, .70f, .37f, 1);
            break;
        case 10: // Sundeok: pale triangular kerchief and a medicine case.
            renderer->DrawSolidDiamond(-1, head - 12, 29, 16, .92f, .87f, .72f, 1);
            renderer->DrawSolidRect(0, head - 3, 25, 3, .79f, .72f, .56f, 1);
            renderer->DrawSolidRect(width * .8f, -23 + stride, 19, 18, .47f, .27f, .30f, 1);
            renderer->DrawSolidRect(width * .8f, -27 + stride, 13, 4, .91f, .86f, .66f, 1);
            renderer->DrawSolidRect(width * .8f, -23 + stride, 4, 12, .91f, .86f, .66f, 1);
            break;
        case 11: // Scholar: black gat, white beard and scroll rather than the clerk's cap.
            renderer->DrawEllipse(1, head + 12, 11, 11, .78f, .79f, .69f, 1);
            renderer->DrawSolidRect(-width * .7f, -25 - stride, 7, 29, .90f, .86f, .70f, 1);
            renderer->DrawSolidRect(-width * .7f, -25 - stride, 9, 3, .34f, .35f, .27f, 1);
            break;
        case 12: // Innkeeper: large hair bun, red apron and a serving tray.
            renderer->DrawEllipse(-5, head - 16, 18, 14, .15f, .17f, .14f, 1);
            renderer->DrawSolidRect(-8, head - 16, 27, 2, .86f, .71f, .37f, 1);
            renderer->DrawEllipse(width * .8f, -24 + stride, 25, 8, .44f, .29f, .17f, 1);
            renderer->DrawEllipse(width * .8f, -29 + stride, 14, 10, .88f, .81f, .62f, 1);
            break;
        case 13: // Potter: low grey wrap and a large terracotta pot carried in front.
            renderer->DrawSolidRect(-1, head - 7, 26, 7, .64f, .68f, .64f, 1);
            renderer->DrawEllipse(width * .55f, -22 + stride, 23, 27, .68f, .35f, .22f, 1);
            renderer->DrawEllipse(width * .55f, -36 + stride, 16, 6, .34f, .23f, .16f, 1);
            break;
        case 14: // Repairman: knotted ochre wrap, planks and rope coils.
            renderer->DrawSolidRect(-1, head - 7, 25, 7, .73f, .60f, .30f, 1);
            renderer->DrawSolidDiamond(-13, head - 4, 12, 12, .73f, .60f, .30f, 1);
            renderer->DrawEllipse(width * .7f, -24 + stride, 17, 23, .72f, .59f, .33f, 1);
            renderer->DrawEllipse(width * .7f, -24 + stride, 9, 14, .31f, .37f, .29f, 1);
            break;
        case 15: // Patrolman: no tall plume, broad round shield.
            renderer->DrawEllipse(-width * .7f, -26 - stride, 24, 29, .31f, .38f, .42f, 1);
            renderer->DrawEllipse(-width * .7f, -26 - stride, 17, 22, .49f, .38f, .23f, 1);
            renderer->DrawSolidDiamond(-width * .7f, -26 - stride, 7, 9, .80f, .69f, .40f, 1);
            break;
        case 17: // Travelling merchant: rounded ochre hat and tall strapped baggage.
            renderer->DrawEllipse(-1, head - 10, 28, 16, .68f, .54f, .27f, 1);
            renderer->DrawEllipse(-1, head - 5, 35, 5, .35f, .31f, .20f, 1);
            break;
        }
        renderer->SetMaterial(0);
    }

    void BuildCitizen(int look, int frame)
    {
        Perf::Scope scope(Perf::Cpu::CharacterBuild);
        Perf::Count(Perf::Counter::CharacterModelsBuilt);

        struct Outfit
        {
            float r, g, b, width, height;
        };

        static const Outfit outfits[WorldLayout::NpcAppearanceCount] = {{.18f, .61f, .58f, 23, 47},
                                                                        {.78f, .82f, .71f, 13, 59},
                                                                        {.17f, .29f, .49f, 28, 60},
                                                                        {.61f, .26f, .50f, 24, 49},
                                                                        {.88f, .57f, .22f, 12, 36},
                                                                        {.29f, .46f, .27f, 21, 52},
                                                                        {.53f, .58f, .33f, 17, 46},
                                                                        {.30f, .19f, .17f, 25, 54},
                                                                        {.17f, .23f, .29f, 18, 50},
                                                                        {.70f, .44f, .24f, 23, 49},
                                                                        {.55f, .28f, .37f, 21, 44},
                                                                        {.71f, .74f, .67f, 15, 54},
                                                                        {.62f, .36f, .30f, 24, 47},
                                                                        {.46f, .53f, .54f, 22, 46},
                                                                        {.37f, .48f, .36f, 20, 53},
                                                                        {.27f, .43f, .63f, 22, 54},
                                                                        {.40f, .32f, .22f, 23, 50},
                                                                        {.62f, .49f, .24f, 18, 52}};
        const Outfit& outfit = outfits[look];
        float phase = float(frame) * 6.2831853f / 8;
        float stride = std::sin(phase) * 3.5f, bob = std::abs(std::sin(phase)) * .9f;
        float width = outfit.width + 2, height = outfit.height + 12;
        float head = -height + 9 - bob;
        bool guard = look == 2 || look == 15;
        bool raider = look == 7 || look == 8;
        bool hunter = look == 5 || look == 6;
        bool scholar = look == 1 || look == 11;
        bool apron = look == 0 || look == 4 || look == 12 || look == 13 || look == 14;
        CitizenBackSilhouette(look, width, height, bob);
        renderer->SetMaterial(0);
        if (raider)
        {
            CharacterCloth(-width * .4f,
                           -height * .7f,
                           width * .25f,
                           -height * .67f,
                           width * .3f,
                           -8,
                           -width * .85f,
                           -15,
                           .17f,
                           .20f,
                           .20f);
        }
        if (hunter)
        {
            renderer->SetMaterial(3);
            renderer->DrawQuad(-width * .7f,
                               -43,
                               -width * .5f,
                               -45,
                               -width * .40f,
                               -19,
                               -width * .67f,
                               -16,
                               .34f,
                               .27f,
                               .17f);
            for (int i = 0; i < 3; ++i)
            {
                float xx = -width * .65f + float(i) * 3;
                renderer->DrawQuad(xx, -48, xx + 1, -48, xx + 4, -21, xx + 3, -21, .63f, .55f, .34f);
                renderer->DrawSolidDiamond(xx, -49, 4, 6, .80f, .77f, .61f, 1);
            }
        }
        float hem = look == 1 || look == 3 || look == 11 ? -5.0f : look == 4 ? -20.0f : -13.0f;
        CharacterBody(
            outfit.r, outfit.g, outfit.b, width, height, frame, scholar || raider, apron, nullptr, hem, look);
        CharacterFace(head, look);
        renderer->SetMaterial(0);
        if (guard)
        {
            // Five rows of lamellar plates, a red helmet tassel and a metal-tipped spear.
            for (int row = 0; row < 5; ++row)
            {
                for (int column = 0; column < 3; ++column)
                {
                    renderer->DrawSolidRect(-7 + float(column) * 6,
                                            -height * .55f + float(row) * 3.8f,
                                            5,
                                            3,
                                            .26f + float(column) * .03f,
                                            .32f,
                                            .33f,
                                            1);
                }
            }
            renderer->DrawEllipse(0, head - 8, 26, 10, .18f, .24f, .27f, 1);
            renderer->DrawSolidRect(0, head - 12, 18, 9, .23f, .31f, .33f, 1);
            if (look == 2)
            {
                renderer->DrawSolidRect(1, head - 17, 3, 12, .66f, .24f, .16f, 1);
            }
            renderer->SetMaterial(3);
            renderer->DrawQuad(width * .8f,
                               -4,
                               width * .8f + 2,
                               -4,
                               width * .8f + 2,
                               -65,
                               width * .8f,
                               -65,
                               .46f,
                               .34f,
                               .21f);
            renderer->SetMaterial(0);
            renderer->DrawSolidDiamond(width * .8f + 1, -69, 7, 16, .66f, .73f, .70f, 1);
            renderer->DrawSolidRect(width * .8f + 1, -60, 5, 5, .64f, .26f, .17f, 1);
        }
        else if (raider)
        {
            renderer->DrawEllipse(-2, head - 7, 25, 10, .19f, .21f, .22f, 1);
            renderer->DrawQuad(-10,
                               head + 2,
                               10,
                               head + 3,
                               7,
                               head + 11,
                               -8,
                               head + 10,
                               look == 7 ? .51f : .32f,
                               .23f,
                               .19f);
            renderer->DrawSolidRect(0, -height * .36f, width, 4, .52f, .25f, .17f, 1);
            renderer->DrawQuad(width * .65f,
                               -25 + stride,
                               width * .65f + 4,
                               -26 + stride,
                               width * .85f + 5,
                               -8 + stride,
                               width * .85f,
                               -5 + stride,
                               .65f,
                               .69f,
                               .62f);
        }
        else if (scholar || look == 3)
        {
            if (look == 11)
            {
                renderer->DrawEllipse(-1, head - 8, 34, 6, .13f, .17f, .18f, 1);
                renderer->DrawSolidRect(-1, head - 13, 15, 10, .17f, .21f, .22f, 1);
                renderer->DrawSolidRect(-1, head - 17, 12, 2, .29f, .34f, .31f, 1);
            }
            renderer->DrawSolidRect(width * .67f,
                                    -22 + stride,
                                    10,
                                    16,
                                    look == 3 ? .52f : .78f,
                                    look == 3 ? .35f : .72f,
                                    .47f,
                                    1);
            renderer->DrawSolidRect(width * .67f - 3, -22 + stride, 2, 15, .37f, .35f, .24f, 1);
        }
        else if (hunter)
        {
            renderer->DrawEllipse(-3, head - 8, 19, 7, .24f, .28f, .19f, 1);
            renderer->DrawSolidRect(0, head - 6, 20, 3, .41f, .43f, .26f, 1);
            // Three wooden segments and a taut string give the bow an actual curved silhouette.
            renderer->DrawQuad(width * .7f,
                               -44,
                               width * .7f + 2,
                               -44,
                               width * .95f + 3,
                               -27,
                               width * .95f,
                               -27,
                               .53f,
                               .36f,
                               .21f);
            renderer->DrawQuad(width * .95f,
                               -27,
                               width * .95f + 3,
                               -27,
                               width * .7f + 2,
                               -9,
                               width * .7f,
                               -9,
                               .53f,
                               .36f,
                               .21f);
            renderer->DrawSolidRect(width * .7f, -26, 1, 35, .80f, .74f, .53f, 1);
        }
        else
        {
            renderer->DrawEllipse(-4, head - 12, 11, 9, .16f, .19f, .17f, 1);
            renderer->SetMaterial(20);
            renderer->DrawQuad(-11,
                               head - 9,
                               9,
                               head - 9,
                               10,
                               head - 3,
                               -10,
                               head - 4,
                               look == 0 ? .64f : outfit.r * .8f,
                               look == 0 ? .61f : outfit.g * .8f,
                               .43f);
        }
        if (apron && look != 4 && look != 12 && look != 13)
        {
            renderer->SetMaterial(3);
            renderer->DrawSolidRect(width * .65f, -18 + stride, 9, 13, .38f, .29f, .18f, 1);
            renderer->SetMaterial(0);
            renderer->DrawSolidRect(width * .65f, -30 + stride, 2, 15, .52f, .49f, .35f, 1);
            renderer->DrawSolidRect(
                width * .65f + 1, -36 + stride, 8, 5, look == 0 || look == 4 ? .52f : .68f, .58f, .46f, 1);
        }
        if (look == 3 || look == 9 || look == 10 || look == 17)
        {
            renderer->SetMaterial(0);
            renderer->DrawEllipse(-width * .60f, -17 - stride, 11, 14, .47f, .36f, .22f, 1);
            renderer->DrawSolidRect(-width * .60f, -22 - stride, 9, 2, .69f, .54f, .30f, 1);
            renderer->DrawSolidDiamond(-width * .60f, -17 - stride, 4, 5, .84f, .71f, .38f, 1);
        }
        CitizenIdentity(look, width, head, stride);
        renderer->SetMaterial(0);
    }

    void BuildAnimal(int kind, int frame)
    {
        float x = 0, y = 0;
        float step = std::sin(frame * 6.2831853f / 8) * 4;
        float c = kind == 0 ? .67f : kind == 1 ? .25f : .43f;
        for (int i = 0; i < 4; ++i)
        {
            renderer->DrawSolidRect(x - 12 + i * 8,
                                    y - 5 + ((i % 2) ? step : -step),
                                    3,
                                    kind == 0 ? 19.0f : 10.0f,
                                    c * .65f,
                                    c * .5f,
                                    c * .4f,
                                    1);
        }
        renderer->DrawEllipse(x, y - 18, 35, 20, c, c * .8f, c * .6f, 1);
        renderer->DrawEllipse(x + 17, y - 25, 16, 16, c, c * .82f, c * .66f, 1);
        renderer->DrawSolidDiamond(x + 12, y - 36, 7, 15, c, c * .8f, c * .6f, 1);
        renderer->DrawSolidDiamond(x + 21, y - 35, 6, 13, c, c * .8f, c * .6f, 1);
        renderer->DrawSolidRect(x + 21, y - 26, 2, 2, .06f, .05f, .04f, 1);
        if (kind == 0)
        {
            renderer->DrawSolidRect(x + 12, y - 42, 2, 17, .45f, .34f, .22f, 1);
            renderer->DrawSolidRect(x + 19, y - 42, 2, 17, .45f, .34f, .22f, 1);
        }
    }

    std::string FrameName(const std::string& prefix, int frame)
    {
        return prefix + std::to_string(frame);
    }

    void BuildProp(int kind)
    {
        switch (kind)
        {
        case 0:
            DrawGiwaHouse(0, 0);
            break;
        case 1:
            DrawChogaHouse(0, 0);
            break;
        case 2:
            DrawMarketStall(0, 0);
            break;
        case 3:
            DrawJangseung(0, 0);
            break;
        case 4:
            DrawStoneWell(0, 0);
            break;
        case 5:
            DrawLantern(0, 0);
            break;
        case 6:
            DrawFence(0, 0, true);
            break;
        case 7:
        case 16:
        case 17:
            DrawTree(0, 0, kind);
            break;
        case 8: // shore reeds
            renderer->SetMaterial(3);
            for (int i = -3; i <= 3; ++i)
            {
                float xx = float(i) * 4, top = -22.0f - float((i * i + 3) % 4) * 6;
                renderer->DrawQuad(xx,
                                   1,
                                   xx + 1.5f,
                                   1,
                                   xx + float(i) * 1.7f,
                                   top,
                                   xx + float(i) * 1.7f - 1,
                                   top,
                                   .41f,
                                   .43f,
                                   .25f);
                renderer->SetMaterial(13);
                renderer->DrawSolidRect(xx + 4, -11, 13, 5, .37f, .46f, .27f, 1);
                renderer->SetMaterial(3);
            }
            break;
        case 9: // shore stones with moss caps
            renderer->SetMaterial(0);
            renderer->DrawEllipse(0, -5, 40, 21, .32f, .36f, .32f, 1);
            renderer->DrawSolidDiamond(-3, -12, 31, 15, .49f, .51f, .41f, 1);
            renderer->DrawSolidDiamond(-8, -14, 18, 8, .36f, .44f, .29f, 1);
            renderer->DrawEllipse(17, -3, 14, 10, .43f, .45f, .37f, 1);
            break;
        case 10: // moored fishing boat (not walkable)
            renderer->SetMaterial(3);
            renderer->DrawQuad(-60, -4, -12, 19, 58, -11, 8, -29, .29f, .23f, .15f);
            renderer->DrawQuad(-60, -4, -12, 19, -10, 7, -51, -10, .42f, .32f, .20f);
            for (int i = -2; i <= 2; ++i)
            {
                float xx = float(i) * 17;
                renderer->DrawQuad(xx - 12, -12, xx + 12, -1, xx + 17, -4, xx - 7, -15, .52f, .39f, .23f);
            }
            renderer->DrawSolidRect(4, -29, 3, 57, .41f, .30f, .18f, 1);
            renderer->SetMaterial(14);
            renderer->DrawQuad(6, -54, 28, -41, 25, -17, 6, -24, .73f, .65f, .45f);
            break;
        case 11: // herbs drying above damp ground
            renderer->SetMaterial(3);
            renderer->DrawSolidRect(-24, -20, 3, 42, .32f, .25f, .16f, 1);
            renderer->DrawSolidRect(24, -20, 3, 42, .32f, .25f, .16f, 1);
            renderer->DrawQuad(-28, -34, 20, -14, 30, -20, -18, -40, .49f, .38f, .23f);
            renderer->SetMaterial(13);
            for (int i = 0; i < 5; ++i)
            {
                renderer->DrawSolidRect(-18 + float(i) * 9, -33 + float(i) * 3.5f, 9, 8, .45f, .49f, .25f, 1);
            }
            break;
        case 12: // linen hung after the storm
            renderer->SetMaterial(3);
            renderer->DrawSolidRect(-31, -24, 3, 50, .31f, .24f, .16f, 1);
            renderer->DrawSolidRect(31, -24, 3, 50, .31f, .24f, .16f, 1);
            renderer->DrawQuad(-32, -45, 32, -38, 32, -37, -32, -44, .40f, .33f, .21f);
            renderer->SetMaterial(14);
            for (int i = 0; i < 3; ++i)
            {
                float xx = -21 + float(i) * 20, yy = -42 + float(i) * 2;
                renderer->DrawQuad(xx - 7,
                                   yy,
                                   xx + 7,
                                   yy + 1,
                                   xx + 6,
                                   yy + 22,
                                   xx - 8,
                                   yy + 20,
                                   .71f - float(i) * .12f,
                                   .69f - float(i) * .09f,
                                   .54f,
                                   1);
            }
            break;
        case 13: // raised rice sacks and salvaged cargo
            renderer->SetMaterial(3);
            renderer->DrawSolidDiamond(0, 0, 49, 22, .32f, .25f, .16f, 1);
            renderer->SetMaterial(5);
            renderer->DrawSolidRect(-9, -12, 23, 25, .63f, .54f, .34f, 1);
            renderer->DrawSolidRect(12, -10, 22, 22, .55f, .47f, .29f, 1);
            renderer->SetMaterial(0);
            renderer->DrawSolidRect(-9, -12, 2, 22, .40f, .32f, .20f, 1);
            break;
        case 14: // medicine-shop cloth, no illegible pseudo-text
            renderer->SetMaterial(3);
            renderer->DrawSolidRect(0, -31, 3, 64, .28f, .24f, .16f, 1);
            renderer->SetMaterial(14);
            renderer->DrawQuad(2, -60, 23, -56, 22, -25, 2, -27, .80f, .71f, .49f);
            renderer->SetMaterial(0);
            renderer->DrawSolidRect(12, -43, 3, 17, .48f, .24f, .15f, 1);
            renderer->DrawSolidRect(12, -43, 13, 3, .48f, .24f, .15f, 1);
            break;
        case 15: // wooden walk on existing road tiles only
            renderer->SetMaterial(3);
            for (int i = 0; i < 6; ++i)
            {
                float a = float(i) / 6, b = float(i + 1) / 6 - .025f;
                renderer->DrawQuad(-48 + 48 * a,
                                   -24 * a,
                                   48 * a,
                                   24 - 24 * a,
                                   48 * b,
                                   24 - 24 * b,
                                   -48 + 48 * b,
                                   -24 * b,
                                   .42f,
                                   .34f,
                                   .22f);
            }
            break;
        }
    }

    void InitializeModels()
    {
        Perf::Scope scope(Perf::Cpu::ModelBuild);
        // Cache only local shape data: no GPU handles, camera positions or runtime state.
        wchar_t executable[32768] = {};
        DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
        std::wstring directory;
        if (length > 0 && length < 32768)
        {
            directory.assign(executable, length);
            directory.resize(directory.find_last_of(L"\\/") + 1);
        }
        directory += L"Cache";
        bool directoryReady =
            CreateDirectoryW(directory.c_str(), nullptr) != FALSE || GetLastError() == ERROR_ALREADY_EXISTS;
        std::wstring cache = directory + L"/harbor_models_v6.cache";

        std::vector<std::string> expected;
        for (int k = 0; k < 18; ++k)
        {
            expected.push_back(FrameName("prop", k));
        }
        for (int k = 0; k < 8; ++k)
        {
            expected.push_back(FrameName("hero", k));
        }
        for (const char* prefix : {"hero_attack", "hero_attack_up", "hero_attack_down"})
        {
            for (int pose = 0; pose < CharacterPresentation::AttackFrames; ++pose)
            {
                expected.push_back(FrameName(prefix, pose));
            }
        }
        for (int k = 0; k < WorldLayout::NpcAppearanceCount; ++k)
        {
            for (int frame = 0; frame < 8; ++frame)
            {
                expected.push_back("citizen" + std::to_string(k) + "_" + std::to_string(frame));
            }
        }
        for (const char* name :
             {"drain", "sluice", "sluice_open", "notice", "wreck", "ledger", "key", "corpse", "bandit_sign"})
        {
            expected.push_back(name);
        }
        for (int k = 0; k < 3; ++k)
        {
            for (int f = 0; f < 8; ++f)
            {
                expected.push_back(FrameName("beast" + std::to_string(k) + "_", f));
            }
        }
        for (int k = 0; k < 3; ++k)
        {
            expected.push_back(FrameName("loot", k));
        }
        for (int k = 0; k < 6; ++k)
        {
            expected.push_back(FrameName("tile", k));
        }

        bool loaded = directoryReady && renderer->LoadModels(cache);
        for (const auto& name : expected)
        {
            loaded = loaded && renderer->HasModel(name);
        }
        if (loaded)
        {
            std::cout << "Loaded model cache: " << expected.size() << " models.\n";
            return;
        }

        for (int kind = 0; kind < 18; ++kind)
        {
            renderer->BeginModel(FrameName("prop", kind));
            BuildProp(kind);
            renderer->EndModel();
        }
        for (int frame = 0; frame < 8; ++frame)
        {
            renderer->BeginModel(FrameName("hero", frame));
            BuildActor(.38f, .46f, .60f, true, frame);
            renderer->EndModel();
        }
        for (int vertical = -1; vertical <= 1; ++vertical)
        {
            const char* prefix = vertical < 0   ? "hero_attack_up"
                                 : vertical > 0 ? "hero_attack_down"
                                                : "hero_attack";
            for (int pose = 0; pose < CharacterPresentation::AttackFrames; ++pose)
            {
                renderer->BeginModel(FrameName(prefix, pose));
                BuildActor(.38f, .46f, .60f, true, 0, pose, vertical);
                renderer->EndModel();
            }
        }
        for (int look = 0; look < WorldLayout::NpcAppearanceCount; ++look)
        {
            for (int frame = 0; frame < 8; ++frame)
            {
                renderer->BeginModel("citizen" + std::to_string(look) + "_" + std::to_string(frame));
                BuildCitizen(look, frame);
                renderer->EndModel();
            }
        }
        for (int kind = 0; kind < 3; ++kind)
        {
            for (int frame = 0; frame < 8; ++frame)
            {
                renderer->BeginModel(FrameName("beast" + std::to_string(kind) + "_", frame));
                BuildAnimal(kind, frame);
                renderer->EndModel();
            }
        }
        for (int kind = 0; kind < 3; ++kind)
        {
            renderer->BeginModel(FrameName("loot", kind));
            if (kind == 0)
            {
                renderer->DrawEllipse(0, -7, 15, 10, .86f, .67f, .24f, 1);
                renderer->DrawSolidRect(0, -7, 3, 3, .23f, .17f, .06f, 1);
            }
            else if (kind == 1)
            {
                renderer->DrawEllipse(0, -10, 14, 20, .21f, .52f, .42f, 1);
                renderer->DrawSolidRect(0, -19, 7, 5, .58f, .40f, .22f, 1);
                renderer->DrawSolidRect(0, -10, 9, 6, .80f, .75f, .59f, 1);
            }
            else
            {
                renderer->DrawSolidDiamond(0, -8, 23, 16, .55f, .37f, .21f, 1);
                renderer->DrawSolidRect(0, -8, 3, 13, .27f, .18f, .11f, 1);
            }
            renderer->EndModel();
        }
        for (int kind = 0; kind < 6; ++kind)
        {
            renderer->BeginModel(FrameName("tile", kind));
            renderer->SetMaterial(kind == 4 ? 2 : kind == 5 ? 6 : 1);
            // One terrain instance; continuous surface detail is evaluated in world space.
            renderer->DrawSolidDiamond(0,
                                       0,
                                       TileWidth + 1,
                                       TileHeight + 1,
                                       kind == 5   ? .12f
                                       : kind == 4 ? .43f
                                                   : .27f,
                                       kind == 5   ? .29f
                                       : kind == 4 ? .42f
                                                   : .36f,
                                       kind == 5   ? .31f
                                       : kind == 4 ? .33f
                                                   : .23f,
                                       1);
            renderer->EndModel();
        }

        renderer->BeginModel("drain");
        renderer->SetMaterial(2);
        renderer->DrawSolidDiamond(0, -6, 92, 36, .24f, .27f, .27f, 1);
        renderer->DrawSolidRect(0, -21, 70, 32, .09f, .14f, .15f, 1);
        for (int k = -2; k <= 2; ++k)
        {
            renderer->DrawSolidRect(k * 13.0f, -21, 3, 32, .36f, .39f, .37f, 1);
        }
        renderer->EndModel();
        for (int open = 0; open < 2; ++open)
        {
            renderer->BeginModel(open ? "sluice_open" : "sluice");
            renderer->SetMaterial(2);
            renderer->DrawSolidRect(-39, -37, 14, 78, .29f, .32f, .30f, 1);
            renderer->DrawSolidRect(39, -37, 14, 78, .29f, .32f, .30f, 1);
            renderer->SetMaterial(6);
            renderer->DrawSolidDiamond(0, 0, 78, 32, .08f, .29f, .35f, 1);
            renderer->SetMaterial(3);
            renderer->DrawSolidRect(0, open ? -65.0f : -31.0f, 66, open ? 18.0f : 66.0f, .30f, .21f, .14f, 1);
            renderer->DrawSolidRect(0, -76, 96, 11, .26f, .20f, .15f, 1);
            renderer->EndModel();
        }
        renderer->BeginModel("notice");
        renderer->SetMaterial(3);
        renderer->DrawSolidRect(-19, -30, 7, 58, .30f, .22f, .15f, 1);
        renderer->DrawSolidRect(19, -30, 7, 58, .30f, .22f, .15f, 1);
        renderer->DrawSolidRect(0, -42, 49, 33, .35f, .27f, .18f, 1);
        renderer->SetMaterial(0);
        renderer->DrawSolidRect(0, -43, 29, 22, .76f, .70f, .53f, 1);
        renderer->EndModel();
        renderer->BeginModel("wreck");
        renderer->SetMaterial(3);
        renderer->DrawSolidDiamond(0, -9, 146, 55, .24f, .18f, .13f, 1);
        for (int k = -3; k <= 3; ++k)
        {
            renderer->DrawSolidRect(k * 16.0f, -24, 6, 30 + std::abs(k) * 5.0f, .37f, .28f, .18f, 1);
        }
        renderer->DrawSolidRect(18, -67, 7, 100, .31f, .24f, .16f, 1);
        renderer->EndModel();
        renderer->BeginModel("ledger");
        renderer->SetMaterial(0);
        renderer->DrawSolidRect(0, -9, 19, 21, .26f, .36f, .45f, 1);
        renderer->DrawSolidRect(-6, -9, 3, 21, .77f, .71f, .53f, 1);
        renderer->DrawSolidRect(2, -9, 8, 11, .70f, .65f, .49f, 1);
        renderer->EndModel();
        renderer->BeginModel("key");
        renderer->DrawEllipse(0, -18, 10, 10, .79f, .64f, .25f, 1);
        renderer->DrawSolidRect(0, -7, 3, 17, .79f, .64f, .25f, 1);
        renderer->DrawSolidRect(4, -2, 7, 3, .79f, .64f, .25f, 1);
        renderer->EndModel();
        renderer->BeginModel("bandit_sign");
        renderer->SetMaterial(3);
        renderer->DrawSolidRect(0, -38, 8, 76, .21f, .14f, .10f, 1);
        renderer->SetMaterial(0);
        renderer->DrawSolidDiamond(0, -52, 42, 45, .58f, .12f, .10f, 1);
        renderer->DrawSolidRect(0, -52, 4, 24, .9f, .68f, .45f, 1);
        renderer->DrawSolidRect(0, -37, 5, 4, .9f, .68f, .45f, 1);
        renderer->EndModel();
        renderer->BeginModel("corpse");
        renderer->DrawEllipse(0, -2, 38, 14, .32f, .33f, .30f, 1);
        renderer->DrawEllipse(17, -5, 11, 10, .62f, .46f, .32f, 1);
        renderer->EndModel();

        if (!directoryReady || !renderer->SaveModels(cache))
        {
            std::cerr << "Model cache could not be written; using in-memory models.\n";
        }
        else
        {
            std::cout << "Generated and saved model cache.\n";
        }
    }

    void InitializeSceneActors()
    {
        SceneGraph& scene = firstLevel.Scene();
        harbor.Initialize();
        for (Harbor::Landmark* site : scene.Actors<Harbor::Landmark>(ActorKind::Prop))
        {
            Actor& label = scene.Create(site->Id(), "SiteLabel", ActorKind::UI);
            label.pass = ScenePass::Overlay;
            label.variant = int(UiPart::SiteLabel);
            label.drawable = true;
            label.SetLocalPosition({0, 0, 105});
        }
        for (const Npc* npc : scene.Actors<Npc>(ActorKind::Npc))
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
            renderer->DrawSolidRect(x, y + 16, 130, 95, 1, .54f, .19f, .11f);
            renderer->SetMaterial(8);
            renderer->DrawSolidRect(x, y, 32, 52, 1, 1, 1, .92f);
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
                int vertical = CharacterPresentation::AttackDirection(firstLevel.facingY);
                auto pose = CharacterPresentation::SampleSwordPose(frame, vertical);
                float flip = firstLevel.Player().mirrorModel ? -1.0f : 1.0f;
                // Narrow ribbons follow the actual blade. No glow during wind-up or recovery.
                for (int k = 0; k < 4 && pose.trailStrength > .01f; ++k)
                {
                    float a = pose.angle - float(k) * .16f;
                    float b = a - .18f;
                    float hx = x + flip * pose.handX, hy = y + pose.handY;
                    renderer->DrawQuad(hx + flip * std::cos(a) * 17,
                                       hy + std::sin(a) * 17,
                                       hx + flip * std::cos(a) * 43,
                                       hy + std::sin(a) * 43,
                                       hx + flip * std::cos(b) * 43,
                                       hy + std::sin(b) * 43,
                                       hx + flip * std::cos(b) * 17,
                                       hy + std::sin(b) * 17,
                                       .84f,
                                       .91f,
                                       .89f,
                                       pose.trailStrength * (.42f - float(k) * .085f));
                }
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
                    renderer->DrawSolidRect(x, y - 60, 42, 5, .07f, .055f, .05f, 1);
                    float width = 40.0f * enemy->health / enemy->maxHealth;
                    if (width > 0)
                    {
                        renderer->DrawSolidRect(x - 20 + width * .5f, y - 60, width, 3, .73f, .25f, .18f, 1);
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
                                    renderer->DrawModel(actor.model, x, y, actor.mirrorModel);
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
        std::wstring title =
            worldMap ? L"\uc804\uccb4 \uc9c0\ub3c4 256x256 [M]" : L"\uc8fc\ubcc0 \uc9c0\ub3c4 64x64 [M]";
        DrawText(1040, 38, title.c_str(), .97f, .89f, .67f);
        DrawText(1040, 290, L"\uae08\ube5b \ubaa9\ud45c / \ud770\uc0c9 \ub098", .97f, .9f, .7f);
        DrawText(1040,
                 313,
                 worldMap ? L"\ubd89\uc740 \uac70\uc810: \uc57d\ud0c8\uc790"
                          : L"\uc804\uccb4 256x256 \uc911 \uc77c\ubd80",
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
    InitializeModels();
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
