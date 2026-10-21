#pragma once

#include "Actor.h"
#include <cmath>

// Shared world-space policy: rendering, travel, combat and AI use the same regions.
namespace WorldLayout
{
    constexpr int MapSize = 384;
    constexpr int ChunkSize = 32;
    constexpr int ChunksPerSide = MapSize / ChunkSize;
    constexpr int TownOffsetX = 40;
    constexpr int TownOffsetY = 56;
    constexpr int NpcAppearanceCount = 18;
    constexpr int VillageCount = 5;
    constexpr int NpcsPerVillage = 20;
    constexpr int MonsterCount = 100;
    constexpr float VillageSpacing = 112;
    constexpr float VillageRadius = 18;
    constexpr float RespawnDelaySeconds = 2;
    constexpr float CharacterVisualHeight = 88;
    constexpr float MarketRadius = 18.0f;
    constexpr float RaiderTerritoryRadius = 26.0f;

    inline ActorPosition Town(float x, float y)
    {
        return {x + TownOffsetX, y + TownOffsetY, 0};
    }

    inline ActorPosition VillageSpawn()
    {
        return Town(13.5f, 12.5f);
    }

    inline ActorPosition HunterCamp()
    {
        return {173.5f, 76.5f, 0};
    }

    inline ActorPosition Waystation()
    {
        return {285.5f, 76.5f, 0};
    }

    inline ActorPosition OldShrine()
    {
        return {285.5f, 188.5f, 0};
    }

    inline ActorPosition RaiderCamp()
    {
        return {341.5f, 300.5f, 0};
    }

    inline ActorPosition VillageCenter(int index)
    {
        switch (index)
        {
        case 1:
            return HunterCamp();
        case 2:
            return Waystation();
        case 3:
            return OldShrine();
        case 4:
            return {173.5f, 188.5f, 0};
        default:
            return Town(21.5f, 20.5f);
        }
    }

    inline const wchar_t* VillageName(int index)
    {
        static const wchar_t* names[] = {L"\ubb34\uc5f0\ud56d",
                                         L"\uc194\ubc14\ub78c\ub9c8\uc744",
                                         L"\uae34\uae38\ub9c8\uc744",
                                         L"\uc740\uc194\ub9c8\uc744",
                                         L"\ub4e4\uc0d8\ub9c8\uc744"};
        return names[index >= 0 && index < VillageCount ? index : 0];
    }

    inline int VillageAt(const ActorPosition& p)
    {
        if (p.x >= TownOffsetX + 4 && p.x <= TownOffsetX + 38 && p.y >= TownOffsetY + 2 &&
            p.y <= TownOffsetY + 38)
        {
            return 0;
        }
        for (int i = 1; i < VillageCount; ++i)
        {
            ActorPosition c = VillageCenter(i);
            if (std::abs(p.x - c.x) <= VillageRadius && std::abs(p.y - c.y) <= VillageRadius)
            {
                return i;
            }
        }
        return -1;
    }

    inline int VillageNeighbour(int village)
    {
        // Every hop has a direct, axis-aligned road of 112 tiles (~20 seconds at 300 px/s).
        const int next[] = {1, 2, 3, 4, 1};
        return next[village];
    }

    inline ActorPosition VillageMarket(int village)
    {
        return village == 0 ? VillageSpawn() : VillageCenter(village);
    }

    inline bool BetweenVillages(const ActorPosition& p)
    {
        if (VillageAt(p) >= 0)
        {
            return false;
        }
        for (int i = 0; i < VillageCount; ++i)
        {
            ActorPosition a = VillageCenter(i), b = VillageCenter(VillageNeighbour(i));
            if (std::abs(a.y - b.y) < 1 && std::abs(p.y - a.y) <= 26 && p.x >= (a.x < b.x ? a.x : b.x) &&
                p.x <= (a.x > b.x ? a.x : b.x))
            {
                return true;
            }
            if (std::abs(a.x - b.x) < 1 && std::abs(p.x - a.x) <= 26 && p.y >= (a.y < b.y ? a.y : b.y) &&
                p.y <= (a.y > b.y ? a.y : b.y))
            {
                return true;
            }
        }
        return false;
    }

    inline bool InVillage(const ActorPosition& p)
    {
        return VillageAt(p) >= 0;
    }

    inline bool InStoryDistrict(const ActorPosition& p)
    {
        return p.x >= TownOffsetX && p.x <= TownOffsetX + 40 && p.y >= TownOffsetY && p.y <= TownOffsetY + 40;
    }
} // namespace WorldLayout
