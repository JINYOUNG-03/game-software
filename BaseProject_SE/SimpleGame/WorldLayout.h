#pragma once

#include "Actor.h"

// Shared world-space policy: rendering, travel, combat and AI use the same regions.
namespace WorldLayout
{
    constexpr int MapSize = 256;
    constexpr int ChunkSize = 32;
    constexpr int ChunksPerSide = MapSize / ChunkSize;
    constexpr int TownOffsetX = 40;
    constexpr int TownOffsetY = 56;
    constexpr int NpcAppearanceCount = 18;
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
        return {102.5f, 112.5f, 0};
    }

    inline ActorPosition Waystation()
    {
        return {146.5f, 78.5f, 0};
    }

    inline ActorPosition OldShrine()
    {
        return {160.5f, 166.5f, 0};
    }

    inline ActorPosition RaiderCamp()
    {
        return {210.5f, 204.5f, 0};
    }

    inline bool InVillage(const ActorPosition& p)
    {
        return p.x >= TownOffsetX + 4 && p.x <= TownOffsetX + 38 && p.y >= TownOffsetY + 2 &&
               p.y <= TownOffsetY + 38;
    }

    inline bool InStoryDistrict(const ActorPosition& p)
    {
        return p.x >= TownOffsetX && p.x <= TownOffsetX + 40 && p.y >= TownOffsetY && p.y <= TownOffsetY + 40;
    }
} // namespace WorldLayout
