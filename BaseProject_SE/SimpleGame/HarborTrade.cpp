#include "stdafx.h"
#include "HarborWorld.h"
#include "Profiler.h"
#include <algorithm>
#include <cmath>

namespace Harbor
{
    namespace
    {
        bool IsShopkeeper(const Npc& npc)
        {
            return npc.job == Job::Merchant || npc.job == Job::Broker;
        }
    } // namespace

    bool World::CanTradeWith(const Npc& npc) const
    {
        const Position hero = level.Player().WorldPosition(), partner = npc.WorldPosition();
        return npc.IsActive() && npc.health > 0 && npc.affinity >= -20 && npc.threatTime <= 0 &&
               std::hypot(hero.x - partner.x, hero.y - partner.y) < 1.5f && level.ClearLine(hero, partner) &&
               WorldLayout::ShareMarket(hero, partner);
    }

    void World::Trade()
    {
        Perf::Scope scope(Perf::Cpu::NpcTrade);
        if (trade.Active() || dialogue.Active())
        {
            return;
        }
        Npc* npc = Nearest();
        if (!npc || !CanTradeWith(*npc))
        {
            Event(
                L"\uac70\ub798\ud558\ub824\uba74 \uc7a5\ud130\uc5d0 \uc788\ub294 \uc8fc\ubbfc\uc5d0\uac8c \uac00\uae4c\uc774 \ub2e4\uac00\uac00\uc790. \uc801\ub300\uc801\uc774\uac70\ub098 \uc2f8\uc6b0\ub294 \uc911\uc778 \uc0c1\ub300\uc640\ub294 \uac70\ub798\ud560 \uc218 \uc5c6\ub2e4.");
            return;
        }
        trade.partner = npc->Id();
        trade.message =
            L"\ubb3c\uac74\uc744 \uace0\ub978 \ub4a4 \uc218\ub7c9\uacfc \uac00\uaca9\uc744 \ud655\uc778\ud574 \uc8fc\uc138\uc694.";
        Perf::Count(Perf::Counter::ShopOpened);
    }

    void World::EndTrade()
    {
        trade = {};
    }

