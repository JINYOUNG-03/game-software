#pragma once

#include <cmath>
#include "WorldLayout.h"

// Shared by cached poses, gameplay impact timing and the existing SwordTrail Actor.
namespace CharacterPresentation
{
    constexpr int AttackFrames = 12;
    constexpr float AttackDuration = .38f;
    constexpr float AttackImpactTime = AttackDuration * .43f;

    inline float NpcVisualHeight(int identity)
    {
        // Includes hats, plumes and carried gear; keep nameplates near each actual silhouette.
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

    inline float Ease(float value)
    {
        float t = Clamp01(value);
        return t * t * (3 - 2 * t);
    }

    inline float Mix(float from, float to, float t)
    {
        return from + (to - from) * t;
    }

    inline int AttackFrame(float remaining)
    {
        int frame = int(Clamp01(1 - remaining / AttackDuration) * AttackFrames);
        return frame < AttackFrames ? frame : AttackFrames - 1;
    }

    inline int AttackDirection(float facingY)
    {
        return facingY < -.6f ? -1 : facingY > .6f ? 1 : 0;
    }

    struct SwordPose
    {
        float handX = 14;
        float handY = -22;
        float angle = -.6f;
        float bodyDip = 0;
        float stance = 0;
        float trailStrength = 0;
    };

    inline SwordPose SampleSwordPose(int frame, int vertical)
    {
        float progress = Clamp01(float(frame) / float(AttackFrames - 1));
        SwordPose pose;
        if (progress < .26f)
        {
            float t = Ease(progress / .26f);
            pose.handX = Mix(14, 5, t);
            pose.handY = Mix(-22, -49, t);
            pose.angle = Mix(-.6f, -2.2f, t);
            pose.bodyDip = -2 * t;
            pose.stance = -t;
        }
        else if (progress < .60f)
        {
            float t = Ease((progress - .26f) / .34f);
            pose.handX = Mix(5, 27, t);
            pose.handY = Mix(-49, -22, t);
            pose.angle = Mix(-2.2f, .85f, t);
            pose.bodyDip = Mix(-2, 3, t);
            pose.stance = Mix(-1, 3, t);
            pose.trailStrength = std::sin(Clamp01((progress - .26f) / .34f) * 3.1415927f);
        }
        else
        {
            float t = Ease((progress - .60f) / .40f);
            pose.handX = Mix(27, 14, t);
            pose.angle = Mix(.85f, -.6f, t);
            pose.bodyDip = 3 * (1 - t);
            pose.stance = 3 * (1 - t);
        }
        pose.angle += vertical < 0 ? -.95f : vertical > 0 ? .85f : 0;
        pose.handY += float(vertical) * 5;
        return pose;
    }
} // namespace CharacterPresentation
