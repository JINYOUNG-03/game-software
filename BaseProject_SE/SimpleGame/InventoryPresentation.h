#pragma once

#include <array>
#include "LevelOne.h"

namespace Inventory
{
    constexpr int Columns = 6;
    constexpr int Rows = 4;
    constexpr int SlotCount = Columns * Rows;
    constexpr float GridX = 490;
    constexpr float GridY = 215;
    constexpr float SlotSize = 80;
    constexpr float SlotStride = 90;
    constexpr float AttributeLeft = 188;
    constexpr float AttributeTop = 607;
    constexpr float AttributeWidth = 224;
    constexpr float AttributeHeight = 22;
    constexpr float AttributeStride = 25;
    constexpr int AttributeCount = 3;

    inline float AttributeRowTop(int attribute)
    {
        return AttributeTop + (attribute - 1) * AttributeStride;
    }

    inline int HitAttribute(float x, float y)
    {
        if (x < AttributeLeft || x >= AttributeLeft + AttributeWidth)
        {
            return 0;
        }
        for (int attribute = 1; attribute <= AttributeCount; ++attribute)
        {
            const float top = AttributeRowTop(attribute);
            if (y >= top && y < top + AttributeHeight)
            {
                return attribute;
            }
        }
        return 0;
    }

    enum class Item
    {
        Empty,
        Potion,
        Coins,
        Hide,
        Ledger,
        Key,
        LedgerCopy
    };

    struct Stack
    {
        Item item = Item::Empty;
        int quantity = 0;
    };

    // A read-only view of existing ownership, not another inventory or an item-transfer system.
    inline std::array<Stack, SlotCount> Snapshot(const Level::Stats& stats, bool ledger, bool key, bool copy)
    {
        std::array<Stack, SlotCount> slots{};
        size_t next = 0;
        auto append = [&](Item item, int quantity)
        {
            if (quantity > 0)
            {
                slots[next++] = {item, quantity};
            }
        };
        append(Item::Potion, stats.potions);
        append(Item::Coins, stats.coins);
        append(Item::Hide, stats.hides);
        append(Item::Ledger, ledger ? 1 : 0);
        append(Item::Key, key ? 1 : 0);
        append(Item::LedgerCopy, copy ? 1 : 0);
        return slots;
    }

    inline int HitSlot(float x, float y)
    {
        if (x < GridX || y < GridY)
        {
            return -1;
        }
        const int column = int((x - GridX) / SlotStride);
        const int row = int((y - GridY) / SlotStride);
        if (column >= Columns || row >= Rows || x - GridX - column * SlotStride >= SlotSize ||
            y - GridY - row * SlotStride >= SlotSize)
        {
            return -1;
        }
        return row * Columns + column;
    }

    inline int MoveSelection(int selected, int delta)
    {
        int row = selected / Columns, column = selected % Columns;
        if (delta == Columns || delta == -Columns)
        {
            row += delta / Columns;
            row = row < 0 ? 0 : row >= Rows ? Rows - 1 : row;
        }
        else
        {
            column += delta;
            column = column < 0 ? 0 : column >= Columns ? Columns - 1 : column;
        }
        return row * Columns + column;
    }
} // namespace Inventory