    std::vector<TradeOffer> World::TradeOffers() const
    {
        Perf::Scope scope(Perf::Cpu::TradeOffers);
        const auto* npc = dynamic_cast<const Npc*>(level.Scene().Find(trade.partner));
        if (!npc || !CanTradeWith(*npc))
        {
            return {};
        }
        std::vector<TradeOffer> offers;
        auto add = [&](TradeAction action,
                       const wchar_t* name,
                       const wchar_t* description,
                       int price,
                       int stock,
                       int maximum,
                       const wchar_t* unavailable)
        {
            offers.push_back(
                {action, name, description, maximum > 0 ? L"" : unavailable, price, stock, maximum});
        };
        const auto& stats = level.stats;
        if (IsShopkeeper(*npc))
        {
            add(TradeAction::BuyPotion,
                L"\ud68c\ubcf5\uc57d \uad6c\ub9e4",
                L"\uccb4\ub825\uc744 45 \ud68c\ubcf5\ud55c\ub2e4. \uac00\ubc29\uc774\ub098 Q\ub85c \ub9c8\uc2e4 \uc218 \uc788\ub2e4.",
                8,
                npc->food,
                TransferLimit(stats.coins, npc->food, 8, stats.potions, npc->coins),
                npc->food <= 0
                    ? L"\ud68c\ubcf5\uc57d\uc774 \ub2e4 \ub5a8\uc5b4\uc84c\ub2e4."
                    : L"\ub3c8\uc774\ub098 \ubcf4\uad00\ud560 \uc5ec\uc720\uac00 \ubd80\uc871\ud558\ub2e4.");
            add(TradeAction::SellHide,
                L"\uac00\uc8fd \ud310\ub9e4",
                L"\ub0b4 \uac00\uc8fd\uc744 \ud55c \uc7a5\uc5d0 \uc5ec\uc12f \ub0e5\uc529 \ud310\ub2e4.",
                6,
                stats.hides,
                TransferLimit(npc->coins, stats.hides, 6, npc->hides, stats.coins),
                stats.hides <= 0
                    ? L"\ud314 \uac00\uc8fd\uc774 \uc5c6\ub2e4."
                    : L"\uc0c1\uc778\uc758 \ub3c8\uc774\ub098 \ubcf4\uad00\ud560 \uc5ec\uc720\uac00 \ubd80\uc871\ud558\ub2e4.");
        }
        if (npc->job == Job::Resident)
        {
            int limit = stats.health < stats.MaxHealth()
                            ? TransferLimit(stats.coins, npc->food, 4, 0, npc->coins)
                            : 0;
            add(TradeAction::BuyMeal,
                L"\ub530\ub73b\ud55c \uad6d \ud55c \uadf8\ub987",
                L"\uc5ec\uae30\uc11c \uba39\uace0 \uccb4\ub825\uc744 35 \ud68c\ubcf5\ud55c\ub2e4. \uac00\ubc29\uc5d0 \ub2f4\uae30\uc9c0 \uc54a\ub294\ub2e4.",
                4,
                npc->food,
                (std::min)(1, limit),
                stats.health >= stats.MaxHealth()
                    ? L"\uc9c0\uae08\uc740 \ubab8\uc774 \uba40\uca61\ud558\ub2e4."
                : npc->food <= 0
                    ? L"\ub0a8\uc740 \uc74c\uc2dd\uc774 \uc5c6\ub2e4."
                    : L"\ub3c8\uc774\ub098 \uac70\ub798\ud560 \uc5ec\uc720\uac00 \ubd80\uc871\ud558\ub2e4.");
        }
        if (Holder(ledger) == npc->Id() && IsShopkeeper(*npc))
        {
            add(TradeAction::BuyLedger,
                L"\uc7a5\ubd80 \ub418\uc0ac\uae30",
                L"\uc774 \uc0ac\ub78c\uc774 \uac00\uc9c4 \uc7a5\ubd80 \uc6d0\ubcf8\uc744 \uc270 \ub0e5\uc5d0 \ub418\uc0b0\ub2e4.",
                50,
                1,
                (std::min)(1, TransferLimit(stats.coins, 1, 50, 0, npc->coins)),
                L"\uc7a5\ubd80\ub97c \ub418\uc0b4 \ub3c8\uc774\ub098 \uac70\ub798\ud560 \uc5ec\uc720\uac00 \ubd80\uc871\ud558\ub2e4.");
        }
        if (Holder(ledger) == level.Player().Id() && (npc->variant == 2 || npc->variant == 3) &&
            recordChoice == RecordChoice::Undecided)
        {
            const bool authority = npc->job == Job::Guard;
            const int price = authority ? 30 : 40;
            add(TradeAction::SellLedger,
                authority ? L"\uc7a5\ubd80\ub97c \uad00\uccad\uc5d0 \ub9e1\uae30\uae30"
                          : L"\uc7a5\ubd80 \uc6d0\ubcf8 \ud310\ub9e4",
                authority
                    ? L"\uad00\uccad\uc758 \uc870\uc0ac\ub97c \ud0dd\ud55c\ub2e4. \uc6d0\ubcf8\uc744 \ub118\uae30\uba74 \ud604\uc7a5\uc774 \ud1b5\uc81c\ub418\uace0 \uc774 \uc120\ud0dd\uc774 \ud655\uc815\ub41c\ub2e4."
                    : L"\uc911\uac1c\uc0c1\uc5d0\uac8c \uc6d0\ubcf8\uc744 \ud310\ub2e4. \uc0ac\uc801 \uac70\ub798\ub85c \ub118\uc5b4\uac00\uba70 \uc774 \uc120\ud0dd\uc774 \ud655\uc815\ub41c\ub2e4.",
                price,
                1,
                copied ? (std::min)(1, TransferLimit(npc->coins, 1, price, 0, stats.coins)) : 0,
                !copied
                    ? L"\uba3c\uc800 \uc7a5\ud130 \uac8c\uc2dc\ud310\uc5d0\uc11c \uc7a5\ubd80 \uc0ac\ubcf8\uc744 \ub0a8\uae30\uc790."
                    : L"\uc0c1\ub300\uc758 \ub3c8\uc774\ub098 \uac70\ub798\ud560 \uc5ec\uc720\uac00 \ubd80\uc871\ud558\ub2e4.");
        }
        if (Holder(key) == level.Player().Id() && (npc->job == Job::Engineer || npc->job == Job::Apprentice))
        {
            add(TradeAction::GiveKey,
                L"\uc218\ubb38 \uc5f4\uc1e0 \ub9e1\uae30\uae30",
                L"\uc774 \uc0ac\ub78c\uc5d0\uac8c \uc5f4\uc1e0\ub97c \ub118\uae30\uace0 \uc218\ubb38\uc744 \uc5f4\ub3c4\ub85d \ub9e1\uae34\ub2e4.",
                0,
                1,
                1,
                L"");
        }
        Perf::Count(Perf::Counter::ShopOffersListed, offers.size());
        return offers;
    }

