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

namespace
{
    constexpr int ScreenWidth = 1280;
    constexpr int ScreenHeight = 800;
    constexpr int MapSize = Level::MapSize;
    constexpr float TileWidth = 96;
    constexpr float TileHeight = 48;
    using Vec3 = Level::Position;

    struct NpcDefinition
    {
        const wchar_t* name;
        Vec3 position;
        const wchar_t* line;
        float r, g, b;
    };

    struct Npc : public Actor
    {
        const wchar_t* name = L"";
        const wchar_t* line = L"";
    };

    enum class UiPart
    {
        Hud,
        MiniMap,
        Stats,
        Prompt,
        Pause
    };
    ActorId promptId = InvalidActorId;
    float uiOriginX = 0;
    float uiOriginY = 0;

    Renderer* renderer = nullptr;
    Level::FirstLevel firstLevel;

    Vec3 PlayerPosition()
    {
        return firstLevel.Player().WorldPosition();
    }

    bool keys[256] = {};
    bool interactionPressed = false;
    bool potionPressed = false;
    int attributePressed = 0;
    bool statsPanel = false;
    bool paused = false;
    int previousTime = 0;
    float elapsed = 0;
    HDC fontDC = nullptr;
    HFONT fontHandle = nullptr;
    std::map<wchar_t, GLuint> glyphs;
    NpcDefinition villagerDefinitions[] = {
        {L"\uc7a5\uc528",
         {12, 10, 0},
         L"\uc300\uc740 \uc544\uc9c1 \uc788\uc5b4. \uc57d\uc7ac\uac00 \ubb38\uc81c\uc9c0... \ub4e4\uc5b4\uc624\ub294 \ubc30\uac00 \uc5c6\uac70\ub4e0.",
         .62f,
         .39f,
         .24f},
        {L"\uc21c\ub355 \ud560\uba48",
         {11, 13, 0},
         L"\uc232\uae38 \uc870\uc2ec\ud574. \uc694 \uba70\uce60 \uc9d0\uc2b9\ub4e4\uc774 \ub9c8\uc744\uae4c\uc9c0 \ub0b4\ub824\uc624\ub354\ub77c.",
         .42f,
         .53f,
         .36f},
        {L"\uc218\ubb38\uc7a5",
         {14, 6, 0},
         L"\ubd81\ucabd \uae38\uc740 \ub9c9\ud614\uc18c. \ub2f5\ub2f5\ud574\ub3c4 \uc870\uae08\ub9cc \ucc38\uc544 \uc8fc\uc2dc\uc624.",
         .29f,
         .37f,
         .48f},
        {L"\ubcf5\uc774",
         {10, 11, 0},
         L"\uc6b0\ub9ac \ud615\ub3c4 \ub098\uc73c\uba74 \uac19\uc774 \ub180 \uc218 \uc788\uaca0\uc9c0?",
         .73f,
         .49f,
         .36f},
        {L"\uc8fc\ubaa8",
         {16, 9, 0},
         L"\uba3c \uae38 \uc654\uc9c0? \uad6d\uc774\ub77c\ub3c4 \ud55c \uadf8\ub987 \uba39\uace0 \uac00.",
         .56f,
         .26f,
         .24f},
        {L"\ub098\ubb34\uafbc",
         {8, 14, 0},
         L"\uc548\uac1c\ud480 \ucc3e\ub098? \uc232\uae38 \ub05d, \uc816\uc740 \ubc14\uc704 \uc606\uc744 \ubd10.",
         .37f,
         .42f,
         .29f},
        {L"\ub18d\ubd80",
         {15, 16, 0},
         L"\uc0ac\ub78c\uc774 \uc544\ud30c\ub3c4 \ubc2d\uc740 \ub3cc\ubd10\uc57c\uc9c0. \uc548 \uadf8\ub7ec\uba74 \uaca8\uc6b8\uc744 \ubabb \ub098.",
         .52f,
         .44f,
         .29f},
        {L"\ube68\ub798\ud558\ub294 \uc544\ub099",
         {17, 19, 0},
         L"\uc800 \ubc43\uc0ac\uacf5 \uc880 \ubd10 \uc918. \uc5b4\uc81c\ubd80\ud130 \ubb3c\ub3c4 \ubabb \ub118\uaca8.",
         .38f,
         .47f,
         .61f},
        {L"\ub3c4\uacf5",
         {9, 9, 0},
         L"\uae68\uc9c4 \uadf8\ub987 \uc788\uc73c\uba74 \uac00\uc838\uc640. \uc544\uc9c1 \uc190\uc740 \uba40\uca61\ud558\ub2c8\uae4c.",
         .59f,
         .44f,
         .32f},
        {L"\ud589\uc0c1",
         {13, 15, 0},
         L"\ub3c4\uc131\ub3c4 \ubcc4\uc218 \uc5c6\ub2e4\ub354\uad70. \uc5b4\ub514\ub85c \uac00\uc57c \ud560\uc9c0 \ubaa8\ub974\uaca0\uc5b4.",
         .46f,
         .32f,
         .48f},
        {L"\uc57d\ubc29 \uc2ec\ubd80\ub984\uafbc",
         {16, 12, 0},
         L"\uc11c\ub9b0 \ub204\ub098\uac00 \ubc24\uc0c8 \ubabb \uc7a4\uc5b4. \uc544\ud508 \uc0ac\ub78c\uc774 \ub108\ubb34 \ub9ce\uc544.",
         .36f,
         .54f,
         .52f},
        {L"\ub178\uc778",
         {11, 7, 0},
         L"\uc800\ub141\ubc25 \uc9d3\ub294 \uc5f0\uae30\ub294 \uc5ec\uc804\ud55c\ub370... \ub9c8\uc744\uc774 \ucc38 \uc870\uc6a9\ud574\uc84c\uc5b4.",
         .49f,
         .49f,
         .42f}};

