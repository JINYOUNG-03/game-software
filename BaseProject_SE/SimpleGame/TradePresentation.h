#pragma once

#include <algorithm>

namespace Shop
{
    constexpr float RowsX = 200;
    constexpr float RowsY = 245;
    constexpr float RowWidth = 480;
    constexpr float RowHeight = 64;
    constexpr float RowStride = 76;
    constexpr int QuantityLimit = 99;

    struct Button
    {
        float x, y, width, height;

        bool Contains(float px, float py) const
        {
            return px >= x && px <= x + width && py >= y && py <= y + height;
        }
    };

    constexpr Button Close{1060, 135, 40, 35};
    constexpr Button Less{745, 495, 50, 34};
    constexpr Button More{895, 495, 50, 34};
    constexpr Button Maximum{975, 495, 100, 34};
    constexpr Button Confirm{735, 585, 340, 40};

    inline int RowAt(float x, float y, int count)
    {
        if (x < RowsX || x > RowsX + RowWidth || y < RowsY)
        {
            return -1;
        }
        const int row = int((y - RowsY) / RowStride);
        return row < count && y - RowsY - row * RowStride <= RowHeight ? row : -1;
    }

    inline int Quantity(int requested, int maximum)
    {
        return (std::max)(1, (std::min)(requested, (std::min)(maximum, QuantityLimit)));
    }
} // namespace Shop