    bool World::ExecuteTrade(TradeAction action, int quantity)
    {
        Perf::Scope scope(Perf::Cpu::TradeCommit);
        Perf::Count(Perf::Counter::ShopAttempts);
        auto reject = [&](const std::wstring& message)
        {
            trade.message = message;
            Perf::Count(Perf::Counter::ShopRejected);
            return false;
        };
        auto* npc = dynamic_cast<Npc*>(level.Scene().Find(trade.partner));
        if (!trade.Active() || !npc || !CanTradeWith(*npc))
        {
            return reject(
                L"\uc9c0\uae08\uc740 \uc774 \uc0ac\ub78c\uacfc \uac70\ub798\ud560 \uc218 \uc5c6\ub2e4. \ucc3d\uc744 \ub2eb\uace0 \ub2e4\uc2dc \ub2e4\uac00\uac00 \ubcf4\uc790.");
        }
        const auto offers = TradeOffers();
        auto selected = std::find_if(offers.begin(),
                                     offers.end(),
                                     [&](const TradeOffer& offer)
                                     {
                                         return offer.action == action;
                                     });
        if (selected == offers.end() || quantity <= 0 || quantity > selected->maximum)
        {
            return reject(
                selected != offers.end() && !selected->unavailable.empty()
                    ? selected->unavailable
                    : L"\ub3c8\uacfc \uc7ac\uace0\ub97c \ub2e4\uc2dc \ud655\uc778\ud558\uace0 \uc218\ub7c9\uc744 \uc904\uc5ec \ubcf4\uc790.");
        }
        const int total = selected->price * quantity; // Fresh offer limits bound all integer transfers.
        auto& stats = level.stats;
        switch (action)
        {
        case TradeAction::BuyPotion:
            stats.coins -= total;
            npc->coins += total;
            npc->food -= quantity;
            stats.potions += quantity;
            break;
        case TradeAction::SellHide:
            npc->coins -= total;
            stats.coins += total;
            stats.hides -= quantity;
            npc->hides += quantity;
            break;
        case TradeAction::BuyMeal:
            stats.coins -= total;
            npc->coins += total;
            --npc->food;
            stats.health += (std::min)(35, stats.MaxHealth() - stats.health);
            break;
        case TradeAction::BuyLedger:
            stats.coins -= total;
            npc->coins += total;
            Transfer(ledger, level.Player().Id());
            break;
        case TradeAction::SellLedger:
            npc->coins -= total;
            stats.coins += total;
            npc->knowledge |= Knowledge::Ledger;
            npc->knownLedgerHolder = npc->Id();
            npc->thinkTime = 0;
            Transfer(ledger, npc->Id());
            ChooseRecord(npc->job == Job::Guard ? RecordChoice::Authority : RecordChoice::Sold);
            UpdateQuest();
            break;
        case TradeAction::GiveKey:
            npc->knowledge |= Knowledge::Key | Knowledge::Plans;
            Transfer(key, npc->Id());
            npc->thinkTime = 0;
            break;
        }
        trade.message =
            selected->name + L" " + std::to_wstring(quantity) + L"\uac1c \uac70\ub798 \uc644\ub8cc";
        if (total > 0)
        {
            trade.message += L" \u00b7 " + std::to_wstring(total) + L"\ub0e5";
        }
        if (action == TradeAction::GiveKey)
        {
            trade.message =
                L"\uc5f4\uc1e0\ub97c \ub9e1\uacbc\ub2e4. \uc218\ubb38\uc744 \uc5f4\ub7ec \uac00\ub294\uc9c0 \ud655\uc778\ud574 \ubcf4\uc790.";
        }
        if (action != TradeAction::SellLedger)
        {
            Event(trade.message);
        }
        Perf::Count(Perf::Counter::NpcTrades);
        Perf::Count(Perf::Counter::ShopUnits, quantity);
        return true;
    }
} // namespace Harbor