    float Distance2D(const Vec3& a, const Vec3& b)
    {
        return std::hypot(a.x - b.x, a.y - b.y);
    }

    void WorldToScreen(const Vec3& point, float& x, float& y)
    {
        x = ScreenWidth * .5f +
            ((point.x - point.y) - (PlayerPosition().x - PlayerPosition().y)) * TileWidth * .5f;
        y = ScreenHeight * .52f +
            ((point.x + point.y) - (PlayerPosition().x + PlayerPosition().y)) * TileHeight * .5f - point.z;
    }

    bool Visible(float x, float y)
    {
        return x > -160 && x < ScreenWidth + 160 && y > -100 && y < ScreenHeight + 180;
    }

    void InitializeKoreanFont()
    {
        fontDC = wglGetCurrentDC();
        fontHandle = CreateFontW(-18,
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
        glUseProgram(0);
        glColor3f(r, g, b);
        glRasterPos2f((x + uiOriginX) / ScreenWidth * 2 - 1, 1 - (y + uiOriginY) / ScreenHeight * 2);
        for (const wchar_t* c = text; *c; ++c)
        {
            auto it = glyphs.find(*c);
            if (it == glyphs.end())
            {
                GLuint id = glGenLists(1);
                HGDIOBJ previous = SelectObject(fontDC, fontHandle);
                bool ok = id && wglUseFontBitmapsW(fontDC, *c, 1, id);
                SelectObject(fontDC, previous);
                if (!ok)
                {
                    if (id)
                    {
                        glDeleteLists(id, 1);
                    }
                    continue;
                }
                it = glyphs.insert(std::make_pair(*c, id)).first;
            }
            renderer->DrawGlyph(it->second);
        }
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
                renderer->DrawEllipse(
                    (k - 1) * 15, -52 - k * 9, 47, 35, .12f + k * .018f, .25f + k * .02f, .19f, 1);
            }
            break;
        }
    }

    void InitializeModels()
    {
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
        std::wstring cache = directory + L"/level1_models_v1.cache";

        std::vector<std::string> expected;
        for (int k = 0; k < 8; ++k)
        {
            expected.push_back(FrameName("prop", k));
        }
        for (int k = 0; k < 8; ++k)
        {
            expected.push_back(FrameName("hero", k));
        }
        for (int k = 0; k < 3; ++k)
        {
            expected.push_back(FrameName("npc", k));
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
        for (int kind = 0; kind < 3; ++kind)
        {
            renderer->BeginModel(FrameName("npc", kind));
            BuildActor(.42f + kind * .10f, .49f - kind * .06f, .39f, false, 0);
            renderer->EndModel();
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
                    renderer->DrawSolidDiamond((k - 1) * 14, -3 + k * 3, 3, 9, .25f, .36f, .21f, .6f);
                }
            }
            renderer->EndModel();
        }

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
        scene.DestroyChildren(firstLevel.NpcRoot());
        for (int i = 0; i < int(sizeof(villagerDefinitions) / sizeof(NpcDefinition)); ++i)
        {
            const auto& source = villagerDefinitions[i];
            Npc& npc = scene.Create<Npc>(firstLevel.NpcRoot(), "Villager", ActorKind::Npc);
            npc.SetWorldPosition(source.position);
            npc.name = source.name;
            npc.line = source.line;
            npc.variant = i;
            npc.drawable = true;
            npc.model = "npc" + std::to_string(i % 3);
            npc.shadowWidth = 30;
        }
        scene.DestroyChildren(firstLevel.OverlayRoot());
        for (UiPart part : {UiPart::Hud, UiPart::MiniMap, UiPart::Stats, UiPart::Pause})
        {
            Actor& ui = scene.Create(firstLevel.OverlayRoot(), "UIPanel", ActorKind::UI);
            ui.variant = int(part);
            ui.drawable = true;
            ui.pass = ScenePass::Overlay;
            ui.drawOrder = part == UiPart::Pause ? 100 : int(part);
        }
        promptId = InvalidActorId;
    }

    void UpdatePromptActor()
    {
        SceneGraph& scene = firstLevel.Scene();
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
        }
        renderer->SetMaterial(0);
    }

    void RenderWorldPass(ScenePass pass)
    {
        const SceneGraph& scene = firstLevel.Scene();
        if (pass == ScenePass::World)
        {
            renderer->DrawScene(scene,
                                pass,
                                [&](const Actor& actor)
                                {
                                    float x, y;
                                    WorldToScreen(actor.WorldPosition(), x, y);
                                    if (!Visible(x, y) || actor.shadowWidth <= 0)
                                    {
                                        return;
                                    }
                                    renderer->DrawEllipse(
                                        x + 14, y + 5, actor.shadowWidth, 22, .025f, .045f, .055f, .30f);
                                    renderer->DrawEllipse(
                                        x + 3, y + 1, actor.shadowWidth * .55f, 12, .02f, .03f, .035f, .36f);
                                });
        }
        renderer->DrawScene(scene,
                            pass,
                            [&](const Actor& actor)
                            {
                                float x, y;
                                WorldToScreen(actor.WorldPosition(), x, y);
                                if (!Visible(x, y))
                                {
                                    return;
                                }
                                if (!actor.model.empty())
                                {
                                    renderer->DrawModel(actor.model, x, y);
                                }
                                if (actor.Kind() == ActorKind::Effect)
                                {
                                    DrawEffectActor(actor, x, y);
                                }
                            });
    }

    void TryInteract()
    {
        if (firstLevel.PickUp())
        {
            return;
        }
        const Npc* closest = nullptr;
        float distance = 1.4f;
        for (const Npc* npc : firstLevel.Scene().Actors<Npc>(ActorKind::Npc))
        {
            float d = Distance2D(PlayerPosition(), npc->WorldPosition());
            if (npc->IsVisible() && d < distance &&
                firstLevel.ClearLine(PlayerPosition(), npc->WorldPosition()))
            {
                closest = npc;
                distance = d;
            }
        }
        if (!closest)
        {
            return;
        }
        int nearest = closest->variant;
        if (nearest == 4)
        {
            firstLevel.stats.health = firstLevel.stats.MaxHealth();
            Say(L"\uc8fc\ubaa8",
                L"\uc7a0\uae50 \uc549\uc544 \uc26c\uc5b4. \ub2e4\uce5c \ub370\ubd80\ud130 \ucd94\uc2ac\ub7ec\uc57c\uc9c0. \uae30\uc6b4\uc740 \uc880 \ub3cc\uc544\uc654\ub098?");
        }
        else if (nearest == 2)
        {
            Say(L"\uc218\ubb38\uc7a5",
                L"\ub9c8\uc744 \ubc16 \uc9d0\uc2b9\ub4e4\uc774 \uc0ac\ub098\uc6cc\uc84c\uc18c. SPACE\ub85c \uacf5\uaca9\ud558\uace0 \uc804\ub9ac\ud488\uc740 E\ub85c \ucc59\uae30\uc2dc\uc624.");
        }
        else
        {
            Say(closest->name, closest->line);
        }
    }

    void RenderMiniMap()
    {
        constexpr float cx = 1150, cy = 126, scale = 2.0f;
        renderer->DrawSolidRect(cx, cy, 240, 225, .04f, .07f, .075f, .94f);
        for (const Actor* tile : firstLevel.Scene().RenderList(ScenePass::Ground))
        {
            Vec3 p = tile->WorldPosition();
            bool w = tile->variant == 5;
            bool road = tile->variant == 4;
            renderer->DrawSolidDiamond(cx + (p.x - p.y) * scale,
                                       cy + (p.x + p.y - MapSize) * scale * .5f,
                                       scale * 2 + 1,
                                       scale + 1,
                                       w      ? .09f
                                       : road ? .55f
                                              : .22f,
                                       w      ? .28f
                                       : road ? .46f
                                              : .34f,
                                       w ? .38f : .21f,
                                       1);
        }
        auto marker = [&](const Vec3& p, float r, float g, float b, float size)
        {
            renderer->DrawEllipse(
                cx + (p.x - p.y) * scale, cy + (p.x + p.y - MapSize) * scale * .5f, size, size, r, g, b, 1);
        };
        for (const Actor* prop : firstLevel.Props())
        {
            if (prop->IsVisible() && prop->variant <= 1)
            {
                marker(prop->WorldPosition(), .62f, .49f, .35f, 4);
            }
        }
        for (const Npc* npc : firstLevel.Scene().Actors<Npc>(ActorKind::Npc))
        {
            if (npc->IsVisible())
            {
                marker(npc->WorldPosition(), .44f, .75f, .6f, 3);
            }
        }
        for (const auto& enemy : firstLevel.Enemies())
        {
            if (enemy->IsVisible() && enemy->health > 0)
            {
                marker(enemy->WorldPosition(), .95f, .30f, .16f, 4);
            }
        }
        for (const auto& loot : firstLevel.Drops())
        {
            if (loot->IsVisible())
            {
                marker(loot->WorldPosition(), .95f, .77f, .25f, 3);
            }
        }
        if (firstLevel.Player().IsActive() && firstLevel.Player().IsVisible())
        {
            marker(PlayerPosition(), .96f, .97f, 1, 6);
        }
        DrawText(cx - 101, cy - 92, L"\uc548\uac1c\ud638 \uc0ac\ub0e5\ud130", .88f, .77f, .53f);
        DrawText(
            cx - 104, cy + 96, L"\ud770\uc0c9 \ub098 / \ubd89\uc740\uc0c9 \uc9d0\uc2b9", .8f, .82f, .75f);
    }

    void DrawWrapped(float x, float y, const std::wstring& text, int limit)
    {
        // Wide Korean glyphs count as two columns. Explicit line breaks remain supported.
        std::wstring line;
        int columns = 0;
        for (wchar_t ch : text)
        {
            int advance = ch < 128 ? 1 : 2;
            if (ch == L'\n' || columns + advance > limit)
            {
                DrawText(x, y, line.c_str());
                y += 25;
                line.clear();
                columns = 0;
                if (ch == L'\n')
                {
                    continue;
                }
            }
            line += ch;
            columns += advance;
        }
        if (!line.empty())
        {
            DrawText(x, y, line.c_str());
        }
    }

    void DrawHud()
    {
        const auto& stats = firstLevel.stats;
        renderer->SetMaterial(0);
        renderer->DrawSolidRect(430, 62, 820, 104, .035f, .055f, .06f, .92f);
        DrawText(
            35, 38, L"\uccab \ub808\ubca8 \u00b7 \uc548\uac1c\ud638 \uc0ac\ub0e5\ud130", .92f, .76f, .48f);
        std::wstring status = L"\ub808\ubca8 " + std::to_wstring(stats.level) + L"   \uccb4\ub825 " +
                              std::to_wstring(stats.health) + L" / " + std::to_wstring(stats.MaxHealth()) +
                              L"   \uacf5\uaca9 " + std::to_wstring(stats.Attack()) + L"   \ubc29\uc5b4 " +
                              std::to_wstring(stats.Defense());
        DrawText(35, 65, status.c_str());
        float hp = 260.0f * stats.health / stats.MaxHealth();
        renderer->DrawSolidRect(165, 81, 260, 7, .18f, .13f, .12f, 1);
        if (hp > 0)
        {
            renderer->DrawSolidRect(35 + hp * .5f, 81, hp, 7, .66f, .28f, .21f, 1);
        }
        std::wstring xp = stats.level == Level::MaximumLevel
                              ? L"\ucd5c\uace0 \ub808\ubca8"
                              : L"\uacbd\ud5d8\uce58 " + std::to_wstring(stats.experience) + L" / " +
                                    std::to_wstring(stats.RequiredExperience());
        DrawText(325, 91, xp.c_str());
        float xpWidth =
            stats.level == Level::MaximumLevel ? 260 : 260.0f * stats.experience / stats.RequiredExperience();
        renderer->DrawSolidRect(690, 81, 260, 7, .16f, .18f, .17f, 1);
        if (xpWidth > 0)
        {
            renderer->DrawSolidRect(560 + xpWidth * .5f, 81, xpWidth, 7, .45f, .66f, .47f, 1);
        }

        std::wstring goal =
            stats.level >= 5
                ? L"\uae30\ubcf8 \uc218\ub828 \uc644\ub8cc! \uacc4\uc18d \uc0ac\ub0e5\ud558\uba70 \uc131\uc7a5\ud560 \uc218 \uc788\ub2e4."
                : L"\uc9d0\uc2b9\uc744 \uc0ac\ub0e5\ud558\uba70 \ub808\ubca8 5\uae4c\uc9c0 \uc131\uc7a5\ud574 \ubcf4\uc790.";
        DrawText(35, 140, goal.c_str(), .9f, .81f, .56f);
        std::wstring inventory = L"\ud68c\ubcf5\uc57d " + std::to_wstring(stats.potions) +
                                 L"   \uc5fd\uc804 " + std::to_wstring(stats.coins) + L"   \uac00\uc8fd " +
                                 std::to_wstring(stats.hides) + L"   \ucc98\uce58 " +
                                 std::to_wstring(stats.kills);
        DrawText(35, 744, inventory.c_str());
        DrawText(
            28,
            777,
            L"WASD \uc774\ub3d9   SPACE \uacf5\uaca9   E \uc90d\uae30/\ub300\ud654   Q \ud68c\ubcf5\uc57d   TAB \ub2a5\ub825\uce58   ESC \uc26c\uae30",
            .8f,
            .81f,
            .73f);
        if (firstLevel.messageTime > 0)
        {
            renderer->DrawSolidRect(510, 687, 980, 78, .035f, .05f, .055f, .95f);
            DrawWrapped(36, 680, firstLevel.message, 98);
        }
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
        else if (stats.points > 0)
        {
            DrawText(35,
                     171,
                     L"\ub2a5\ub825\uce58 \uc810\uc218\uac00 \uc0dd\uacbc\ub2e4! TAB \ub610\ub294 1 / 2 / 3",
                     .97f,
                     .79f,
                     .44f);
        }
    }

    void DrawPausePanel()
    {
        if (paused)
        {
            renderer->DrawSolidRect(640, 400, 390, 90, .03f, .05f, .06f, .96f);
            DrawText(498, 405, L"\uc7a0\uc2dc \uc26c\ub294 \uc911 - ESC\ub85c \ub3cc\uc544\uac00\uae30");
        }
    }

    void RenderOverlay()
    {
        renderer->DrawScene(firstLevel.Scene(),
                            ScenePass::Overlay,
                            [&](const Actor& actor)
                            {
                                UiPart part = UiPart(actor.variant);
                                if (part == UiPart::Prompt)
                                {
                                    const Actor* owner = firstLevel.Scene().Find(actor.ParentId());
                                    if (!owner)
                                    {
                                        return;
                                    }
                                    float x, y;
                                    WorldToScreen(actor.WorldPosition(), x, y);
                                    renderer->DrawSolidRect(x, y, 170, 30, .04f, .06f, .07f, .92f);
                                    DrawText(x - 76,
                                             y + 6,
                                             owner->Kind() == ActorKind::Loot
                                                 ? L"E \uc804\ub9ac\ud488 \uc90d\uae30"
                                                 : L"E \ub9d0 \uac78\uae30",
                                             1,
                                             .84f,
                                             .43f);
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
                                case UiPart::MiniMap:
                                    RenderMiniMap();
                                    break;
                                case UiPart::Stats:
                                    DrawStatsPanel();
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
    }

    void Update(float dt)
    {
        if (paused)
        {
            interactionPressed = false;
            potionPressed = false;
            attributePressed = 0;
            UpdatePromptActor();
            return;
        }
        elapsed += dt;
        float sx = (keys['d'] ? 1.0f : 0) - (keys['a'] ? 1.0f : 0);
        float sy = (keys['s'] ? 1.0f : 0) - (keys['w'] ? 1.0f : 0);
        firstLevel.MovePlayer(sx, sy, dt);
        firstLevel.Update(dt);
        if (keys[' '])
        {
            firstLevel.Attack();
        }
        if (interactionPressed)
        {
            TryInteract();
            interactionPressed = false;
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
    renderer->SetTime(elapsed);
    renderer->BeginFrame(.075f, .12f, .14f, 1);
    RenderWorldPass(ScenePass::Ground);
    RenderWorldPass(ScenePass::World);
    RenderWorldPass(ScenePass::Effects);
    renderer->EndWorld(elapsed);
    RenderOverlay();
    glutSwapBuffers();
    renderer->EndFrame();
}

void Idle()
{
    int now = glutGet(GLUT_ELAPSED_TIME);
    float dt = previousTime ? float(now - previousTime) / 1000 : 0;
    previousTime = now;
    Update((std::min)(dt, .05f));
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
    if (key == 27)
    {
        paused = !paused;
    }
    else if (key == '\t')
    {
        statsPanel = !statsPanel;
    }
    else if (!paused)
    {
        if (key == 'e')
        {
            interactionPressed = true;
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
    if (renderer)
    {
        renderer->SetOutputSize(w > 0 ? w : 1, h > 0 ? h : 1);
    }
}

void Close()
{
    for (const auto& glyph : glyphs)
    {
        glDeleteLists(glyph.second, 1);
    }
    glyphs.clear();
    delete renderer;
    renderer = nullptr;
}

int main(int argc, char** argv)
{
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
    glutInitWindowSize(ScreenWidth, ScreenHeight);
    glutCreateWindow("Mist Lake - Level 1");
    SetWindowTextW(GetActiveWindow(), L"\uc548\uac1c\ud638 \u00b7 \uccab \ubc88\uc9f8 \uc0ac\ub0e5\ud130");
    if (glewInit() != GLEW_OK || !glewIsSupported("GL_VERSION_3_3"))
    {
        return 1;
    }
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    renderer = new Renderer(ScreenWidth, ScreenHeight);
    if (!renderer->IsInitialized())
    {
        delete renderer;
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
