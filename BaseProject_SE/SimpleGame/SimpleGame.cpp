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
        GuideHelp
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

    void DrawTree(float x, float y)
    {
        renderer->DrawSolidRect(x, y - 22.0f, 8.0f, 35.0f, 0.23f, 0.16f, 0.10f, 1.0f);
        renderer->DrawSolidDiamond(x, y - 43.0f, 40.0f, 52.0f, 0.11f, 0.25f, 0.18f, 1.0f);
        renderer->DrawSolidDiamond(x - 8.0f, y - 35.0f, 28.0f, 38.0f, 0.16f, 0.33f, 0.22f, 1.0f);
    }

    void DrawHouse(float x, float y, bool straw)
    {
        // Raised foundation, two lit facades and pitched roof planes share one ground anchor.
        renderer->SetMaterial(2);
        renderer->DrawQuad(x - 58, y - 17, x + 10, y + 13, x + 55, y - 12, x - 12, y - 40, .31f, .34f, .33f);
        renderer->SetMaterial(3);
        renderer->DrawQuad(x - 53, y - 20, x + 10, y + 7, x + 10, y - 49, x - 53, y - 76, .56f, .46f, .31f);
        renderer->DrawQuad(x + 10, y + 7, x + 51, y - 15, x + 51, y - 71, x + 10, y - 49, .32f, .31f, .24f);
        // Timber posts and warm paper lattice windows.
        renderer->SetMaterial(0);
        for (int i = 0; i < 3; ++i)
        {
            float px = x - 43 + i * 19, py = y - 41 + i * 8;
            renderer->DrawQuad(px, py - 17, px + 13, py - 11, px + 13, py + 12, px, py + 6, .77f, .66f, .43f);
            for (int k = 0; k < 3; ++k)
            {
                renderer->DrawSolidRect(px + 2 + k * 4, py - 4 + k * 1.5f, 1, 22, .28f, .22f, .15f, 1);
            }
            renderer->DrawSolidRect(px - 5, py - 5, 3, 47, .22f, .17f, .11f, 1);
        }
        renderer->SetMaterial(straw ? 5 : 4);
        renderer->DrawQuad(x - 71,
                           y - 73,
                           x - 16,
                           y - 109,
                           x + 67,
                           y - 73,
                           x + 12,
                           y - 39,
                           straw ? .59f : .22f,
                           straw ? .47f : .29f,
                           straw ? .26f : .31f);
        renderer->DrawQuad(x - 71,
                           y - 73,
                           x + 12,
                           y - 39,
                           x + 12,
                           y - 33,
                           x - 71,
                           y - 66,
                           straw ? .40f : .11f,
                           straw ? .31f : .16f,
                           straw ? .17f : .18f);
        renderer->DrawQuad(x + 12,
                           y - 39,
                           x + 67,
                           y - 73,
                           x + 67,
                           y - 66,
                           x + 12,
                           y - 33,
                           straw ? .35f : .09f,
                           straw ? .28f : .13f,
                           straw ? .14f : .16f);
        renderer->SetMaterial(0);
        // Roof ridge and upturned end caps.
        renderer->DrawQuad(
            x - 21, y - 111, x + 61, y - 77, x + 65, y - 80, x - 17, y - 116, .35f, .40f, .40f);
        renderer->DrawSolidDiamond(x - 70, y - 72, 10, 9, .27f, .34f, .35f, 1);
        renderer->DrawSolidDiamond(x + 68, y - 73, 10, 9, .27f, .34f, .35f, 1);
        // Earthenware onggi beside the foundation.
        for (int k = 0; k < 3; ++k)
        {
            renderer->DrawEllipse(x - 65 + k * 9, y - 9 + k * 4, 12, 16, .22f, .14f, .09f, 1);
            renderer->DrawEllipse(x - 65 + k * 9, y - 16 + k * 4, 12, 5, .13f, .09f, .06f, 1);
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

    void BuildActor(float r, float g, float b, bool hero, int frame)
    {
        float x = 0, y = 0;
        float phase = frame * 6.2831853f / 8;
        float stride = hero ? std::sin(phase) * 3 : 0;
        y -= hero ? std::abs(std::sin(phase)) * 1.4f : 0;
        // Layered hanbok silhouette: shoes, trousers, robe, sleeves, collar, head and gat.
        renderer->DrawSolidRect(x - 4, y - 4 + stride, 6, 9, .12f, .13f, .13f, 1);
        renderer->DrawSolidRect(x + 4, y - 4 - stride, 6, 9, .12f, .13f, .13f, 1);
        renderer->DrawSolidDiamond(x, y - 16, 23, 28, r * .7f, g * .7f, b * .7f, 1);
        renderer->DrawSolidRect(x, y - 25, 15, 17, r, g, b, 1);
        renderer->DrawSolidRect(x - 11, y - 23 - stride, 7, 16, r * .85f, g * .85f, b * .85f, 1);
        renderer->DrawSolidRect(x + 11, y - 23 + stride, 7, 16, r * .85f, g * .85f, b * .85f, 1);
        renderer->DrawSolidDiamond(x, y - 28, 9, 12, .85f, .81f, .66f, 1);
        renderer->DrawSolidRect(x, y - 20, 16, 3, .19f, .17f, .13f, 1);
        renderer->DrawEllipse(x, y - 40, 13, 16, .70f, .53f, .36f, 1);
        renderer->DrawSolidRect(x + 3, y - 40, 2, 2, .12f, .09f, .08f, 1);
        renderer->DrawEllipse(x, y - 47, hero ? 31.0f : 18.0f, 7, .08f, .10f, .12f, 1);
        renderer->DrawSolidRect(x, y - 51, hero ? 14.0f : 10.0f, 8, .09f, .12f, .14f, 1);
        if (hero)
        {
            renderer->DrawSolidRect(x + 15, y - 19, 3, 26, .28f, .23f, .19f, 1);
        }
    }

    void BuildCitizen(int look, int frame)
    {
        struct Outfit
        {
            float r, g, b, width, height;
        };

        static const Outfit outfits[WorldLayout::NpcAppearanceCount] = {{.25f, .52f, .53f, 19, 47},
                                                                        {.36f, .43f, .63f, 15, 51},
                                                                        {.18f, .32f, .56f, 23, 57},
                                                                        {.55f, .29f, .57f, 19, 50},
                                                                        {.73f, .49f, .28f, 14, 40},
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
        float stride = std::sin(frame * 6.2831853f / 8) * 3;
        float bob = std::abs(stride) * .4f, head = -outfit.height - bob;
        float width = outfit.width;
        bool guard = look == 2 || look == 15;
        bool raider = look == 7 || look == 8;
        bool hunter = look == 5 || look == 6 || look == 16;
        bool scholar = look == 1 || look == 11;
        bool apron = look == 0 || look == 4 || look == 12 || look == 13 || look == 14;
        renderer->SetMaterial(0);
        renderer->DrawSolidRect(-width * .23f, -5 + stride, 7, 11, .11f, .12f, .12f, 1);
        renderer->DrawSolidRect(width * .23f, -5 - stride, 7, 11, .11f, .12f, .12f, 1);
        renderer->DrawSolidDiamond(0,
                                   head * .42f,
                                   width + (scholar ? 9 : 3),
                                   outfit.height * .59f,
                                   outfit.r * .77f,
                                   outfit.g * .77f,
                                   outfit.b * .77f,
                                   1);
        renderer->DrawSolidRect(0, head * .57f, width, outfit.height * .33f, outfit.r, outfit.g, outfit.b, 1);
        renderer->DrawSolidRect(
            -width * .62f, head * .56f - stride, 7, 18, outfit.r * .83f, outfit.g * .83f, outfit.b * .83f, 1);
        renderer->DrawSolidRect(
            width * .62f, head * .56f + stride, 7, 18, outfit.r * .83f, outfit.g * .83f, outfit.b * .83f, 1);
        renderer->DrawSolidDiamond(0, head * .69f, 8, 12, .83f, .79f, .65f, 1);
        renderer->DrawSolidRect(0, head * .43f, width + 1, 3, raider ? .65f : .24f, .18f, .12f, 1);
        float skin = (look % 4) * .045f;
        renderer->DrawEllipse(
            0, head + 9, look == 4 ? 12.0f : 15.0f, 17, .76f - skin, .59f - skin, .42f - skin, 1);
        renderer->DrawSolidRect(4, head + 8, 2, 2, .10f, .08f, .07f, 1);
        if (guard)
        {
            renderer->DrawSolidRect(0, head * .58f, width - 4, 13, .23f, .29f, .34f, 1);
            renderer->DrawEllipse(0, head, 23, 13, .12f, .18f, .25f, 1);
            renderer->DrawSolidRect(3, head - 9, 4, 15, .70f, .18f, .14f, 1);
            renderer->DrawSolidRect(width + 1, -25, 3, 55, .43f, .29f, .16f, 1);
            renderer->DrawSolidDiamond(width + 1, -56, 8, 15, .64f, .70f, .71f, 1);
        }
        else if (raider)
        {
            renderer->DrawSolidDiamond(
                -7, head * .47f, width + 18, outfit.height * .77f, .16f, .13f, .12f, .95f);
            renderer->DrawEllipse(0, head + 3, 23, 23, .15f, .17f, .18f, 1);
            renderer->DrawSolidRect(0, head + 12, 16, 8, look == 7 ? .55f : .31f, .15f, .12f, 1);
            renderer->DrawSolidRect(3, head + 5, 3, 2, .79f, .70f, .42f, 1);
            renderer->DrawSolidDiamond(width + 2, -25, 8, 31, .68f, .66f, .57f, 1);
        }
        else if (scholar)
        {
            renderer->DrawEllipse(0, head - 1, 35, 7, .12f, .13f, .14f, 1);
            renderer->DrawSolidRect(0, head - 6, 14, 12, .15f, .16f, .17f, 1);
            renderer->DrawSolidRect(width * .67f, -23, 10, 16, .72f, .69f, .54f, 1);
        }
        else
        {
            renderer->DrawSolidRect(
                0, head - 1, width * .95f, 6, hunter ? .28f : outfit.r * .8f, .28f, .23f, 1);
            renderer->DrawEllipse(-3, head - 7, 9, 10, .12f, .12f, .10f, 1);
        }
        if (hunter)
        {
            renderer->DrawSolidDiamond(width + 1, -26, 12, 44, .48f, .32f, .17f, 1);
            renderer->DrawSolidRect(width + 1, -26, 2, 39, .84f, .77f, .58f, 1);
            renderer->DrawSolidRect(-width * .5f, -25, 8, 21, .31f, .24f, .16f, 1);
        }
        if (apron)
        {
            renderer->DrawSolidDiamond(0, head * .34f, width - 4, 21, .64f, .58f, .42f, 1);
            renderer->DrawSolidRect(width * .64f, -21, 7, 12, .31f, .22f, .13f, 1);
        }
        if (look == 3 || look == 9 || look == 10 || look == 17)
        {
            renderer->DrawEllipse(width * .64f, -15, 13, 15, .55f, .40f, .20f, 1);
            renderer->DrawSolidRect(width * .64f, -16, 3, 3, .86f, .72f, .29f, 1);
        }
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
            DrawTree(0, 0);
            renderer->SetMaterial(0);
            for (int k = 0; k < 3; ++k)
            {
                renderer->DrawEllipse(static_cast<float>((k - 1) * 15),
                                      static_cast<float>(-52 - k * 9),
                                      47,
                                      35,
                                      .12f + k * .018f,
                                      .25f + k * .02f,
                                      .19f,
                                      1);
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
        std::wstring cache = directory + L"/harbor_models_v3.cache";

        std::vector<std::string> expected;
        for (int k = 0; k < 8; ++k)
        {
            expected.push_back(FrameName("prop", k));
        }
        for (int k = 0; k < 8; ++k)
        {
            expected.push_back(FrameName("hero", k));
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

        for (int kind = 0; kind < 8; ++kind)
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
            float n = kind * .008f;
            renderer->DrawSolidDiamond(0,
                                       0,
                                       TileWidth + 1,
                                       TileHeight + 1,
                                       kind == 5   ? .06f
                                       : kind == 4 ? .39f
                                                   : .18f + n,
                                       kind == 5   ? .22f
                                       : kind == 4 ? .36f
                                                   : .28f + n,
                                       kind == 5   ? .28f
                                       : kind == 4 ? .28f
                                                   : .19f + n,
                                       1);
            if (kind < 4)
            {
                renderer->SetMaterial(0);
                for (int k = 0; k < 3; ++k)
                {
                    renderer->DrawSolidDiamond(static_cast<float>((k - 1) * 14),
                                               static_cast<float>(-3 + k * 3),
                                               3,
                                               9,
                                               .25f,
                                               .36f,
                                               .21f,
                                               .6f);
                }
            }
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
            label.SetLocalPosition({0, 0, 93});
        }
        scene.DestroyChildren(firstLevel.OverlayRoot());
        for (UiPart part : {UiPart::Hud,
                            UiPart::MiniMap,
                            UiPart::Stats,
                            UiPart::Journal,
                            UiPart::Pause,
                            UiPart::QuestTracker,
                            UiPart::GuideHelp})
        {
            Actor& ui = scene.Create(firstLevel.OverlayRoot(), "UIPanel", ActorKind::UI);
            ui.variant = int(part);
            ui.drawable = true;
            ui.pass = ScenePass::Overlay;
            ui.drawOrder = part == UiPart::GuideHelp ? 200 : part == UiPart::Pause ? 100 : int(part);
        }
        helpVisible = true;
        helpPressed = continueHelpPressed = false;
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
        prompt->SetVisible(target != InvalidActorId && !paused);
        if (target)
        {
            scene.Reparent(promptId, target, false);
            prompt->SetLocalPosition({0, 0, 70});
        }
    }

    void DrawEffectActor(const Actor& actor, float x, float y)
    {
        switch (ActorEffect(actor.variant))
        {
        case ActorEffect::Flame:
            renderer->DrawEllipse(x, y + 13, 95, 80, 1, .52f, .13f, .08f);
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
                float progress = 1 - firstLevel.attackAnimation / .22f;
                float angle = std::atan2(firstLevel.facingY, firstLevel.facingX) + (progress - .5f) * 2.5f;
                for (int k = 0; k < 5; ++k)
                {
                    float a = angle - k * .13f;
                    renderer->DrawEllipse(x + std::cos(a) * 52,
                                          y - 18 + std::sin(a) * 36,
                                          17,
                                          9,
                                          .95f,
                                          .89f,
                                          .67f,
                                          .8f - k * .13f);
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
                    renderer->DrawSolidRect(x, y - 64, 42, 5, .07f, .055f, .05f, 1);
                    float width = 40.0f * npc->health / npc->maxHealth;
                    renderer->DrawSolidRect(x - 20 + width * .5f,
                                            y - 64,
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

    void RenderWorldPass(ScenePass pass)
    {
        Perf::Scope scope(pass == ScenePass::Ground  ? Perf::Cpu::RenderGround
                          : pass == ScenePass::World ? Perf::Cpu::RenderWorld
                                                     : Perf::Cpu::RenderEffects);
        const SceneGraph& scene = firstLevel.Scene();
        if (pass == ScenePass::World)
        {
            Perf::Scope shadows(Perf::Cpu::RenderShadow);
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
                                    renderer->DrawEllipse(
                                        x + 14, y + 5, actor.shadowWidth, 22, .025f, .045f, .055f, .30f);
                                    renderer->DrawEllipse(
                                        x + 3, y + 1, actor.shadowWidth * .55f, 12, .02f, .03f, .035f, .36f);
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
                                if (!actor.model.empty())
                                {
                                    renderer->DrawModel(actor.model, x, y);
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

    void DrawGuideHelp()
    {
        if (!helpVisible)
        {
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
                L"\uc804\ubd80 \uc678\uc6b8 \ud544\uc694\ub294 \uc5c6\uc5b4\uc694. \uc77c\uc744 \ub9c8\uce58\uba74 \ud654\uba74 \uc704\uc5d0 \ub2e4\uc74c \ud589\ub3d9\uc774 \ub098\ud0c0\ub0a9\ub2c8\ub2e4.",
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

        if (!journalPanel)
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
        bool blocked = paused || helpVisible || journalPanel || statsPanel || bounds.left < 20 ||
                       bounds.right > 1000 || bounds.top < 226 || bounds.bottom > 540;
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

    void QuestMarkerPosition(const Actor& marker, float& x, float& y, bool& edge)
    {
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
                worldLabelBounds.push_back({x - 145, y - 17, x + 145, y + 42});
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
                    if (paused || helpVisible || journalPanel || statsPanel)
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

    void Update(float dt)
    {
        Perf::Scope scope(Perf::Cpu::Update);
        if (helpPressed)
        {
            helpVisible = true;
            helpPressed = false;
            journalPanel = statsPanel = false;
        }
        if (helpVisible && continueHelpPressed)
        {
            helpVisible = false;
            continueHelpPressed = false;
            std::fill(std::begin(keys), std::end(keys), false);
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
        harbor.Update(dt);
        if (keys[' '])
        {
            if (firstLevel.Attack())
            {
                harbor.PlayerAttack();
            }
        }
        if (interactionPressed)
        {
            TryInteract();
            interactionPressed = false;
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
                           paused || helpVisible,
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
    worldMonitor.Tick(harbor, paused || helpVisible, dt);
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
    if (helpVisible)
    {
        if (key == 13 || key == 'h')
        {
            continueHelpPressed = true;
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
