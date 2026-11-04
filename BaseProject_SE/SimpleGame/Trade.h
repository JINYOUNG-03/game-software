#pragma once

#include "Actor.h"
#include <algorithm>
#include <limits>
#include <string>

namespace Harbor
{
    enum class TradeAction
    {
        BuyPotion,
        SellHide,
        BuyMeal,
        BuyLedger,
        SellLedger,
        GiveKey
    };

    struct TradeOffer
    {
        TradeAction action = TradeAction::BuyPotion;
        std::wstring name;
        std::wstring description;
        std::wstring unavailable;
        int price = 0;
        int stock = 0;
        int maximum = 0;
    };

    struct TradeSession
    {
        ActorId partner = InvalidActorId;
        std::wstring message;

        bool Active() const
        {
            return partner != InvalidActorId;
        }
    };

    inline bool IsSale(TradeAction action)
    {
        return action == TradeAction::SellHide || action == TradeAction::SellLedger;
    }

    inline bool NeedsTradeConfirmation(TradeAction action)
    {
        return action == TradeAction::SellLedger || action == TradeAction::GiveKey;
    }

    inline int TransferLimit(int wallet, int stock, int price, int owned, int recipientWallet)
    {
        if (wallet < 0 || stock <= 0 || price <= 0 || owned < 0 || recipientWallet < 0)
        {
            return 0;
        }
        constexpr int maximum = (std::numeric_limits<int>::max)();
        return (std::min)({stock, wallet / price, maximum - owned, (maximum - recipientWallet) / price});
    }
} // namespace Harbor
