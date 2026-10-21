#pragma once

#include <cmath>
#include "WorldLayout.h"

// Shared by cached poses, gameplay impact timing and the existing SwordTrail Actor.
namespace CharacterPresentation
{
    constexpr float HumanWidthScale = 1.40f;
    constexpr int AttackFrames = 12;
    constexpr float AttackDuration = .38f;
    constexpr float AttackImpactTime = AttackDuration * .55f;

    inline int ScreenDirection(float x, float y)
    {
        return std::abs(x) > std::abs(y) ? (x >= 0 ? 0 : 2) : (y >= 0 ? 1 : 3);
    }

    inline float AttackTrailStrength(float remaining)
    {
        float progress = 1 - remaining / AttackDuration;
        return progress > .45f && progress < .70f ? std::sin((progress - .45f) / .25f * 3.1415927f) : 0;
    }

    inline float NpcVisualHeight(int identity)
    {
        // Domain-only fallback; the presentation layer replaces this with the loaded sprite bounds.
        static const float heights[] = {
            72, 96, 104, 82, 64, 82, 70, 78, 82, 74, 74, 82, 80, 72, 78, 82, 82, 80};
        static_assert(sizeof(heights) / sizeof(*heights) == WorldLayout::NpcAppearanceCount,
                      "Every NPC appearance requires a visual height.");
        return identity >= 0 && identity < WorldLayout::NpcAppearanceCount ? heights[identity] : 104.0f;
    }

    inline float Clamp01(float value)
    {
        return value < 0 ? 0 : value > 1 ? 1 : value;
    }

    inline int AttackFrame(float remaining)
    {
        int frame = int(Clamp01(1 - remaining / AttackDuration) * AttackFrames);
        return frame < AttackFrames ? frame : AttackFrames - 1;
    }

} // namespace CharacterPresentation
