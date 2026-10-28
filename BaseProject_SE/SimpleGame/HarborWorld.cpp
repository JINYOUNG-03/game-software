#include "stdafx.h"
#include "HarborWorld.h"
#include "Profiler.h"
#include "CharacterPresentation.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <map>

namespace Harbor
{
    namespace
    {
        float Distance(const Level::Position& a, const Level::Position& b)
        {
            return std::hypot(a.x - b.x, a.y - b.y);
        }

        bool IsMerchant(const Npc& npc)
        {
            return npc.job == Job::Merchant || npc.job == Job::Broker;
        }

        int Bounded(int value, int minimum, int maximum)
        {
            return (std::max)(minimum, (std::min)(maximum, value));
        }

        std::string JsonWide(const std::wstring& text)
        {
            // Keep source/log encoding independent of the Windows code page.
            const char hex[] = "0123456789abcdef";
            std::string out = "\"";
            for (wchar_t c : text)
            {
                unsigned code = static_cast<unsigned short>(c);
                if (c == L'"' || c == L'\\')
                {
                    out += '\\';
                    out += char(c);
                }
                else if (code < 32 || code > 126)
                {
                    out += "\\u";
                    for (int shift = 12; shift >= 0; shift -= 4)
                    {
                        out += hex[(code >> shift) & 15];
                    }
                }
                else
                {
                    out += char(c);
                }
            }
            return out + "\"";
        }

        struct Settlement
        {
            const char* id;
            const wchar_t* name;
            const char* kind;
            Level::Position center;
            float radius;
        };

        const std::array<Settlement, WorldLayout::VillageCount>& Settlements()
        {
            static const std::array<Settlement, WorldLayout::VillageCount> places = {
                {{"muyeon", WorldLayout::VillageName(0), "village", WorldLayout::VillageCenter(0), 18},
                 {"solbaram", WorldLayout::VillageName(1), "village", WorldLayout::VillageCenter(1), 18},
                 {"gingil", WorldLayout::VillageName(2), "village", WorldLayout::VillageCenter(2), 18},
                 {"eunsol", WorldLayout::VillageName(3), "village", WorldLayout::VillageCenter(3), 18},
                 {"deulsaem", WorldLayout::VillageName(4), "village", WorldLayout::VillageCenter(4), 18}}};
            return places;
        }

        const char* MoodKeys[] = {
            "conflict", "wounded", "flooded", "controlled", "restless", "busy", "quiet", "uninhabited"};

        int SettlementAt(const Level::Position& position)
        {
            return WorldLayout::VillageAt(position);
        }

        const char* SettlementId(int index)
        {
            return index < 0 ? "wilderness" : Settlements()[size_t(index)].id;
        }

        const char* JobKey(Job job)
        {
            const char* names[] = {"engineer",
                                   "clerk",
                                   "guard",
                                   "broker",
                                   "apprentice",
                                   "hunter",
                                   "raider",
                                   "merchant",
                                   "scholar",
                                   "resident"};
            return names[int(job)];
        }

        const char* ActionKey(Action action)
        {
            const char* names[] = {"rest",
                                   "hunt",
                                   "raid",
                                   "trade",
                                   "study",
                                   "investigate",
                                   "return",
                                   "patrol",
                                   "dead",
                                   "defend",
                                   "travel"};
            return names[int(action)];
        }
    } // namespace

    const wchar_t* World::JobLabel(Job job)
    {
        const wchar_t* names[] = {L"\uc218\ub85c \uae30\uc220\uc790",
                                  L"\uc870\ud569 \uae30\ub85d\uc6d0",
                                  L"\uc870\uc0ac\uad00",
                                  L"\uc911\uac1c\uc0c1",
                                  L"\uc218\uc2b5 \uc218\ub9ac\uacf5",
                                  L"\uc0ac\ub0e5\uafbc",
                                  L"\uc57d\ud0c8\uc790",
                                  L"\uc0c1\uc778",
                                  L"\ud559\uc790",
                                  L"\uc8fc\ubbfc"};
        return names[int(job)];
    }

    const wchar_t* World::ActionLabel(Action action)
    {
        const wchar_t* names[] = {L"\ubab8 \ucd94\uc2a4\ub974\uae30",
                                  L"\uc0ac\ub0e5",
                                  L"\uc57d\ud0c8",
                                  L"\uac70\ub798",
                                  L"\uae68\ub2ec\uc74c \ucc3e\uae30",
                                  L"\uc870\uc0ac",
                                  L"\uadc0\ud658",
                                  L"\uc21c\ucc30",
                                  L"\uc0ac\ub9dd",
                                  L"\ub9de\uc11c\uae30",
                                  L"\ub9c8\uc744 \uac04 \uc774\ub3d9"};
        return names[int(action)];
    }

    World::Position World::Reachable(const Position& requested) const
    {
        for (int ring = 0; ring < Level::MapSize; ++ring)
        {
            for (int y = -ring; y <= ring; ++y)
            {
                for (int x = -ring; x <= ring; ++x)
                {
                    if (std::abs(x) != ring && std::abs(y) != ring)
                    {
                        continue;
                    }
                    Position p = {
                        std::floor(requested.x) + x + .5f, std::floor(requested.y) + y + .5f, requested.z};
                    if (level.Walkable(p.x, p.y))
                    {
                        return p;
                    }
                }
            }
        }
        return level.Player().WorldPosition();
    }

    World::Position World::Site(int index) const
    {
        return level.Scene().Get(sites[index]).WorldPosition();
    }

    ActorId World::Holder(ActorId artifact) const
    {
        const Actor* node = level.Scene().Find(artifact);
        return node && node->ParentId() != worldRoot ? node->ParentId() : InvalidActorId;
    }

    void World::Transfer(ActorId artifact, ActorId holder)
    {
        Actor& node = level.Scene().Get(artifact);
        level.Scene().Reparent(artifact, holder ? holder : worldRoot, true);
        if (holder)
        {
            node.SetLocalPosition({0, 0, artifact == ledger ? 72.0f : 88.0f});
        }
    }

    void World::Event(const std::wstring& text, bool notifyPlayer)
    {
        history.push_front(Clock() + L"  " + text);
        if (history.size() > 6)
        {
            history.pop_back();
        }
        if (notifyPlayer)
        {
            level.message = text;
            level.messageTime = 6;
        }
        Perf::Count(Perf::Counter::StoryEvents);
    }

    Npc* World::Person(int definitionId) const
    {
        for (Npc* npc : level.Scene().Actors<Npc>(ActorKind::Npc))
        {
            if (npc->variant == definitionId)
            {
                return npc;
            }
        }
        return nullptr;
    }

    void World::ReactToPlayer(const std::wstring& result)
    {
        consequence = result;
        logTime = 0;
        for (Npc* npc : level.Scene().Actors<Npc>(ActorKind::Npc))
        {
            if (npc->homeVillage != 0 || npc->courier)
            {
                continue;
            }
            if (npc->variant >= 5 && (Distance(npc->WorldPosition(), level.Player().WorldPosition()) > 6 ||
                                      !level.ClearLine(npc->WorldPosition(), level.Player().WorldPosition())))
            {
                continue;
            }
            npc->thinkTime = 0;
            npc->path.clear();
            npc->waypoint = 0;
            if (npc->variant >= 9 && npc->job != Job::Hunter)
            {
                npc->line =
                    recordChoice == RecordChoice::Public
                        ? (npc->faction == 0
                               ? L"\uc7a5\ubd80\uac00 \uacf5\uac1c\ub418\uace0 \uc7a5\ud130\uac00 \uc220\ub801\uc5ec. \uc9c4\uc2e4\ub3c4 \uc911\uc694\ud558\uc9c0\ub9cc \ud63c\ub780\uc774 \uac71\uc815\uc774\uc57c."
                               : L"\ub2f9\uc2e0\uc774 \uaebc\ub0b4 \uc900 \uc774\ub984\uc744 \ub4e4\uc73c\ub7ec \uc7a5\ud130\ub85c \uac00\uace0 \uc788\uc5b4. \uac00\uc871\ub4e4 \uc774\uc57c\uae30\ub97c \ub4e4\uc5b4\uc57c\uc9c0.")
                    : recordChoice == RecordChoice::Authority
                        ? L"\uad00\uccad\uc5d0\uc11c \ud604\uc7a5\uc744 \ub9c9\ub294\ub2e4\ub2c8 \uc9d1\uc73c\ub85c \uac00\uc57c\uaca0\uc5b4. \uc6b0\ub9ac\ub3c4 \uadf8 \uae30\ub85d\uc744 \ubcfc \uc218 \uc788\uc744\uae4c?"
                    : recordChoice == RecordChoice::Sold
                        ? L"\uc7a5\ubd80\uac00 \ud314\ub838\ub2e4\ub294 \ub9d0\uc740 \ub4e4\uc5c8\uc5b4. \ub3c8\uc774 \uae09\ud55c \uac74 \uc54c\uc9c0\ub9cc, \ub0a8\uc740 \uac00\uc871\ub4e4\uc740 \uc5b4\uca4c\uc9c0?"
                        : L"\ub2f9\uc2e0\uc774 \uc870\uc0ac\ud55c\ub2e4\ub294 \uc774\uc57c\uae30\ub97c \ub4e3\uace0 \uc7a5\ud130\uc5d0 \ub098\uc654\uc5b4. \uae30\ub85d\uc744 \uc5b4\ub5bb\uac8c \ud560 \uc0dd\uac01\uc774\uc57c?";
                if (gateOpen)
                {
                    npc->line +=
                        L" \ubb3c\uae38\uc740 \uc5f4\ub838\uc73c\ub2c8 \uc0c8 \uace8\ubaa9\uc73c\ub85c \ubb3c\uac74\uc744 \uc62e\uae30\ub824\uace0.";
                }
            }
        }
        Perf::Count(Perf::Counter::QuestTransitions);
        Event(L"\ub0b4 \ud589\ub3d9\uc758 \uacb0\uacfc: " + result);
    }

    void World::UpdateQuest()
    {
        Perf::Scope scope(Perf::Cpu::QuestUpdate);
        int next = !briefed                                  ? 0
                   : !recoveredLedger || !recoveredKey       ? 1
                   : !copied                                 ? 2
                   : recordChoice == RecordChoice::Undecided ? 3
                   : !gateOpen                               ? 4
                   : !returned                               ? 5
                   : !explored                               ? 6
                                                             : 7;
        if (next != questStep)
        {
            questStep = next;
            Perf::Count(Perf::Counter::QuestTransitions);
            logTime = 0;
        }
    }

    std::wstring World::Objective() const
    {
        const wchar_t* goals[] = {
            L"1. \ubbf8\ub77c\uc5d0\uac8c \ub9d0\uc744 \uac78\uc5b4 \ub9c8\uc744 \uc0ac\uc815\uc744 \ub4e3\uc790 [E]",
            L"2. \ubb34\ub108\uc9c4 \ubc30\uc218\ub85c\uc5d0\uc11c \uc7a5\ubd80\uc640 \uc5f4\uc1e0\ub97c \ucc59\uae30\uc790 [F, \uac01\uac01 \ud55c \ubc88]",
            L"3. \uc7a5\ud130 \uac8c\uc2dc\ud310\uc5d0\uc11c \uc7a5\ubd80 \uc0ac\ubcf8\uc744 \ub0a8\uae30\uc790 [F]",
            L"4. \uc7a5\ubd80\ub97c \ub204\uad6c\uc5d0\uac8c \ub9e1\uae38\uc9c0 \uacb0\uc815\ud558\uc790",
            L"5. \uc61b \uc218\ubb38\uc73c\ub85c \uac00\uc11c \ubb3c\uae38\uc744 \uc5f4\uc790 [F]",
            L"6. \ubbf8\ub77c\uc5d0\uac8c \ub3cc\uc544\uac00 \ub2ec\ub77c\uc9c4 \ub9c8\uc744\uc744 \ud655\uc778\ud558\uc790 [E]",
            L"7. \ud070\uae38\uc744 \ub530\ub77c \uc194\ubc14\ub78c \uc0ac\ub0e5\uafbc \uac70\uc810\uc744 \ucc3e\uc544\ubcf4\uc790 [F]",
            L"\uccab \uc758\ub8b0 \uc644\ub8cc. \uc0ac\ub0e5\u00b7\uac70\ub798\u00b7\uc131\uc7a5\ud558\uba70 \uc790\uc720\ub86d\uac8c \ud0d0\ud5d8\ud558\uc790"};
        return goals[questStep];
    }

    std::wstring World::ObjectiveHint() const
    {
        if (questStep == 3)
        {
            return L"\uc7a5\ud130 R: \uacf5\uac1c / \uc138\ub77c\ud2b8 \uacc1 T: \uad00\uccad / \uc774\uc548 \uacc1 T: \ud310\ub9e4";
        }
        if (questStep == 1)
        {
            return std::wstring(L"\uc7a5\ubd80 ") +
                   (recoveredLedger ? L"\ud655\ubcf4" : L"\ubbf8\ud655\ubcf4") + L" / \uc5f4\uc1e0 " +
                   (recoveredKey ? L"\ud655\ubcf4" : L"\ubbf8\ud655\ubcf4") +
                   L" \u00b7 \uae08\ube5b \ubaa9\ud45c \ud45c\uc2dc\ub97c \ub530\ub77c\uac00\uc790";
        }
        if (questStep == 4 && !Holder(key))
        {
            return L"\uc5f4\uc1e0\uac00 \ub545\uc5d0 \ub5a8\uc5b4\uc838 \uc788\ub2e4. \uae08\ube5b \ud45c\uc2dc\ub97c \ub530\ub77c F\ub85c \ud68c\uc218\ud558\uc790";
        }
        if (questStep == 4 && Holder(key) != level.Player().Id())
        {
            return L"\uc5f4\uc1e0\ub97c \ub9e1\uae34 \uc218\ub9ac\uacf5\uc774 \uc218\ubb38\uc73c\ub85c \uac04\ub2e4. \uc9c1\uc811 \uac00\uc11c \ud655\uc778\ud558\uc790";
        }
        return L"\uae08\ube5b \ud45c\uc2dc\ub294 \ud604\uc7ac \ubaa9\uc801\uc9c0 \u00b7 M \uc9c0\ub3c4 \uc804\ud658 \u00b7 J \uc758\ub8b0\uc640 \uc120\ud0dd \uacb0\uacfc";
    }

    std::wstring World::Consequence() const
    {
        std::wstring choice = recordChoice == RecordChoice::Public
                                  ? L"[\uacf5\uac1c \u00b7 \uac00\uc871\ub4e4\uc758 \uc99d\uc5b8]"
                              : recordChoice == RecordChoice::Authority
                                  ? L"[\uad00\uccad \u00b7 \ud604\uc7a5 \ud1b5\uc81c]"
                              : recordChoice == RecordChoice::Sold
                                  ? L"[\ud310\ub9e4 \u00b7 \uc6d0\ubcf8\uc740 \ubd80\ub450\ub85c]"
                                  : L"";
        return choice + (choice.empty() ? L"" : L" ") +
               (consequence.empty()
                    ? L"\ub2f9\uc2e0\uc774 \uc870\uc0ac\ub97c \uc2dc\uc791\ud558\uba74 \uc0ac\ub78c\ub4e4\uc774 \uc6c0\uc9c1\uc778\ub2e4."
                    : consequence);
    }

    ActorId World::ObjectiveTarget() const
    {
        auto artifactTarget = [&](ActorId artifact)
        {
            ActorId holder = Holder(artifact);
            return holder ? holder : artifact;
        };
        if (questStep == 0 || questStep == 5)
        {
            const Npc* mira = Person(0);
            return mira && mira->health > 0 ? mira->Id() : sites[2];
        }
        if (questStep == 1)
        {
            return artifactTarget(recoveredLedger ? key : ledger);
        }
        if (questStep == 2 || questStep == 3)
        {
            return sites[2];
        }
        if (questStep == 4)
        {
            return Holder(key) ? sites[1] : key;
        }
        return questStep == 6 ? sites[4] : InvalidActorId;
    }

    ObjectiveGuide World::Guide() const
    {
        Perf::Scope scope(Perf::Cpu::QuestUpdate);
        ObjectiveGuide guide;
        guide.step = questStep;
        guide.target = ObjectiveTarget();
        const wchar_t* reasons[] = {
            L"\ubc30\uc218\ub85c\uac00 \ubb34\ub108\uc838 \uace8\ubaa9\uc5d0 \ubb3c\uc774 \ucc3c\ub2e4. \uc218\ub9ac\uacf5 \ubbf8\ub77c\uac00 \ub3c4\uc640\uc904 \uc0ac\ub78c\uc744 \ucc3e\uace0 \uc788\ub2e4.",
            L"\ubbf8\ub77c\ub294 \ubc30\uc218\ub85c\ub97c \ubc1b\uce58\ub294 \uc911\uc774\ub2e4. \ubb3c\uc5d0 \uc7a0\uae30\uae30 \uc804\uc5d0 \uc7a5\ubd80\uc640 \uc218\ubb38 \uc5f4\uc1e0\ub97c \ucc3e\uc544 \uc8fc\uc790.",
            L"\uc7a5\ubd80\uc5d0\ub294 \uc2e4\uc885\ub41c \uc0ac\ub78c\ub4e4\uc758 \uc774\ub984\uc774 \uc788\ub2e4. \uc6d0\ubcf8\uc744 \ub118\uae30\uae30 \uc804\uc5d0 \uc99d\uac70\ub97c \ub0a8\uaca8 \ub450\uc790.",
            L"\uac00\uc871\ub4e4\uc5d0\uac8c \uc54c\ub9b4\uc9c0, \uad00\uccad\uc758 \uc870\uc0ac\ub97c \ubbff\uc744\uc9c0, \ub3c8\uc744 \ubc1b\uc744\uc9c0 \uc9c1\uc811 \uacb0\uc815\ud574\uc57c \ud55c\ub2e4.",
            L"\uc5f4\uc1e0\ub85c \uc61b \uc218\ubb38\uc744 \uc5f4\uba74 \uc7a0\uae34 \uace8\ubaa9\uc744 \ub2e4\uc2dc \uae38\ub85c \uc4f8 \uc218 \uc788\ub2e4.",
            L"\ubb3c\uae38\uc774 \uc5f4\ub838\ub2e4. \ubbf8\ub77c\uc5d0\uac8c \ub3cc\uc544\uac00 \ubcf4\uc0c1\uc744 \ubc1b\uace0 \ub9c8\uc744\uc758 \ubcc0\ud654\ub97c \ud655\uc778\ud558\uc790.",
            L"\ub9c8\uc744 \uc77c\uc740 \ub9c8\ucce4\ub2e4. \uc0ac\ub0e5\uafbc \uac70\uc810\uc744 \ucc3e\uc73c\uba74 \ubc14\uae65\uc138\uc0c1\uc5d0\uc11c \uc0b4\uc544\uac08 \uae38\uc744 \uc54c \uc218 \uc788\ub2e4.",
            L"\uccab \uc758\ub8b0\ub97c \ub9c8\ucce4\ub2e4. \uc774\uc81c \uc0ac\ub0e5\uacfc \uac70\ub798\ub85c \uc131\uc7a5\ud558\uba70 \uc790\uc720\ub86d\uac8c \ud0d0\ud5d8\ud560 \uc218 \uc788\ub2e4."};
        guide.reason = reasons[questStep];
        const Actor* target = level.Scene().Find(guide.target);
        if (!target)
        {
            guide.destination = L"\uc790\uc720 \ud0d0\ud5d8";
            guide.action =
                L"SPACE \uacf5\uaca9 \u00b7 E \uc804\ub9ac\ud488 \u00b7 T \uac70\ub798 \u00b7 TAB \ub2a5\ub825\uce58";
            return guide;
        }
        const Npc* person = dynamic_cast<const Npc*>(target);
        const Landmark* landmark = dynamic_cast<const Landmark*>(target);
        if (landmark)
        {
            guide.destination = landmark->site == 2   ? L"\uc7a5\ud130 \uac8c\uc2dc\ud310"
                                : landmark->site == 1 ? L"\uc61b \uc218\ubb38"
                                : landmark->site == 4 ? L"\uc194\ubc14\ub78c \uc0ac\ub0e5\uafbc \uac70\uc810"
                                                      : landmark->label;
        }
        else
        {
            guide.destination = person                   ? person->name
                                : guide.target == ledger ? L"\ubc30\uc218\ub85c\uc758 \uc7a5\ubd80"
                                                         : L"\ubc30\uc218\ub85c\uc758 \uc5f4\uc1e0";
        }
        float distance = Distance(level.Player().WorldPosition(), target->WorldPosition());
        if (questStep == 0 || questStep == 5)
        {
            guide.interactionKey = person ? 'E' : 'F';
            guide.ready =
                distance < (person ? 1.5f : 1.8f) &&
                (!person || level.ClearLine(level.Player().WorldPosition(), target->WorldPosition()));
            guide.action = questStep == 0
                               ? (person ? L"[E] \ubbf8\ub77c\uc5d0\uac8c \uc0ac\uc815 \ub4e3\uae30"
                                         : L"[F] \uac8c\uc2dc\ud310 \uc758\ub8b0 \uc77d\uae30")
                               : (person ? L"[E] \ubbf8\ub77c\uc5d0\uac8c \uc644\ub8cc \ubcf4\uace0"
                                         : L"[F] \uac8c\uc2dc\ud310\uc5d0 \uc644\ub8cc \ubcf4\uace0");
        }
        else if (questStep == 1 || (questStep == 4 && !Holder(key)))
        {
            guide.interactionKey = 'F';
            guide.ready =
                distance < 1.5f && level.ClearLine(level.Player().WorldPosition(), target->WorldPosition());
            guide.action = guide.target == ledger ? L"[F] \uc7a5\ubd80 \ucc59\uae30\uae30"
                                                  : L"[F] \uc5f4\uc1e0 \ucc59\uae30\uae30";
        }
        else if (questStep == 2)
        {
            guide.interactionKey = 'F';
            guide.ready = distance < 1.8f;
            guide.action = L"[F] \uac8c\uc2dc\ud310\uc5d0\uc11c \uc7a5\ubd80 \uc0ac\ubcf8 \ub9cc\ub4e4\uae30";
        }
        else if (questStep == 3)
        {
            guide.ready = distance <= WorldLayout::MarketRadius;
            guide.action =
                L"R \uacf5\uac1c / \uc138\ub77c\ud2b8 \uacc1 T \uad00\uccad / \uc774\uc548 \uacc1 T \ud310\ub9e4 \u00b7 H\ub85c \uacb0\uacfc \ube44\uad50";
        }
        else if (questStep == 4)
        {
            bool delegated = Holder(key) != level.Player().Id();
            guide.interactionKey = delegated ? 0 : 'F';
            guide.ready = !delegated && distance < 1.8f;
            guide.action =
                delegated
                    ? L"\uc5f4\uc1e0\ub97c \ub9e1\uae34 \uc218\ub9ac\uacf5\uc774 \uc624\ub294 \uc911\uc774\ub2e4. \uc218\ubb38\uc5d0\uc11c \uae30\ub2e4\ub9ac\uc790."
                    : L"[F] \uc5f4\uc1e0\ub85c \uc61b \uc218\ubb38 \uc5f4\uae30";
        }
        else if (questStep == 6)
        {
            guide.interactionKey = 'F';
            guide.ready = distance < 1.8f;
            guide.action = L"[F] \uc0ac\ub0e5\uafbc \uac70\uc810 \ubc1c\uacac\ud558\uae30";
        }
        return guide;
    }

    void World::ChooseRecord(RecordChoice choice)
    {
        if (recordChoice != RecordChoice::Undecided)
        {
            return;
        }
        recordChoice = choice;
        if (choice == RecordChoice::Public)
        {
            Disclose(nullptr);
            if (Npc* clerk = Person(1))
            {
                clerk->line =
                    L"\ub2f9\uc2e0\uc774 \uc774\ub984\uc744 \uaebc\ub0b4 \uc900 \ub355\uc5d0 \uac00\uc871\ub4e4\uc774 \ubaa8\uc774\uace0 \uc788\uc5b4. \ud558\uc9c0\ub9cc \uc228\uace0 \uc2f6\uc740 \uc0ac\ub78c\ub3c4 \uc788\ub354\uad70.";
            }
            if (Npc* guard = Person(2))
            {
                guard->line =
                    L"\ub2f9\uc2e0\uc774 \ud37c\ub728\ub9b0 \uae30\ub85d \ub54c\ubb38\uc5d0 \uc7a5\ud130\uac00 \uc220\ub801\uc774\uc624. \uc99d\uc5b8\uc740 \ub4e3\uaca0\uc9c0\ub9cc \uc9c8\uc11c\ub3c4 \uc9c0\ucf1c\uc57c \ud558\uc624.";
            }
            ReactToPlayer(
                L"\uc7a5\ubd80 \uacf5\uac1c \u2192 \ub3c4\uc724\uacfc \uc8fc\ubbfc\ub4e4\uc774 \uc7a5\ud130\uc5d0 \ubaa8\uc774\uace0, \uc138\ub77c\ud2b8\uac00 \uc8fc\ubcc0\uc744 \uc21c\ucc30\ud55c\ub2e4.");
        }
        else if (choice == RecordChoice::Authority)
        {
            trust[0] = Bounded(trust[0] + 15, -100, 100);
            trust[1] = Bounded(trust[1] - 8, -100, 100);
            if (Npc* guard = Person(2))
            {
                guard->line =
                    L"\uae30\ub85d\uc740 \ub0b4\uac00 \ubcf4\uad00\ud558\uaca0\uc18c. \ud604\uc7a5\uc740 \ud1b5\uc81c\ud558\ub418 \ub2f9\uc2e0\uc774 \uc4f8 \uc6b0\ud68c\ub85c\ub294 \ub0a8\uaca8 \ub450\uaca0\uc18c.";
            }
            if (Npc* clerk = Person(1))
            {
                clerk->line =
                    L"\uad00\uccad\uc5d0 \ub118\uacbc\uad6c\ub098. \uc870\uc0ac\ub294 \uc870\uc6a9\ud574\uc84c\uc9c0\ub9cc, \uac00\uc871\ub4e4\uc740 \uadf8 \uc774\ub984\uc744 \ubabb \ub4e3\uac8c \ub410\uc5b4.";
            }
            ReactToPlayer(
                L"\uad00\uccad\uc5d0 \uc7a5\ubd80\ub97c \ub118\uae40 \u2192 \uc138\ub77c\ud2b8\uac00 \ubc30\uc218\ub85c\ub97c \ud1b5\uc81c\ud558\uace0 \uc8fc\ubbfc\ub4e4\uc740 \uc9d1\uc73c\ub85c \ubb3c\ub7ec\ub09c\ub2e4.");
        }
        else
        {
            trust[3] = Bounded(trust[3] + 12, -100, 100);
            trust[1] = Bounded(trust[1] - 12, -100, 100);
            if (Npc* broker = Person(3))
            {
                broker->line =
                    L"\ub2f9\uc2e0\uc5d0\uac8c \uac12\uc744 \uce58\ub800\uc9c0. \uc774\uc81c \ubd80\ub450\uc758 \uad6c\ub9e4\uc790\ub97c \ub9cc\ub098\uc57c\uaca0\uc5b4. \uc0ac\ubcf8\uc740 \ub2f9\uc2e0 \ucc45\uc784\uc774\uc57c.";
            }
            ReactToPlayer(
                L"\uc7a5\ubd80 \ud310\ub9e4 \u2192 40\ub0e5\uc744 \ubc1b\uace0 \uc774\uc548\uc774 \ubd80\ub450\ub85c \uac04\ub2e4. \uc8fc\ubbfc\ub4e4\uc740 \uae30\ub85d\uc744 \ub4e3\uc9c0 \ubabb\ud55c\ub2e4.");
        }
    }

    void World::Initialize()
    {
        level.Scene().DestroyChildren(level.NpcRoot());
        if (worldRoot)
        {
            level.Scene().Destroy(worldRoot);
        }
        worldRoot = level.Scene().Create(level.Scene().Root().Id(), "Harbor").Id();
        minutes = 360;
        logTime = gossipTime = 0;
        statisticsTime = 0;
        villageStatistics = {};
        trust.fill(0);
        history.clear();
        gateOpen = published = controlled = repaired = copied = ledgerWet = false;
        flood = 0;
        briefed = recoveredLedger = recoveredKey = returned = explored = dealSettled = false;
        EndDialogue();
        recordChoice = RecordChoice::Undecided;
        questStep = 0;
        consequence.clear();
        random.seed(level.seed ^ 0x48415242u);

        struct SiteDefinition
        {
            const wchar_t* label;
            Position p;
            const char* model;
        };

        const SiteDefinition definitions[] = {
            {L"\ubb34\ub108\uc9c4 \ubc30\uc218\ub85c", WorldLayout::Town(23.5f, 12.5f), "drain"},
            {L"\uc61b \uc218\ubb38", WorldLayout::Town(23.5f, 24.5f), "sluice"},
            {L"\uc870\ud569 \uc0ac\ubb34\uc18c \u00b7 \uc7a5\ud130", WorldLayout::VillageSpawn(), "notice"},
            {L"\ud3d0\uc120 \ubd80\ub450", WorldLayout::Town(28.5f, 17.5f), "wreck"},
            {WorldLayout::VillageName(1), WorldLayout::HunterCamp(), "notice"},
            {WorldLayout::VillageName(2), WorldLayout::Waystation(), "notice"},
            {WorldLayout::VillageName(3), WorldLayout::OldShrine(), "notice"},
            {L"\ubd89\uc740\uace8 \uc57c\uc601\uc9c0 \u00b7 \uc704\ud5d8",
             WorldLayout::RaiderCamp(),
             "bandit_sign"},
            {WorldLayout::VillageName(4), WorldLayout::VillageCenter(4), "notice"}};
        for (int i = 0; i < int(sites.size()); ++i)
        {
            Landmark& node = level.Scene().Create<Landmark>(worldRoot, "HarborSite", ActorKind::Prop);
            node.SetWorldPosition(Reachable(definitions[i].p));
            node.label = definitions[i].label;
            node.site = i;
            node.model = definitions[i].model;
            node.drawable = true;
            node.shadowWidth = 65;
            sites[i] = node.Id();
        }
        for (int i = 0; i < 2; ++i)
        {
            Actor& item = level.Scene().Create(worldRoot, i ? "SluiceKey" : "MissingLedger", ActorKind::Prop);
            item.SetWorldPosition(Site(0));
            item.model = i ? "key" : "ledger";
            item.drawable = true;
            item.shadowWidth = 15;
            if (i)
            {
                key = item.Id();
                item.SetLocalPosition({Site(0).x, Site(0).y + .3f, 0});
            }
            else
            {
                ledger = item.Id();
            }
        }
        Actor& cordon = level.Scene().Create(worldRoot, "DrainCordon", ActorKind::Prop);
        barrier = cordon.Id();
        cordon.SetWorldPosition(Reachable(WorldLayout::Town(22.5f, 12.5f)));
        cordon.model = "prop3";
        cordon.SetVisible(false);
        cordon.drawable = true;
        cordon.shadowWidth = 40;

        struct Definition
        {
            const wchar_t* name;
            Job job;
            int faction;
            Position p;
            const wchar_t* line;
        };

        const Definition people[] = {
            {L"\ubbf8\ub77c \ubca4",
             Job::Engineer,
             2,
             {12, 10, 0},
             L"\ubb3c\uc774 \ube60\uc84c\ub2e4\uace0 \ub05d\ub09c \uac8c \uc544\ub2c8\uc57c. \uba3c\uc800 \ubc30\uc218\ub85c\ubd80\ud130 \ubd10\uc57c\uaca0\uc5b4."},
            {L"\ub3c4\uc724 \ub80c",
             Job::Clerk,
             1,
             {11, 13, 0},
             L"\uc5f4\ub450 \ud574 \uc804 \uba85\ubd80\ub77c\ub2c8\u2026 \ub0b4 \ub3d9\uc0dd \uc774\ub984\ub3c4 \uc788\uc744\uae4c. \uc870\ud569 \uc0ac\ub78c\ub4e4\ubd80\ud130 \ub2e4\uce58\uac8c \ud560 \uc21c \uc5c6\uc9c0."},
            {L"\uc138\ub77c\ud2b8 \uacbd",
             Job::Guard,
             0,
             {14, 6, 0},
             L"\uc18c\ubb38\ub9cc\uc73c\ub85c \ud56d\uad6c\ub97c \ub4a4\uc9d1\uc744 \uc21c \uc5c6\uc18c. \uae30\ub85d\uc744 \uba3c\uc800 \ud655\uc778\ud558\uaca0\uc18c."},
            {L"\uc774\uc548 \ub178\uc744",
             Job::Broker,
             3,
             {16, 9, 0},
             L"\ubb3c\uac74\ub3c4 \uc18c\ubb38\ub3c4 \uc784\uc790\uac00 \ub530\ub85c \uc788\uc9c0. \uac12\uc744 \ub9de\ucd94\uba74 \uc11c\ub85c \ub35c \uace4\ub780\ud574\uc838."},
            {L"\ud558\ub78c",
             Job::Apprentice,
             2,
             {16, 12, 0},
             L"\uc6b0\ub9ac \uace8\ubaa9\uc5d4 \uc544\uc9c1 \ubb3c\uc774 \ucc28 \uc788\uc5b4. \uc61b \uc218\ubb38\uc744 \uc5f4 \uc218\ub9cc \uc788\ub2e4\uba74\u2026"},
            {L"\uc194\uac1c",
             Job::Hunter,
             1,
             {8, 14, 0},
             L"\uac00\uc8fd\uc774 \uc788\uc5b4\uc57c \uc300\uc744 \uc0ac\uc9c0. \uc232\uc5d0\uc11c \ub3cc\uc544\uc624\uba74 \uc7a5\ud130\uc5d0 \ub4e4\ub97c \uac70\uc57c."},
            {L"\uc5f0\ub450",
             Job::Hunter,
             2,
             {15, 16, 0},
             L"\uc624\ub298\uc740 \ubb34\ub9ac\ud558\uc9c0 \uc54a\uc73c\ub824\uace0. \ub2e4\uce58\uba74 \uc9d1\uc5d0 \uae30\ub2e4\ub9ac\ub294 \uc0ac\ub78c\uc774 \ub354 \ud798\ub4e4\uc5b4."},
            {L"\uba39\uc1e0",
             Job::Raider,
             3,
             {20, 17, 0},
             L"\ub0b4 \ubc30\ubd80\ud130 \ucc44\uc6cc\uc57c\uc9c0. \ub0a8\uc758 \uc8fc\uba38\ub2c8 \uc0ac\uc815\uae4c\uc9c0 \ubd10 \uc904 \uc5ec\uc720\ub294 \uc5c6\uc5b4."},
            {L"\ud751\ube44",
             Job::Raider,
             3,
             {21, 21, 0},
             L"\uce7c\uc744 \uaebc\ub0b4\uae30 \uc804\uc5d0 \uac12\ubd80\ud130 \ubd88\ub7ec \ubd10. \uc11c\ub85c \uc0b4\uc544 \ub3cc\uc544\uac00\uba74 \uc88b\uc796\uc544."},
            {L"\uc7a5\uc528",
             Job::Merchant,
             1,
             {12, 9, 0},
             L"\uac00\uc8fd\uc740 \ub0b4\uac00 \uc0ac\uc9c0. \ub2e4 \ud314\uc558\uc73c\uba74 \uc57d\ub3c4 \uc880 \ucc59\uaca8 \uac00."},
            {L"\uc21c\ub355",
             Job::Merchant,
             2,
             {14, 9, 0},
             L"\uc0c1\ucc98\ub97c \ub36e\uc5b4 \ub450\uba74 \ub367\ub098. \uc57d \ud55c \ubcd1\uc5d0 \uc5ec\ub35f \ub0e5\uc774\uc57c."},
            {L"\uc724 \uc11c\uc0dd",
             Job::Scholar,
             2,
             {11, 7, 0},
             L"\uae30\ub85d\uc744 \uc77d\ub294 \uc77c\uacfc \uc9c4\uc2e4\uc744 \uc544\ub294 \uc77c\uc740 \ub2e4\ub974\ub354\uad70. \uc624\ub298\ub3c4 \ubc30\uc6cc\uc57c\uc9c0."},
            {L"\uc8fc\ubaa8",
             Job::Resident,
             2,
             {16, 10, 0},
             L"\uad6d \ud55c \uadf8\ub987 \uba39\uace0 \uac00. \uae09\ud55c \ub9c8\uc74c\uc73c\ub85c \uce7c\ubd80\ud130 \ube7c\uc9c4 \ub9d0\uace0."},
            {L"\ub3c4\uacf5",
             Job::Resident,
             1,
             {9, 9, 0},
             L"\ubd80\ub450\uac00 \uc7a0\uae30\uba74 \uadf8\ub987\ub3c4 \ubabb \ub0b4\ub2e4 \ud314\uc544. \ubb3c\uae38\uc774 \uc880 \ub6ab\ub824\uc57c \ud560 \ud150\ub370."},
            {L"\ubc15 \uc218\ub9ac\uacf5",
             Job::Engineer,
             1,
             {15, 15, 0},
             L"\uc218\ubb38\uc744 \uc5f4\uba74 \uace8\ubaa9\uc740 \uc0b4\uaca0\uc9c0. \ub300\uc2e0 \uc544\ub798 \uae30\ub85d \ubcf4\uad00\uc18c\ub294 \uc816\uc744 \uac70\uc57c."},
            {L"\uac15 \uc21c\ub77c\uafbc",
             Job::Guard,
             0,
             {14, 15, 0},
             L"\uc7a5\ud130\uc5d0\uc120 \uce7c \ub0b4\ub824\ub193\uc73c\uc2dc\uc624. \uc2f8\uc6c0\uc774 \ub098\uba74 \uac00\ub9cc\uc788\uc744 \uc21c \uc5c6\uc18c."},
            {L"\uc9c4 \ubc43\uc0ac\uacf5",
             Job::Hunter,
             1,
             {17, 19, 0},
             L"\ubc30\uac00 \ubabb \ub728\ub2c8 \uc232\uc774\ub77c\ub3c4 \uac00\uc57c\uc9c0. \ubc8c\uc5b4 \uc628 \uac74 \uc5ec\uae30\uc11c \ubc14\uafd4."},
            {L"\uc6b0 \uc0c1\uc778",
             Job::Merchant,
             3,
             {13, 15, 0},
             L"\uac12\uc740 \uc0ac\uc815\uc5d0 \ub530\ub77c \ub2ec\ub77c\uc838. \ubbff\uc744 \ub9cc\ud55c \uc190\ub2d8\ud55c\ud150 \uc870\uae08 \uae4e\uc544 \uc8fc\uace0."}};
        std::array<int, WorldLayout::VillageCount> residentSlots{};
        const int civilianLooks[] = {0, 1, 2, 3, 4, 5, 6, 9, 10, 11, 12, 13, 14, 15, 16, 17};
        for (int i = 0; i < WorldLayout::VillageCount * WorldLayout::NpcsPerVillage; ++i)
        {
            int village = 0;
            if (i < WorldLayout::NpcAppearanceCount)
            {
                village = i == 7 || i == 8 ? 4 : i == 5 || i == 6 || i == 16 ? 1 : 0;
            }
            else
            {
                while (residentSlots[village] >= WorldLayout::NpcsPerVillage)
                {
                    ++village;
                }
            }
            int slot = residentSlots[village]++;
            bool courier = slot >= WorldLayout::NpcsPerVillage - 2;
            int appearance = i < WorldLayout::NpcAppearanceCount ? i
                             : courier                           ? (slot % 2 ? 17 : 9)
                                                                 : civilianLooks[slot % 16];
            const auto& source = people[appearance];
            Npc& npc = level.Scene().Create<Npc>(level.NpcRoot(), "HarborResident", ActorKind::Npc);
            npc.name = i < WorldLayout::NpcAppearanceCount
                           ? source.name
                           : std::wstring(WorldLayout::VillageName(village)) + L" " + source.name + L" " +
                                 std::to_wstring(slot + 1);
            npc.line = source.line;
            if (i >= WorldLayout::NpcAppearanceCount)
            {
                npc.line =
                    courier
                        ? L"\uc7a5\ud130\ub97c \uc624\uac00\uba70 \ubb3c\uac74\ub3c4 \ud314\uace0 \uc0ac\ub78c\ub4e4 \uc18c\uc2dd\ub3c4 \ub4e3\uace0 \uc788\uc5b4."
                        : L"\uba39\uace0\uc0b4\ub824\uba74 \uc7a5\ud130\uc5d0\ub3c4 \ub098\uac00\uace0 \uc9d1\uc548\uc77c\ub3c4 \ub3cc\ubd10\uc57c\uc9c0.";
            }
            npc.job = source.job;
            npc.faction = source.faction;
            npc.appearance = appearance;
            npc.homeVillage = village;
            npc.courier = courier;
            npc.travelVillage = WorldLayout::VillageNeighbour(village);
            npc.courierWait = 3 + float(slot % 3);
            Position c = WorldLayout::VillageCenter(village);
            Position home = {c.x - 12 + float(slot % 5) * 6, c.y - 12 + float(slot / 5) * 6, 0};
            if (i < 5)
            {
                home = WorldLayout::Town(source.p.x, source.p.y);
            }
            npc.home = Reachable(home);
            Position initial = npc.home;
            if (npc.job == Job::Raider)
            {
                Position camp = WorldLayout::RaiderCamp();
                initial = Reachable({camp.x + (i == 7 ? -4.0f : 5.0f), camp.y - 3, 0});
                npc.home = initial;
            }
            npc.SetWorldPosition(initial);
            npc.destination = initial;
            npc.lastLedger = Site(0);
            npc.variant = i;
            npc.drawable = true;
            npc.model = "citizen" + std::to_string(appearance) + "_idle";
            npc.shadowWidth = 30;
            npc.shadowHeight = 62;
            npc.visualHeight = CharacterPresentation::NpcVisualHeight(appearance);
            npc.coins = IsMerchant(npc) ? 180 : 25 + slot * 2;
            if (i < 5 && !IsMerchant(npc))
            {
                npc.coins = 60;
            }
            npc.maxHealth = npc.job == Job::Guard ? 160 : 100;
            npc.health = npc.maxHealth;
            npc.thinkTime = float(i % 20) * .09f;
            npc.scheduled = i >= 5;
            npc.wealthWeight = npc.job == Job::Hunter || IsMerchant(npc) ? 1.6f : .8f;
            npc.enlightenmentWeight = npc.job == Job::Scholar ? 2.5f : .3f;
            npc.raidWeight = npc.job == Job::Raider ? 1.8f : 0;
            npc.raidCooldown = npc.job == Job::Raider ? 35 + float(i - 7) * 20 : 0;
            Actor& status = level.Scene().Create(npc.Id(), "NpcHealth", ActorKind::Effect);
            status.pass = ScenePass::Effects;
            status.variant = int(ActorEffect::NpcStatus);
            status.drawable = true;
        }
        Event(
            L"\ubb34\uc5f0\ud56d, \uccab\ub0a0 \uc544\uce68. \ubc30\uc218\ub85c\uac00 \ubb34\ub108\uc838 \uc61b \uc7a5\ubd80\uc640 \uc218\ubb38 \uc5f4\uc1e0\uac00 \ub4dc\ub7ec\ub0ac\ub2e4.");
    }

    void World::Observe(Npc& npc)
    {
        for (ActorId artifact : {ledger, key})
        {
            const Actor& item = level.Scene().Get(artifact);
            if (Distance(npc.WorldPosition(), item.WorldPosition()) < 4 &&
                level.ClearLine(npc.WorldPosition(), item.WorldPosition()))
            {
                npc.knowledge |= artifact == ledger ? Knowledge::Ledger : Knowledge::Key;
                if (artifact == ledger)
                {
                    npc.lastLedger = item.WorldPosition();
                    npc.knownLedgerHolder = Holder(ledger);
                    npc.searchedLedger = false;
                }
            }
        }
    }

    void World::Think(Npc& npc, const std::vector<Npc*>& npcs, const std::vector<Level::Enemy*>& beasts)
    {
        Perf::Scope scope(Perf::Cpu::NpcThink);
        Perf::Count(Perf::Counter::NpcDecisions);
        Observe(npc);
        npc.target = InvalidActorId;
        auto set = [&](Action action, const Position& destination, ActorId target = InvalidActorId)
        {
            if (npc.action != action)
            {
                npc.actionTime = 0;
                npc.raidHits = 0;
                if (action == Action::Raid)
                {
                    npc.raidWindow = 6;
                    npc.raidCooldown = 90 + float(npc.variant % 3) * 15;
                }
            }
            npc.action = action;
            npc.destination = destination;
            npc.target = target;
        };
        if (npc.health < npc.maxHealth * .4f && npc.food > 0)
        {
            set(Action::Rest, npc.home);
        }
        else if (npc.threatTime > 0)
        {
            Actor* threat = level.Scene().Find(npc.threat);
            Npc* other = dynamic_cast<Npc*>(threat);
            bool tooFarFromCamp =
                npc.job == Job::Raider &&
                Distance(npc.WorldPosition(), WorldLayout::RaiderCamp()) > WorldLayout::RaiderTerritoryRadius;
            if (threat && threat->IsVisible() && (!other || other->health > 0) && !tooFarFromCamp &&
                !(npc.job == Job::Raider && WorldLayout::InVillage(threat->WorldPosition())) &&
                Distance(npc.WorldPosition(), threat->WorldPosition()) < 8 &&
                level.ClearLine(npc.WorldPosition(), threat->WorldPosition()))
            {
                set(threat->Kind() == ActorKind::Enemy ? Action::Hunt : Action::Defend,
                    threat->WorldPosition(),
                    threat->Id());
            }
            else
            {
                set(Action::Return, npc.home);
            }
        }
        else if (npc.action == Action::Return && Distance(npc.WorldPosition(), npc.home) > .8f)
        {
            set(Action::Return, npc.home);
        }
        else if (npc.variant < 5)
        {
            // Story destinations are unlocked by player actions, not an elapsed-time schedule.
            if (!briefed)
            {
                set(Action::Rest, npc.home);
            }
            else if (npc.variant == 0)
            {
                bool delegated = Holder(key) == npc.Id() && !gateOpen;
                set(delegated || !repaired ? Action::Investigate : Action::Study,
                    delegated ? Site(1) : Site(0));
            }
            else if (npc.variant == 1)
            {
                set(Action::Study,
                    recordChoice == RecordChoice::Public ? Reachable(WorldLayout::Town(11.5f, 14.5f))
                                                         : npc.home);
            }
            else if (npc.variant == 2)
            {
                Position post =
                    recordChoice == RecordChoice::Authority ? Site(0)
                    : recordChoice == RecordChoice::Public
                        ? Reachable(WorldLayout::Town(15.5f + std::sin(float(minutes) * .12f) * 2,
                                                      13.5f + std::cos(float(minutes) * .12f) * 2))
                    : copied ? Reachable(WorldLayout::Town(13.5f, 10.5f))
                             : npc.home;
                set(recordChoice == RecordChoice::Authority && !controlled ? Action::Investigate
                                                                           : Action::Patrol,
                    post);
            }
            else if (npc.variant == 3)
            {
                set(recordChoice == RecordChoice::Sold && !dealSettled ? Action::Investigate : Action::Trade,
                    recordChoice == RecordChoice::Sold ? Site(3)
                    : copied                           ? Reachable(WorldLayout::Town(16.5f, 12.5f))
                                                       : npc.home);
            }
            else
            {
                bool delegated = Holder(key) == npc.Id() && !gateOpen;
                set(delegated ? Action::Investigate : Action::Study,
                    delegated || gateOpen ? Site(1) : npc.home);
            }
        }
        else if (Holder(key) == npc.Id() && !gateOpen &&
                 (npc.knowledge & Knowledge::Plans || npc.job == Job::Engineer))
        {
            set(Action::Investigate, Site(1));
        }
        else if (npc.courier)
        {
            Position destination = WorldLayout::VillageMarket(npc.travelVillage);
            if (Distance(npc.WorldPosition(), destination) < 1.2f)
            {
                if (npc.courierWait > 0)
                {
                    set(Action::Trade, destination);
                }
                else
                {
                    npc.travelVillage = npc.travelVillage == npc.homeVillage
                                            ? WorldLayout::VillageNeighbour(npc.homeVillage)
                                            : npc.homeVillage;
                    npc.courierWait = 4;
                    Perf::Count(Perf::Counter::CourierDepartures);
                    set(Action::Travel, WorldLayout::VillageMarket(npc.travelVillage));
                }
            }
            else
            {
                set(Action::Travel, destination);
            }
        }
        else if (npc.variant >= 9 && npc.homeVillage == 0 && npc.job != Job::Hunter && briefed &&
                 (recordChoice != RecordChoice::Public || (npc.knowledge & Knowledge::Published)))
        {
            Position gathering =
                gateOpen ? WorldLayout::Town(27.5f + float(npc.variant % 3), 21.5f + float(npc.variant % 4))
                : recordChoice == RecordChoice::Public || recordChoice == RecordChoice::Undecided
                    ? WorldLayout::Town(10.5f + float(npc.variant % 4) * 2, 15.5f + float(npc.variant % 2))
                    : npc.home;
            set(npc.job == Job::Guard ? Action::Patrol
                : gateOpen || recordChoice == RecordChoice::Public || recordChoice == RecordChoice::Undecided
                    ? Action::Trade
                    : Action::Rest,
                Reachable(gathering));
        }
        else if (IsMerchant(npc))
        {
            Position stall = WorldLayout::VillageMarket(npc.homeVillage);
            stall.x += float(npc.variant % 3 - 1) * .8f;
            stall.y += float((npc.variant / 3) % 3 - 1) * .8f;
            set(Action::Trade, stall);
        }
        else if (npc.job == Job::Guard)
        {
            set(Action::Patrol,
                Reachable({npc.home.x + std::sin(float(minutes) * .08f) * 2,
                           npc.home.y + std::cos(float(minutes) * .08f) * 2,
                           0}));
        }
        else
        {
            Npc* victim = nullptr;
            Level::Enemy* prey = nullptr;
            Npc* merchant = nullptr;
            float victimDistance = 7, preyDistance = 9, merchantDistance = 1000;
            for (Npc* candidate : npcs)
            {
                if (candidate == &npc || candidate->health <= 0)
                {
                    continue;
                }
                float d = Distance(npc.WorldPosition(), candidate->WorldPosition());
                if (npc.job != Job::Raider && IsMerchant(*candidate) && d < merchantDistance &&
                    WorldLayout::InVillage(candidate->WorldPosition()))
                {
                    merchant = candidate;
                    merchantDistance = d;
                }
                if (npc.job == Job::Raider && RaidAllowed(npc, *candidate) && d < victimDistance &&
                    level.ClearLine(npc.WorldPosition(), candidate->WorldPosition()))
                {
                    victim = candidate;
                    victimDistance = d;
                }
            }
            for (Level::Enemy* candidate : beasts)
            {
                float d = Distance(npc.WorldPosition(), candidate->WorldPosition());
                if (candidate->health > 0 && d < preyDistance &&
                    level.ClearLine(npc.WorldPosition(), candidate->WorldPosition()))
                {
                    prey = candidate;
                    preyDistance = d;
                }
            }
            bool raidPlayer =
                RaidAllowed(npc, level.Player()) &&
                Distance(npc.WorldPosition(), level.Player().WorldPosition()) < victimDistance &&
                level.ClearLine(npc.WorldPosition(), level.Player().WorldPosition());
            float rest = npc.food > 0 ? npc.healthWeight * (1 - float(npc.health) / npc.maxHealth) * 5 : 0;
            float trade = npc.job != Job::Raider && npc.hides > 0 && merchant ? npc.wealthWeight * 2.5f : 0;
            bool needsFood = npc.health < npc.maxHealth / 2 && npc.food == 0 && npc.coins >= 8 && merchant &&
                             merchant->food > 0;
            if (needsFood)
            {
                trade = 10;
            }
            float raid = raidPlayer ? npc.raidWeight * (1 + (std::min)(level.stats.coins, 100) / 100.0f)
                         : victim   ? npc.raidWeight * (1 + (std::min)(victim->coins, 100) / 100.0f)
                                    : 0;
            float study = npc.enlightenmentWeight;
            float hunt = prey && npc.job == Job::Hunter ? npc.wealthWeight * 1.5f : 0;
            if (rest > (std::max)({trade, raid, study, hunt}))
            {
                set(Action::Rest, npc.home);
            }
            else if (raid > (std::max)({trade, study, hunt}) && (victim || raidPlayer))
            {
                set(Action::Raid,
                    raidPlayer ? level.Player().WorldPosition() : victim->WorldPosition(),
                    raidPlayer ? level.Player().Id() : victim->Id());
            }
            else if (trade >= (std::max)(study, hunt) && trade > 0)
            {
                set(Action::Trade, merchant->WorldPosition(), merchant->Id());
            }
            else if (hunt > study && prey)
            {
                set(Action::Hunt, prey->WorldPosition(), prey->Id());
            }
            else if (npc.job == Job::Hunter || npc.job == Job::Raider)
            {
                if (Distance(npc.WorldPosition(), npc.destination) < 1 || npc.action != Action::Patrol)
                {
                    Position center = npc.job == Job::Raider ? WorldLayout::RaiderCamp()
                                                             : WorldLayout::VillageCenter(npc.homeVillage);
                    int radius = npc.job == Job::Raider ? 13 : 35;
                    Position destination =
                        Reachable({center.x + float(int(random() % (radius * 2 + 1)) - radius),
                                   center.y + float(int(random() % (radius * 2 + 1)) - radius),
                                   0});
                    if (WorldLayout::InVillage(destination))
                    {
                        destination = Reachable(center);
                    }
                    set(Action::Patrol, destination);
                }
                else
                {
                    set(Action::Patrol, npc.destination);
                }
            }
            else
            {
                bool visitingMarket = (int(minutes / 12) + npc.variant) % 2 != 0;
                Position meeting = WorldLayout::VillageMarket(npc.homeVillage);
                meeting.x += float(npc.variant % 3 - 1) * .8f;
                meeting.y += float((npc.variant / 3) % 3 - 1) * .8f;
                set(npc.job == Job::Scholar ? Action::Study
                    : visitingMarket        ? Action::Trade
                                            : Action::Rest,
                    visitingMarket ? meeting : npc.home);
            }
        }
        if (Distance(npc.WorldPosition(), npc.destination) > .5f)
        {
            bool reusable = npc.waypoint < npc.path.size() &&
                            Distance(npc.pathDestination, npc.destination) < 1.2f &&
                            npc.pathRevision == level.Scene().CollisionRevision();
            if (reusable)
            {
                Perf::Count(Perf::Counter::NpcPathReuse);
            }
            else
            {
                npc.path = level.FindPath(npc.WorldPosition(), npc.destination);
                npc.waypoint = 0;
                npc.pathRevision = level.Scene().CollisionRevision();
                npc.pathDestination = npc.destination;
                if (npc.path.empty())
                {
                    Perf::Count(Perf::Counter::NpcPathFailures);
                    npc.destination = Reachable(npc.destination);
                    npc.path = level.FindPath(npc.WorldPosition(), npc.destination);
                    npc.pathDestination = npc.destination;
                }
            }
        }
        else
        {
            npc.path.clear();
            npc.waypoint = 0;
        }
    }

    bool World::RaidAllowed(const Npc& attacker, const Actor& target) const
    {
        const auto* victim = dynamic_cast<const Npc*>(&target);
        bool allowed =
            attacker.job == Job::Raider &&
            (attacker.raidCooldown <= 0 || (attacker.action == Action::Raid && attacker.raidWindow > 0)) &&
            attacker.health > attacker.maxHealth * .65f &&
            !WorldLayout::InVillage(attacker.WorldPosition()) &&
            !WorldLayout::InVillage(target.WorldPosition()) &&
            Distance(attacker.WorldPosition(), WorldLayout::RaiderCamp()) <=
                WorldLayout::RaiderTerritoryRadius &&
            Distance(target.WorldPosition(), WorldLayout::RaiderCamp()) <=
                WorldLayout::RaiderTerritoryRadius &&
            (victim ? victim->variant >= 5 && victim->job != Job::Guard && victim->job != Job::Raider &&
                          victim->health > 0 && victim->coins >= 15
                    : target.Id() == level.Player().Id() && level.stats.coins >= 15);
        if (!allowed && attacker.job == Job::Raider)
        {
            Perf::Count(Perf::Counter::RaidAttemptsBlocked);
        }
        return allowed;
    }

    void World::Witness(ActorId attacker, const Position& where, int affinity, int karma)
    {
        for (Npc* witness : level.Scene().Actors<Npc>(ActorKind::Npc))
        {
            if (witness->health <= 0 || witness->Id() == attacker ||
                Distance(witness->WorldPosition(), where) > 6 ||
                !level.ClearLine(witness->WorldPosition(), where))
            {
                continue;
            }
            if (attacker == level.Player().Id())
            {
                witness->affinity = Bounded(witness->affinity + affinity, -100, 100);
                witness->karma = Bounded(witness->karma + karma, -100, 100);
                trust[witness->faction] = Bounded(trust[witness->faction] + affinity / 4, -100, 100);
            }
            if (witness->job == Job::Guard && affinity < 0)
            {
                witness->threat = attacker;
                witness->threatTime = 25;
                witness->thinkTime = 0;
            }
        }
    }

    void World::Hurt(Npc& victim, int damage, ActorId attacker)
    {
        Perf::Scope scope(Perf::Cpu::NpcCombat);
        const auto* source = dynamic_cast<const Npc*>(level.Scene().Find(attacker));
        if (source && (victim.variant < 5 ||
                       (WorldLayout::InVillage(victim.WorldPosition()) && source->action != Action::Defend)))
        {
            Perf::Count(Perf::Counter::RaidAttemptsBlocked);
            return;
        }
        if (victim.health <= 0)
        {
            return;
        }
        Perf::Count(Perf::Counter::NpcCombatHits);
        victim.health = (std::max)(0, victim.health - damage);
        victim.threat = attacker;
        victim.threatTime = 15;
        victim.thinkTime = 0;
        Witness(attacker, victim.WorldPosition(), -8, -3);
        if (victim.health == 0)
        {
            level.CancelEnemyAttacksTargeting(victim.Id());
            if (attacker == level.Player().Id())
            {
                level.stats.GainExperience(12);
                ++level.stats.kills;
            }
            Perf::Count(Perf::Counter::NpcDeaths);
            victim.action = Action::Dead;
            victim.path.clear();
            victim.moving = false;
            victim.model = "corpse";
            for (ActorId artifact : {ledger, key})
            {
                if (Holder(artifact) == victim.Id())
                {
                    Position p = victim.WorldPosition();
                    Transfer(artifact, InvalidActorId);
                    level.Scene().Get(artifact).SetWorldPosition(p);
                }
            }
            Event(
                victim.name +
                    L"\uc774 \uc4f0\ub7ec\uc84c\ub2e4. \ub0a8\uc740 \ubb3c\uac74\uc740 \uadf8 \uc790\ub9ac\uc5d0 \uc788\ub2e4.",
                attacker == level.Player().Id() ||
                    Distance(victim.WorldPosition(), level.Player().WorldPosition()) < 12);
        }
    }

    void World::Exchange(Npc& seller, Npc& buyer)
    {
        Perf::Scope scope(Perf::Cpu::NpcTrade);
        if (!IsMerchant(buyer) || buyer.health <= 0 || seller.hides <= 0 || buyer.coins < 6 ||
            !WorldLayout::InVillage(buyer.WorldPosition()))
        {
            return;
        }
        int count = (std::min)(seller.hides, buyer.coins / 6);
        seller.hides -= count;
        buyer.hides += count;
        seller.coins += count * 6;
        buyer.coins -= count * 6;
        Perf::Count(Perf::Counter::NpcTrades);
    }

    void World::Disclose(Npc* speaker)
    {
        if (published)
        {
            return;
        }
        published = true;
        // Only witnesses at the market hear this; distant villages require a travelling witness.
        Position origin = speaker ? speaker->WorldPosition() : Site(2);
        for (Npc* npc : level.Scene().Actors<Npc>(ActorKind::Npc))
        {
            if (npc->health > 0 && Distance(npc->WorldPosition(), origin) < 6 &&
                level.ClearLine(npc->WorldPosition(), origin))
            {
                npc->knowledge |= Knowledge::Ledger | Knowledge::Published;
            }
        }
        if (!speaker)
        {
            trust[1] = Bounded(trust[1] + 12, -100, 100);
            trust[0] = Bounded(trust[0] - 10, -100, 100);
        }
        Event(
            (speaker ? speaker->name : L"\ub2f9\uc2e0") +
            std::wstring(
                L"\uc774 \uc7a5\ubd80 \ub0b4\uc6a9\uc744 \uc54c\ub838\ub2e4. \ucc45\uc784\uc744 \ubb3b\ub294 \ubaa9\uc18c\ub9ac\uc640 \uc774\ub984\uc774 \ub4dc\ub7ec\ub0a0\uae4c \ub450\ub824\uc6b4 \ubaa9\uc18c\ub9ac\uac00 \ub4a4\uc11e\uc778\ub2e4."));
        if (ledgerWet && !copied)
        {
            Event(
                L"\uc816\uc740 \uc7a5\ubd80\uc5d0 \ube48\uce78\uc774 \ub9ce\ub2e4. \uacf5\uac1c\ub41c \uc774\ub984\ub9cc\uc73c\ub85c \uadf8\ub0a0\uc758 \uc9c4\uc2e4\uc744 \ub2e8\uc815\ud560 \uc218 \uc5c6\ub2e4.");
        }
    }

    void World::OpenGate(Npc* actor)
    {
        gateOpen = true;
        flood = 0;
        // Opening the sluice endangers only records still physically in the drainage.
        if (!Holder(ledger) && Distance(level.Scene().Get(ledger).WorldPosition(), Site(0)) < 2)
        {
            ledgerWet = true;
            Event(
                L"\uc218\ubb38\uc774 \uc5f4\ub838\ub2e4. \uace8\ubaa9\uc758 \ubb3c\uc740 \ube60\uc84c\uc9c0\ub9cc, \ubc30\uc218\ub85c\uc5d0 \ub0a8\uc740 \uc7a5\ubd80\ub294 \uc816\uc5b4 \uae00\uc790\uac00 \ubc88\uc84c\ub2e4.");
        }
        else
        {
            Event(
                L"\uc218\ubb38\uc774 \uc5f4\ub838\ub2e4. \uace8\ubaa9\uc758 \ubb3c\uc740 \ube60\uc9c0\uace0, \ubd09\uc778\ub3fc \uc788\ub358 \uad6c\uc5ed\uc774 \ubaa8\uc2b5\uc744 \ub4dc\ub7ec\ub0c8\ub2e4.");
        }
        if (!actor)
        {
            Witness(level.Player().Id(), Site(1), 4, 2);
        }
        level.Scene().Get(sites[1]).model = "sluice_open";
        level.DrainFloodedLane();
        if (Npc* apprentice = Person(4))
        {
            apprentice->knowledge |= Knowledge::Plans;
            apprentice->line =
                L"\ub2f9\uc2e0\uc774 \ubb3c\uae38\uc744 \uc5f4\uc5c8\uad6c\ub098! \uc0ac\ub78c\ub4e4\uc774 \ub4dc\ub7ec\ub09c \uace8\ubaa9\uc73c\ub85c \ubb3c\uac74\uc744 \uc62e\uae30\uace0 \uc788\uc5b4.";
        }
        ReactToPlayer(
            L"\uc218\ubb38 \uac1c\ubc29 \u2192 \uc7a0\uae34 \uace8\ubaa9\uc774 \uc2e4\uc81c \uae38\ub85c \ubc14\ub00c\uace0 \uc8fc\ubbfc\uacfc \uc0c1\uc778\uc774 \uadf8\uacf3\uc73c\ub85c \uc774\ub3d9\ud55c\ub2e4.");
        UpdateQuest();
    }

    void World::Step(Npc& npc, float dt)
    {
        Position before = npc.WorldPosition();
        float budget = dt * (npc.action == Action::Raid ? 2.0f : npc.courier ? 3.6f : 1.65f);
        for (int step = 0; step < 8 && npc.waypoint < npc.path.size() && budget > 0; ++step)
        {
            Position current = npc.WorldPosition();
            Position target = npc.path[npc.waypoint];
            float d = Distance(current, target);
            if (d < .12f)
            {
                ++npc.waypoint;
            }
            else
            {
                float amount = (std::min)(d, budget);
                level.MoveActor(
                    npc, (target.x - current.x) / d * amount, (target.y - current.y) / d * amount);
                budget -= amount;
                if (Distance(npc.WorldPosition(), target) < .12f)
                {
                    ++npc.waypoint;
                }
                else
                {
                    break;
                }
            }
        }
        const Position after = npc.WorldPosition();
        float travelled = Distance(before, after);
        npc.moving = travelled > .0001f;
        if (npc.moving)
        {
            npc.visualDirection =
                CharacterPresentation::ScreenDirection(((after.x - before.x) - (after.y - before.y)) * 48,
                                                       ((after.x - before.x) + (after.y - before.y)) * 24);
            npc.phase += travelled * 5.5f;
            float screenDirection = (after.x - before.x) - (after.y - before.y);
            if (std::abs(screenDirection) > .0001f)
            {
                npc.mirrorModel = screenDirection < 0;
            }
        }
        npc.model =
            "citizen" + std::to_string(npc.appearance) + "_" +
            (npc.moving ? std::to_string(int(npc.phase * CharacterPresentation::WalkFrames / 6.2831853f) %
                                         CharacterPresentation::WalkFrames)
                        : "idle");
        npc.cooldown = (std::max)(0.0f, npc.cooldown - dt);
        npc.threatTime = (std::max)(0.0f, npc.threatTime - dt);
        npc.raidCooldown = (std::max)(0.0f, npc.raidCooldown - dt);
        npc.raidWindow = (std::max)(0.0f, npc.raidWindow - dt);
        if (npc.courier && Distance(after, WorldLayout::VillageMarket(npc.travelVillage)) < 1.2f)
        {
            npc.courierWait = (std::max)(0.0f, npc.courierWait - dt);
        }
        if (npc.action == Action::Raid && npc.raidWindow <= 0)
        {
            npc.action = Action::Return;
            npc.destination = npc.home;
            npc.path.clear();
            npc.thinkTime = 0;
        }
        bool arrived = Distance(npc.WorldPosition(), npc.destination) < .8f;
        if ((npc.action == Action::Raid || npc.action == Action::Defend) && npc.target)
        {
            Actor* target = level.Scene().Find(npc.target);
            bool permitted =
                target && (npc.action == Action::Raid ? RaidAllowed(npc, *target)
                                                      : !(npc.job == Job::Raider &&
                                                          WorldLayout::InVillage(target->WorldPosition())));
            if (permitted && npc.cooldown <= 0 &&
                Distance(npc.WorldPosition(), target->WorldPosition()) < 1.1f &&
                level.ClearLine(npc.WorldPosition(), target->WorldPosition()))
            {
                npc.cooldown = 1.3f;
                Npc* victim = dynamic_cast<Npc*>(target);
                int stolen = 0;
                if (victim && victim->health > 0)
                {
                    if (npc.action == Action::Raid)
                    {
                        stolen = (std::min)(victim->coins, 5);
                        victim->coins -= stolen;
                        npc.coins += stolen;
                    }
                    Hurt(*victim, 13 + npc.experience / 60, npc.Id());
                    if (victim->health <= 0 && npc.action == Action::Raid)
                    {
                        npc.coins += victim->coins;
                        stolen += victim->coins;
                        victim->coins = 0;
                        npc.hides += victim->hides;
                        victim->hides = 0;
                    }
                }
                if (victim && victim->health <= 0 && npc.job == Job::Guard && !Holder(ledger) &&
                    Distance(npc.WorldPosition(), level.Scene().Get(ledger).WorldPosition()) < 1.5f)
                {
                    Transfer(ledger, npc.Id());
                    npc.knowledge |= Knowledge::Ledger;
                    npc.thinkTime = 0;
                }
                else if (target->Id() == level.Player().Id())
                {
                    if (npc.action == Action::Raid)
                    {
                        stolen = (std::min)(level.stats.coins, 5);
                        level.stats.coins -= stolen;
                        npc.coins += stolen;
                    }
                    level.DamagePlayer(14);
                }
                Perf::Count(Perf::Counter::NpcRaidCoins, stolen);
                if (npc.action == Action::Raid && ++npc.raidHits >= 2)
                {
                    npc.raidWindow = 0;
                    npc.threatTime = 0;
                    npc.thinkTime = 0;
                    npc.action = Action::Return;
                    npc.destination = npc.home;
                    npc.path.clear();
                }
            }
        }
        else if (npc.action == Action::Hunt && npc.target)
        {
            auto* beast = dynamic_cast<Level::Enemy*>(level.Scene().Find(npc.target));
            if (beast && beast->health > 0 && npc.cooldown <= 0 &&
                Distance(npc.WorldPosition(), beast->WorldPosition()) < 1.2f &&
                level.ClearLine(npc.WorldPosition(), beast->WorldPosition()))
            {
                Perf::Scope combat(Perf::Cpu::NpcCombat);
                npc.cooldown = 1.0f;
                Perf::Count(Perf::Counter::NpcCombatHits);
                if (level.DamageEnemy(*beast, 20 + npc.experience / 50, false))
                {
                    npc.hides += 2 + beast->kind;
                    ++npc.food;
                    npc.experience += 20;
                    if (npc.experience % 60 == 0)
                    {
                        npc.maxHealth += 5;
                    }
                    npc.thinkTime = 0;
                }
                else
                {
                    level.BeginEnemyAttack(*beast, npc.Id());
                }
            }
        }
        if (!arrived || npc.health <= 0)
        {
            return;
        }
        npc.actionTime += dt;
        if (npc.action == Action::Trade && npc.cooldown <= 0)
        {
            auto* merchant = dynamic_cast<Npc*>(level.Scene().Find(npc.target));
            if (merchant && Distance(npc.WorldPosition(), merchant->WorldPosition()) < 1.5f)
            {
                Exchange(npc, *merchant);
            }
            npc.cooldown = 3;
            npc.thinkTime = 0;
        }
        if (npc.action == Action::Rest && npc.actionTime > 2 && npc.food > 0 && npc.health < npc.maxHealth)
        {
            --npc.food;
            npc.health = (std::min)(npc.maxHealth, npc.health + 30);
            npc.actionTime = 0;
        }
        if (npc.action == Action::Study && npc.actionTime > 4)
        {
            ++npc.enlightenment;
            npc.actionTime = 0;
            if (npc.job == Job::Apprentice)
            {
                npc.knowledge |= Knowledge::Plans;
            }
        }
        // Exchanging hides for provisions replenishes stock without creating currency.
        if (IsMerchant(npc) && npc.hides >= 2 && npc.food < 8 && npc.cooldown <= 0 &&
            Distance(npc.WorldPosition(), npc.home) < 1.0f)
        {
            npc.hides -= 2;
            ++npc.food;
            npc.cooldown = 4;
        }
        if (npc.job != Job::Raider && npc.health < npc.maxHealth / 2 && npc.food == 0 && npc.coins >= 8)
        {
            Perf::Scope tradeScope(Perf::Cpu::NpcTrade);
            for (Npc* merchant : level.Scene().Actors<Npc>(ActorKind::Npc))
            {
                if (merchant != &npc && IsMerchant(*merchant) && merchant->health > 0 && merchant->food > 0 &&
                    Distance(merchant->WorldPosition(), Site(2)) < WorldLayout::MarketRadius &&
                    Distance(merchant->WorldPosition(), npc.WorldPosition()) < 1.5f)
                {
                    npc.coins -= 8;
                    merchant->coins += 8;
                    --merchant->food;
                    ++npc.food;
                    Perf::Count(Perf::Counter::NpcTrades);
                    npc.thinkTime = 0;
                    break;
                }
            }
        }
        if (npc.action != Action::Investigate)
        {
            return;
        }
        // NPCs can implement an explicit request, but cannot take the player's evidence or choose for them.
        if (npc.variant == 0 && briefed && !repaired && Distance(npc.WorldPosition(), Site(0)) < 1.2f)
        {
            repaired = true;
            Actor& support = level.Scene().Create(worldRoot, "PlayerRequestedDrainRepair", ActorKind::Prop);
            support.SetWorldPosition({Site(0).x - .6f, Site(0).y, 0});
            support.model = "prop3";
            support.drawable = true;
            support.shadowWidth = 40;
            Event(
                L"\ubbf8\ub77c\uac00 \ub2f9\uc2e0\uc758 \ubd80\ud0c1\uc73c\ub85c \ubc30\uc218\ub85c\uc5d0 \uc9c0\uc9c0\ub300\ub97c \uc138\uc6e0\ub2e4. \uce68\uc218 \uc18d\ub3c4\uac00 \uc904\uc5c8\ub2e4.");
        }
        if (npc.variant == 2 && recordChoice == RecordChoice::Authority && !controlled &&
            Distance(npc.WorldPosition(), Site(0)) < 1.2f)
        {
            controlled = true;
            Actor& cordon = level.Scene().Get(barrier);
            cordon.SetVisible(true);
            cordon.SetBlocksMovement(true);
            if (!level.AllWalkableConnected())
            {
                cordon.SetBlocksMovement(false);
            }
            Event(
                L"\uc138\ub77c\ud2b8\uac00 \ub118\uaca8\ubc1b\uc740 \uc7a5\ubd80\ub97c \ub4e4\uace0 \ubc30\uc218\ub85c\ub97c \ud1b5\uc81c\ud55c\ub2e4. \uc8fc\ubbfc\ub4e4\uc740 \ubb3c\ub7ec\ub098\uace0 \uc6b0\ud68c\ub85c\ub294 \ub0a8\ub294\ub2e4.");
        }
        if (npc.variant == 3 && recordChoice == RecordChoice::Sold && !dealSettled &&
            Distance(npc.WorldPosition(), Site(3)) < 1.2f)
        {
            dealSettled = true;
            Event(
                L"\uc774\uc548\uc774 \ubd80\ub450\uc5d0 \ub3c4\ucc29\ud588\ub2e4. \uc7a5\ubd80\ub294 \uc0ac\uc801\uc778 \uac70\ub798\uc5d0 \ubb36\uc600\uace0, \uac00\uc871\ub4e4\uc5d0\uac8c \uc774\ub984\uc740 \uc54c\ub824\uc9c0\uc9c0 \uc54a\uc558\ub2e4.");
        }
        if (Holder(key) == npc.Id() && !gateOpen && Distance(npc.WorldPosition(), Site(1)) < 1.2f)
        {
            OpenGate(&npc);
        }
    }

    void World::SpreadRumors(const std::vector<Npc*>& npcs)
    {
        Perf::Scope scope(Perf::Cpu::RumorUpdate);
        using Cell = std::pair<int, int>;
        std::map<Cell, std::vector<size_t>> neighbours;
        std::vector<int> known;
        known.reserve(npcs.size());
        for (size_t i = 0; i < npcs.size(); ++i)
        {
            const Npc& npc = *npcs[i];
            known.push_back(npc.knowledge);
            if (npc.health > 0)
            {
                Position p = npc.WorldPosition();
                neighbours[{int(std::floor(p.x / 4)), int(std::floor(p.y / 4))}].push_back(i);
            }
        }
        for (size_t i = 0; i < npcs.size(); ++i)
        {
            const Npc& source = *npcs[i];
            if (source.health <= 0 || !known[i])
            {
                continue;
            }
            Position p = source.WorldPosition();
            int cx = int(std::floor(p.x / 4)), cy = int(std::floor(p.y / 4));
            for (int y = -1; y <= 1; ++y)
            {
                for (int x = -1; x <= 1; ++x)
                {
                    auto bucket = neighbours.find({cx + x, cy + y});
                    if (bucket == neighbours.end())
                    {
                        continue;
                    }
                    for (size_t target : bucket->second)
                    {
                        Npc& receiver = *npcs[target];
                        Perf::Count(Perf::Counter::RumorCandidates);
                        int learned = known[i] & ~receiver.knowledge;
                        if (target == i || !learned || Distance(p, receiver.WorldPosition()) > 1.6f ||
                            !level.ClearLine(p, receiver.WorldPosition()))
                        {
                            continue;
                        }
                        receiver.knowledge |= learned;
                        if (learned & Knowledge::Ledger)
                        {
                            receiver.lastLedger = source.lastLedger;
                            receiver.knownLedgerHolder = source.knownLedgerHolder;
                        }
                        receiver.lastRumorSender = source.Id();
                        receiver.lastRumorMinute = minutes;
                        ++receiver.rumorsReceived;
                        int village = SettlementAt(receiver.WorldPosition());
                        if (village >= 0)
                        {
                            ++villageStatistics[size_t(village)].rumorDeliveries;
                        }
                        Perf::Count(Perf::Counter::RumorTransfers);
                    }
                }
            }
        }
    }

    int World::VillageMood(int village, const std::vector<const Npc*>& npcs) const
    {
        int present = 0, conflict = 0, wounded = 0, trading = 0, news = 0;
        for (const Npc* npc : npcs)
        {
            if (npc->health <= 0 || SettlementAt(npc->WorldPosition()) != village)
            {
                continue;
            }
            ++present;
            conflict += npc->action == Action::Raid || npc->action == Action::Defend ? 1 : 0;
            wounded += npc->health < npc->maxHealth / 2 ? 1 : 0;
            trading += npc->action == Action::Trade ? 1 : 0;
            news += (npc->knowledge & Knowledge::Published) ? 1 : 0;
        }
        return conflict                      ? 0
               : wounded                     ? 1
               : village == 0 && flood >= 25 ? 2
               : village == 0 && controlled  ? 3
               : news >= 3                   ? 4
               : trading >= 3                ? 5
               : present                     ? 6
                                             : 7;
    }

    void World::UpdateVillageStatistics(float dt, const std::vector<Npc*>& npcs)
    {
        statisticsTime += dt;
        if (statisticsTime < 1)
        {
            return;
        }
        Perf::Scope scope(Perf::Cpu::VillageStatistics);
        const std::vector<const Npc*> observers(npcs.begin(), npcs.end());
        for (int village = 0; village < WorldLayout::VillageCount; ++village)
        {
            auto& stats = villageStatistics[size_t(village)];
            stats.moodSeconds[size_t(VillageMood(village, observers))] += statisticsTime;
            stats.observedSeconds += statisticsTime;
            Perf::Count(Perf::Counter::VillageMoodSamples);
        }
        statisticsTime = 0;
    }

    void World::Update(float dt)
    {
        Perf::Scope scope(Perf::Cpu::NpcUpdate);
        for (const auto& impact : level.ConsumeEnemyAttackImpacts())
        {
            auto* victim = dynamic_cast<Npc*>(level.Scene().Find(impact.target));
            if (victim && victim->health > 0)
            {
                Hurt(*victim, impact.damage, impact.source);
                // Beasts are not human raid targets; the next hunt/flee decision remains authoritative.
                victim->threatTime = 0;
                Perf::Count(Perf::Counter::EnemyAttackHits);
            }
            else
            {
                Perf::Count(Perf::Counter::EnemyAttackMisses);
            }
        }
        UpdateQuest();
        minutes += dt; // Daily life continues; story choices remain with the player.
        auto npcs = level.Scene().Actors<Npc>(ActorKind::Npc);
        auto beasts = level.Scene().Actors<Level::Enemy>(ActorKind::Enemy);
        for (Npc* npc : npcs)
        {
            if (npc->health <= 0)
            {
                continue;
            }
            Perf::Count(Perf::Counter::NpcsUpdated);
            npc->thinkTime -= dt;
            if (npc->thinkTime <= 0)
            {
                Think(*npc, npcs, beasts);
                npc->thinkTime = 1.2f + float(npc->variant % 4) * .08f;
            }
            Step(*npc, dt);
        }
        {
            Perf::Scope story(Perf::Cpu::StoryUpdate);
            flood = gateOpen ? 0 : (std::min)(100, int((minutes - 360) / (repaired ? 9 : 3)));
            gossipTime -= dt;
            if (gossipTime <= 0)
            {
                gossipTime = 2;
                SpreadRumors(npcs);
            }
        }
        UpdateVillageStatistics(dt, npcs);
        logTime -= dt;
        if (logTime <= 0)
        {
            logTime = 2;
            Log();
        }
    }

    void World::PlayerAttack()
    {
        for (Npc* npc : level.Scene().Actors<Npc>(ActorKind::Npc))
        {
            if (npc->health <= 0)
            {
                continue;
            }
            Position p = npc->WorldPosition(), hero = level.Player().WorldPosition();
            float sx = ((p.x - hero.x) - (p.y - hero.y)) * 48;
            float sy = ((p.x - hero.x) + (p.y - hero.y)) * 24;
            float d = std::hypot(sx, sy);
            if (d <= 78 && (d <= 12 || (sx * level.facingX + sy * level.facingY) / d >= .1f) &&
                level.ClearLine(hero, p))
            {
                Hurt(*npc, level.stats.Attack(), level.Player().Id());
            }
        }
    }

    Npc* World::Nearest(float radius)
    {
        Npc* result = nullptr;
        for (Npc* npc : level.Scene().Actors<Npc>(ActorKind::Npc))
        {
            float d = Distance(level.Player().WorldPosition(), npc->WorldPosition());
            if (d < radius && level.ClearLine(level.Player().WorldPosition(), npc->WorldPosition()))
            {
                result = npc;
                radius = d;
            }
        }
        return result;
    }

    Actor* World::NearbySite(float radius)
    {
        Position hero = level.Player().WorldPosition();
        for (ActorId artifact : {ledger, key})
        {
            Actor& item = level.Scene().Get(artifact);
            if (!Holder(artifact) && Distance(hero, item.WorldPosition()) < 1.5f &&
                level.ClearLine(hero, item.WorldPosition()))
            {
                return &item;
            }
        }
        for (ActorId id : sites)
        {
            Actor& site = level.Scene().Get(id);
            if (Distance(hero, site.WorldPosition()) < radius && level.ClearLine(hero, site.WorldPosition()))
            {
                return &site;
            }
        }
        return nullptr;
    }

    void World::EndDialogue()
    {
        dialogue = {};
    }

    void World::AdvanceDialogue()
    {
        Perf::Scope scope(Perf::Cpu::Interaction);
        if (!dialogue.Active() || !dialogue.pages[dialogue.page].choices.empty())
        {
            return;
        }
        if (++dialogue.page >= dialogue.pages.size())
        {
            EndDialogue();
        }
    }

    void World::CompleteFirstQuest()
    {
        if (returned || questStep != 5)
        {
            return;
        }
        returned = true;
        level.stats.GainExperience(120);
        level.stats.coins += 20;
        ReactToPlayer(
            L"\ubb3c\uae38\uc744 \uc5f0 \uc77c\uc744 \ubcf4\uace0\ud588\ub2e4. \ud488\uc0af 20\ub0e5\uacfc \uacbd\ud5d8\uce58 120\uc744 \ubc1b\uc558\ub2e4. \ub2e4\uc74c\uc740 \uc194\ubc14\ub78c \uc0ac\ub0e5\uafbc \uac70\uc810\uc744 \ucc3e\uc544\ubcf4\uc790.");
        UpdateQuest();
    }

    void World::ChooseDialogue(size_t choice)
    {
        Perf::Scope scope(Perf::Cpu::Interaction);
        if (!dialogue.Active() || choice >= dialogue.pages[dialogue.page].choices.size())
        {
            return;
        }
        Actor* source = level.Scene().Find(dialogue.source);
        Npc* person = dynamic_cast<Npc*>(source);
        if (!source || (person && person->health <= 0) ||
            Distance(level.Player().WorldPosition(), source->WorldPosition()) >= (person ? 1.5f : 1.8f) ||
            (person && !level.ClearLine(level.Player().WorldPosition(), source->WorldPosition())))
        {
            EndDialogue();
            return;
        }
        DialogueChoice selected = dialogue.pages[dialogue.page].choices[choice];
        if (selected.effect == DialogueEffect::AcceptQuest && !briefed)
        {
            briefed = true;
            ReactToPlayer(
                L"\uc7a5\ubd80\uc640 \uc218\ubb38 \uc5f4\uc1e0\ub97c \ucc3e\uc544\uc624\uae30\ub85c \uc57d\uc18d\ud588\ub2e4. \ubbf8\ub77c\ub294 \ubc30\uc218\ub85c\ub97c \ubc1b\uce5c\ub2e4. \uc7a5\ubd80\ub97c \ub204\uad6c\uc5d0\uac8c \ub118\uae38\uc9c0\ub294 \uc544\uc9c1 \uc815\ud558\uc9c0 \uc54a\uc558\ub2e4.");
            UpdateQuest();
        }
        else if (selected.effect == DialogueEffect::CompleteQuest)
        {
            CompleteFirstQuest();
        }
        else if (selected.effect == DialogueEffect::ShareKnowledge && person)
        {
            Npc& npc = *person;
            if (Holder(ledger) == level.Player().Id() || copied)
            {
                npc.knowledge |= Knowledge::Ledger;
                npc.lastLedger = level.Player().WorldPosition();
                npc.knownLedgerHolder =
                    Holder(ledger) == level.Player().Id() ? level.Player().Id() : InvalidActorId;
            }
            if (Holder(key) == level.Player().Id())
            {
                npc.knowledge |= Knowledge::Key;
            }
            Event(
                npc.name +
                    L"\uc5d0\uac8c \ub0b4\uac00 \ucc3e\uc740 \ubb3c\uac74 \uc774\uc57c\uae30\ub97c \uc804\ud588\ub2e4.",
                false);
        }
        if (selected.next < 0 || size_t(selected.next) >= dialogue.pages.size())
        {
            EndDialogue();
        }
        else
        {
            dialogue.page = size_t(selected.next);
        }
    }

    void World::BeginDialogue(Npc* npc)
    {
        Perf::Scope scope(Perf::Cpu::Interaction);
        EndDialogue();
        dialogue.source = npc ? npc->Id() : sites[2];
        std::wstring name = npc ? npc->name : L"\uc7a5\ud130 \uac8c\uc2dc\ud310";
        const auto add = [&](const std::wstring& speaker, const std::wstring& text)
        {
            dialogue.pages.push_back({speaker, text, {}});
        };
        const auto option = [&](size_t page, const std::wstring& text, int next, DialogueEffect effect)
        {
            dialogue.pages[page].choices.push_back({text, next, effect});
        };
        if (npc && npc->affinity < -20)
        {
            add(name,
                L"\uac70\uae30\uc11c \uba48\ucdb0. \ub2f9\uc2e0\uc774 \ud55c \uc9d3\uc744 \uc78a\uc740 \uc904 \uc54c\uc544? \uc9c0\uae08\uc740 \ub9d0 \uc11e\uace0 \uc2f6\uc9c0 \uc54a\uc544.");
            option(0, L"\uc54c\uaca0\uc5b4. \ubb3c\ub7ec\ub0a0\uac8c.", -1, DialogueEffect::None);
            return;
        }
        if ((!npc || npc->variant == 0) && !briefed)
        {
            add(name,
                npc ? L"\ubc1c\ubc11 \uc870\uc2ec\ud574. \ubc24\uc0c8 \ube44\uac00 \uc3df\uc544\uc838\uc11c \uad6c\uc2dc\uac00\uc9c0 \ubc30\uc218\ub85c\uac00 \ubb34\ub108\uc84c\uc5b4. \ube44\ub294 \uadf8\ucce4\ub294\ub370 \uc61b \uc218\ubb38\uc774 \uc7a0\uaca8 \uc788\uc5b4\uc11c \ub0ae\uc740 \uace8\ubaa9\uc73c\ub85c \ubb3c\uc774 \ub2e4\uc2dc \ubc00\ub824\ub4e4\uace0 \uc788\uc5b4."
                    : L"\uad6c\uc2dc\uac00\uc9c0 \ubc30\uc218\ub85c \ubd95\uad34. \uc7a0\uae34 \uace8\ubaa9\uc758 \ubb3c\uae38\uc744 \uc5f4 \uc0ac\ub78c\uc744 \uad6c\ud568. \ud604\uc7a5\uc5d0\uc11c \uc77c\ud558\ub294 \uae30\uc220\uc790 \ubbf8\ub77c\uc5d0\uac8c \ub3c4\uc6c0\uc744 \uc904 \uac83.");
            add(L"\ub098",
                L"\uadf8\ub7fc \uc218\ubb38\ubd80\ud130 \uc5f4\uba74 \ub418\uaca0\ub124. \uadf8\ub7f0\ub370 \uc65c \ub2e4\ub4e4 \uc7a5\ubd80 \uc774\uc57c\uae30\ub97c \ud558\uc9c0?");
            add(name,
                L"\ubb34\ub108\uc9c4 \ubcbd \uc548\uc5d0\uc11c \uc624\ub798\ub41c \ud654\ubb3c \uc0c1\uc790\uac00 \ub4dc\ub7ec\ub0ac\uac70\ub4e0. \uc5f4\ub450 \ud574 \uc804 \uac00\ub77c\uc549\uc740 \ubc00\uc218\uc120\uc758 \uc9d0\uc774\ub798. \uc61b \uc218\ubb38 \uc5f4\uc1e0\uc640 \uc7a5\ubd80\uac00 \uc788\ub2e4\ub294\ub370, \uc0ac\ub77c\uc9c4 \uc0ac\ub78c\ub4e4\uc758 \uc774\ub984\uc774 \uc801\ud600 \uc788\ub300.");
            add(name,
                L"\ub09c \uc5ec\uae30\uc11c \uc218\ub85c\ub97c \ubc1b\uccd0\uc57c \ud574. \ub300\uc2e0 \uc7a5\ubd80\uc640 \uc5f4\uc1e0\ub97c \ucc59\uaca8 \uc904\ub798? \uc7a5\ubd80\ub294 \ubb3c\uc744 \ube7c\uae30 \uc804\uc5d0 \uac74\uc838\uc57c \ud574. \uc5f4\uc1e0\ub85c \ubb3c\uae38\uae4c\uc9c0 \uc5f4\uc5b4 \uc8fc\uba74 \ud488\uc0af 20\ub0e5\uc744 \uc904\uac8c.");
            option(
                3,
                L"\ub9e1\uc744\uac8c. \ubc30\uc218\ub85c\uc5d0\uc11c \uc7a5\ubd80\uc640 \uc5f4\uc1e0\ub97c \ucc3e\uc544\uc62c\uac8c.",
                -1,
                DialogueEffect::AcceptQuest);
            option(3,
                   L"\uc7a5\ubd80\ub294 \ub204\uad6c\uc5d0\uac8c \ub118\uaca8\uc57c \ud558\ub294\ub370?",
                   4,
                   DialogueEffect::None);
            option(
                3,
                L"\uc544\uc9c1 \uc57d\uc18d\uc740 \ubabb \ud558\uaca0\uc5b4. \ub098\uc911\uc5d0 \uc62c\uac8c.",
                -1,
                DialogueEffect::None);
            add(name,
                L"\uadf8\uac74 \ub124\uac00 \ub0b4\uc6a9\uc744 \ubcf4\uace0 \uc815\ud574. \uac00\uc871\ub4e4\uc5d0\uac8c \uc54c\ub9ac\uc790\ub294 \uc0ac\ub78c\ub3c4, \uad00\uccad\uc5d0 \ub9e1\uae30\uc790\ub294 \uc0ac\ub78c\ub3c4 \uc788\uc5b4. \uac12\uc744 \uce58\ub974\uaca0\ub2e4\ub294 \uc911\uac1c\uc0c1\ub3c4 \uc788\uace0. \ub204\uad6c\ub97c \ubbff\ub4e0 \uc0ac\ubcf8\uc740 \ub0a8\uaca8 \ub46c.");
            option(
                4,
                L"\uc77c\ub2e8 \ub458 \ub2e4 \ucc3e\uc544\uc62c\uac8c. \ucc98\ubd84\uc740 \uadf8 \ub4a4\uc5d0 \uc815\ud558\uaca0\uc5b4.",
                -1,
                DialogueEffect::AcceptQuest);
            option(4, L"\uc0dd\uac01\ud560 \uc2dc\uac04\uc774 \ud544\uc694\ud574.", -1, DialogueEffect::None);
            return;
        }
        if ((!npc || npc->variant == 0) && questStep == 5)
        {
            add(name,
                npc ? L"\ubb3c \ube60\uc9c0\ub294 \uc18c\ub9ac \ub4e4\ub9ac\uc9c0? \uc544\uae4c\uae4c\uc9c0\ub9cc \ud574\ub3c4 \ubabb \uc9c0\ub098\uac00\ub358 \uace8\ubaa9\uc73c\ub85c \uc0ac\ub78c\ub4e4\uc774 \uc9d0\uc744 \uc62e\uae30\uace0 \uc788\uc5b4. \ub124\uac00 \uc5f4\uc5b4 \uc900 \uae38\uc774\uc57c."
                    : L"\ubc30\uc218\ub85c \ubcf5\uad6c \ud655\uc778. \uad6c\uc2dc\uac00\uc9c0 \uace8\ubaa9\uc758 \ud1b5\ud589\uc774 \ub2e4\uc2dc \uc5f4\ub838\ub2e4. \uc791\uc5c5\uc744 \ub9c8\uce5c \uc0ac\ub78c\uc740 \uc644\ub8cc \ubcf4\uace0 \ud6c4 \ud488\uc0af\uc744 \ubc1b\uc544 \uac08 \uac83.");
            add(L"\ub098",
                L"\uc5f4\uc1e0\ub85c \uc218\ubb38\uc744 \uc5f4\uc5c8\uc5b4. \uc7a5\ubd80\ub294\u2026 " +
                    Consequence());
            add(name,
                L"\uc0ac\ub78c\ub9c8\ub2e4 \ub124 \uacb0\uc815\uc744 \ub2e4\ub974\uac8c \ub9d0\ud558\uaca0\uc9c0. \uadf8\ub798\ub3c4 \uc624\ub298 \uc9d1\uc73c\ub85c \ub3cc\uc544\uac08 \uae38\uc774 \uc0dd\uae34 \uc0ac\ub78c\uc740 \uc788\uc5b4. \uc57d\uc18d\ud55c \ud488\uc0af 20\ub0e5\uc744 \ubc1b\uc544. \ubc14\uae65\uc5d0\uc11c \uc77c\uac10\uc744 \ucc3e\uc73c\ub824\uba74 \uc194\ubc14\ub78c \uc0ac\ub0e5\uafbc \uac70\uc810\uc5d0 \uac00 \ubd10.");
            option(
                2,
                L"\uc77c\uc744 \ub9c8\ucce4\uc5b4. \uc57d\uc18d\ud55c \ud488\uc0af\uc744 \ubc1b\uc744\uac8c. (20\ub0e5 / \uacbd\ud5d8\uce58 120)",
                -1,
                DialogueEffect::CompleteQuest);
            option(
                2,
                L"\uc544\uc9c1 \ub458\ub7ec\ubcfc \uacf3\uc774 \uc788\uc5b4. \ubcf4\uace0\ub294 \ub098\uc911\uc5d0 \ud560\uac8c.",
                -1,
                DialogueEffect::None);
            return;
        }
        add(name,
            npc ? npc->line
                : L"\uc7a5\ud130 \uac8c\uc2dc\ud310\uc758 \uae00\uc740 \ube57\ubb3c\uc5d0 \uc816\uc5b4 \uc788\ub2e4.");
        std::wstring context;
        std::wstring answer;
        if (npc && npc->variant == 0 && returned)
        {
            context =
                L"\uace8\ubaa9\uc73c\ub85c \uc9d0\uc744 \uc62e\uae30\ub294 \uc0ac\ub78c\ub4e4 \ubcf4\uc5ec? \uac01\uc790 \ub124 \uc774\uc57c\uae30\ub97c \ub2e4\ub974\uac8c \ud558\uaca0\uc9c0\ub9cc, \uc801\uc5b4\ub3c4 \uc624\ub298\uc740 \ub3cc\uc544\uac08 \uae38\uc774 \uc0dd\uacbc\uc5b4.";
            answer =
                L"\uc774\uc81c \ub124 \uc77c\ub3c4 \ubd10\uc57c\uc9c0. \ud070\uae38\uc744 \ub530\ub77c \uc194\ubc14\ub78c \uc0ac\ub0e5\uafbc \uac70\uc810\uc73c\ub85c \uac00 \ubd10. \uc232\uc5d0\uc11c \uba39\uace0\uc0ac\ub294 \uc0ac\ub78c\ub4e4\ud55c\ud14c \ubc14\uae65 \uc0ac\uc815\uc744 \ub4e4\uc744 \uc218 \uc788\uc744 \uac70\uc57c.";
        }
        else if (npc && npc->variant == 0)
        {
            context =
                L"\uc5f4\uc1e0\ub294 \uc218\ubb38\uc5d0, \uc7a5\ubd80\ub294 \uc0ac\ub78c\ub4e4\uc5d0\uac8c \ud544\uc694\ud55c \ubb3c\uac74\uc774\uc57c. \ub458\uc744 \ud55c\uaebc\ubc88\uc5d0 \ud574\uacb0\ud558\ub824\uace0 \uc11c\ub450\ub974\uc9c0 \ub9c8. \uc7a5\ubd80\ub97c \uac74\uc9c4 \ub4a4 \uac8c\uc2dc\ud310\uc5d0\uc11c \uc0ac\ubcf8\ubd80\ud130 \ub0a8\uaca8.";
            answer =
                gateOpen
                    ? L"\ubb3c\uae38\uc740 \uc5f4\ub838\uc5b4. \uc7a5\ubd80\ub97c \uc5b4\ub5bb\uac8c \ucc98\ub9ac\ud588\ub294\uc9c0\uae4c\uc9c0 \uc815\ub9ac\ub418\uba74 \ub2e4\uc2dc \uc640. \uc57d\uc18d\ud55c \ud488\uc0af\uc740 \ucc59\uaca8 \ub458\uac8c."
                    : L"\ubc30\uc218\ub85c \uc548\ucabd \ud654\ubb3c \uc0c1\uc790\ubd80\ud130 \uc0b4\ud3b4\ubd10. \uc7a5\ubd80\uc640 \uc5f4\uc1e0\ub97c \ud558\ub098\uc529 \ucc59\uae34 \ub2e4\uc74c \uc61b \uc218\ubb38\uc73c\ub85c \uac00\uba74 \ub3fc. \uc5f4\uc1e0\ub97c \ub9e1\uae38 \uc0dd\uac01\uc774\uba74 \ub098\ub098 \ud558\ub78c\uc5d0\uac8c \uba3c\uc800 \ub9d0\ud574.";
        }
        else if (npc && npc->variant == 1)
        {
            context =
                L"\ub0b4 \ub3d9\uc0dd\ub3c4 \uadf8\ub54c \uc0ac\ub77c\uc84c\uc5b4. \uad00\uccad \uae30\ub85d\uc5d0\ub294 \uc774\ub984 \uc606\uc5d0 \uc2e4\uc885\uc774\ub77c\uace0 \ud55c \uc904 \uc801\ud600 \uc788\ub354\ub77c. \uc5f4\ub450 \ud574\uac00 \uc9c0\ub0ac\ub294\ub370\ub3c4 \ub204\uac00 \ub9c8\uc9c0\ub9c9\uc73c\ub85c \ubd24\ub294\uc9c0\uc870\ucc28 \ubab0\ub77c.";
            answer =
                recordChoice == RecordChoice::Public
                    ? L"\ub124\uac00 \uc774\ub984\uc744 \uaebc\ub0b4 \uc900 \ub355\uc5d0 \uc11c\ub85c \ub9d0\uc744 \ub9de\ucdb0 \ubcfc \uc218 \uc788\uac8c \ub410\uc5b4. \uace0\ub9d9\ub2e4\ub294 \uc0ac\ub78c\ub9cc \uc788\ub294 \uac74 \uc544\ub2c8\uc57c. \uc544\uc9c1 \uc228\uace0 \uc2f6\uc740 \uac00\uc871\ub3c4 \uc788\uac70\ub4e0."
                    : L"\ucc3e\uac8c \ub418\uba74 \uc0ac\ubcf8\uc744 \ub0a8\uaca8 \uc918. \uc6d0\ubcf8\uc744 \uc5b4\ub514\uc5d0 \ub9e1\uae30\ub4e0 \uc774\ub984\uae4c\uc9c0 \uc0ac\ub77c\uc9c0\uac8c \ud558\uc9c4 \ub9d0\uc544 \uc918. \uadf8\ub807\ub2e4\uace0 \uc900\ube44\ub3c4 \uc5c6\uc774 \uc0ac\ub78c\ub4e4 \uc55e\uc5d0\uc11c \uc77d\ub294 \uac74\u2026 \uac81\uc774 \ub098.";
        }
        else if (npc && npc->variant == 2)
        {
            context =
                L"\ub204\uac00 \ub204\uad6c\ub97c \ubc84\ub838\ub2e4\ub294 \ub9d0\uc774 \uc7a5\ud130\uc5d0 \ub3cc\uace0 \uc788\uc18c. \uc544\uc9c1 \ud655\uc778\ub3c4 \uc548 \ub41c \ub9d0 \ub54c\ubb38\uc5d0 \uc774\uc6c3\ub07c\ub9ac \uc0bf\ub300\uc9c8\uc744 \ud574. \ub098\ub294 \uadf8\uac78 \ub9c9\uc73c\ub7ec \uc654\uc18c.";
            answer =
                controlled
                    ? L"\uc7a5\ubd80\ub294 \uc811\uc218\ud588\uc18c. \ud604\uc7a5\uc740 \ub2f9\ubd84\uac04 \ud1b5\uc81c\ud560 \uac70\uc694. \uac00\uc871\ub4e4\uc774 \ub2f5\ub2f5\ud574\ud558\ub294 \uac74 \uc54c\uc9c0\ub9cc, \uc870\uc0ac \uc804\uc5d0 \ud6fc\uc190\ub418\ub294 \uc99d\uac70\ubd80\ud130 \ub9c9\uc544\uc57c \ud558\uc624."
                    : L"\uad00\uccad\uc5d0 \uc6d0\ubcf8\uc744 \ub9e1\uae30\uba74 \ub300\uac00\ub85c 30\ub0e5\uc744 \uc9c0\uae09\ud558\uaca0\uc18c. \ub300\uc2e0 \ud604\uc7a5 \ucd9c\uc785\uc740 \uc81c\ud55c\ub420 \uac70\uc694. \uc548\uc815\uc774 \ud544\uc694\ud558\ub2e4\ub294 \ub0b4 \ub9d0\uacfc, \uac00\uc871\ub4e4\uc758 \uc54c \uad8c\ub9ac \uc0ac\uc774\uc5d0\uc11c \ud310\ub2e8\ud558\uc2dc\uc624. \uacb0\uc815\ud588\ub2e4\uba74 \uc7a5\ud130\uc5d0\uc11c \ub0b4 \uacc1\uc5d0 \uc640 T\ub85c \ub118\uae30\uc2dc\uc624.";
        }
        else if (npc && npc->variant == 3)
        {
            context =
                L"\uc57d\uac12\uc774 \ub610 \uc62c\ub790\ub354\ub77c. \uc9c4\uc2e4\uc744 \uc548\ub2e4\uace0 \ubc25\uc774 \uc0dd\uae30\ub294 \uac74 \uc544\ub2c8\uc796\uc544. \uadf8\ub807\ub2e4\uace0 \ubaa8\ub978 \ucc99\ud558\ub294 \uac12\uc774 \uc2fc \uac83\ub3c4 \uc544\ub2c8\uace0.";
            answer =
                recordChoice == RecordChoice::Sold
                    ? L"\uc6d0\ubcf8\uc740 \ub0b4\uac00 \uac00\uc838\uac14\uc9c0. \uc57d\uc18d\ud55c \ub3c8\ub3c4 \uc92c\uace0. \uadf8 \uc774\ub984\ub4e4\uc774 \uc5b4\ub514\ub85c \ud758\ub7ec\uac08\uc9c0\ub294\u2026 \uac70\ub798\uac00 \ub05d\ub09c \ub4a4\uae4c\uc9c0 \ubcf4\uc7a5\ud55c \uac74 \uc544\ub2c8\uc57c."
                    : L"\uc6d0\ubcf8\uc774\uba74 40\ub0e5. \uad00\uccad\ubcf4\ub2e4 \ub354 \uccd0\uc904\uac8c. \ub2e4\ub9cc \uadf8 \ub4a4\uc5d0 \ub204\uad6c \uc190\uc5d0 \ub4e4\uc5b4\uac08\uc9c0\ub294 \ubb3b\uc9c0 \ub9c8. \ud314 \uc0dd\uac01\uc774 \uc815\ud574\uc9c0\uba74 \uc7a5\ud130\uc5d0\uc11c \ub0b4 \uacc1\uc5d0 \uc640 T\ub85c \uac70\ub798\ud574.";
        }
        else if (npc && npc->variant == 4)
        {
            context =
                L"\uc6b0\ub9ac \uc9d1\ub3c4 \uc800 \ub0ae\uc740 \uace8\ubaa9\uc5d0 \uc788\uc5b4. \uc5b4\uba38\ub2c8\uac00 \ubb3c \ub4e4\uae30 \uc804\uc5d0 \uc300\ub3c5\ubd80\ud130 \uc62e\uae30\ub7ac\ub294\ub370, \uc218\ubb38\ub9cc \uc5f4\uba74 \uad1c\ucc2e\uc744 \uc904 \uc54c\uc558\uc5b4. \uc5f4\uc1e0\uac00 \uc5c6\uc5b4\uc11c \uc544\ubb34\uac83\ub3c4 \ubabb \ud588\uc9c0\ub9cc.";
            answer =
                gateOpen
                    ? L"\uc774\uc81c \uc9d1\uc5d0 \uac08 \uc218 \uc788\uc5b4. \uae38\uc774 \uc5f4\ub9b0 \uac78 \ubcf4\ub2c8\uae4c, \uc218\ubb38\uc744 \uace0\uce58\ub294 \uc77c\uc774 \uadf8\ub0e5 \uc1f3\ub369\uc774 \ub9cc\uc9c0\ub294 \uac74 \uc544\ub2c8\ub77c\ub294 \uc0dd\uac01\uc774 \ub4e4\uc5b4."
                    : L"\uc5f4\uc1e0\uac00 \uc788\uc73c\uba74 \uc9c1\uc811 \uc5f4\uc5b4\ub3c4 \ub3fc. \ubb34\uc12d\uac70\ub098 \ub2e4\ub978 \uc77c\uc744 \ubd10\uc57c \ud558\uba74 \uc7a5\ud130\uc5d0\uc11c \ub0b4 \uacc1\uc5d0 \uc640 T\ub85c \ub9e1\uaca8 \uc918. \uc7a5\ubd80\ub294 \ubb3c\uc5d0 \uc4f8\ub9ac\uae30 \uc804\uc5d0 \uba3c\uc800 \ucc59\uae30\uace0.";
        }
        else if (npc && npc->job == Job::Hunter)
        {
            context =
                L"\uc0ac\ub0e5\uc740 \ucde8\ubbf8\uac00 \uc544\ub2c8\uc57c. \uac00\uc8fd\uc744 \ud314\uc544\uc57c \uc57d\ub3c4 \uc0ac\uace0 \uc300\ub3c4 \uc0ac\uc9c0. \uc694\uc0cc \uc9d0\uc2b9\ubcf4\ub2e4 \uc232\uae38\uc5d0\uc11c \ub9cc\ub098\ub294 \uc0ac\ub78c\uc774 \ub354 \ubb34\uc11c\uc6b8 \ub54c\ub3c4 \uc788\uc5b4.";
            answer =
                L"\ub9c8\uc744 \ubc16 \uc194\ubc14\ub78c \uac70\uc810\ubd80\ud130 \ucc3e\uc544\ubd10. \ud070\uae38\uc744 \ub193\uce58\uc9c0 \ub9d0\uace0. \uac00\uc8fd\uc740 \uc7a5\ud130 \uc0c1\uc778\uc5d0\uac8c \ud314 \uc218 \uc788\uc5b4. \ub2e4\ucce4\uc73c\uba74 \uc57d\ubd80\ud130 \ucc59\uaca8. \uc8fd\uc73c\uba74 \ud488\uc0af\uc774\uace0 \ubb50\uace0 \uc5c6\uc73c\ub2c8\uae4c.";
        }
        else if (npc && (npc->job == Job::Merchant || npc->job == Job::Broker))
        {
            context =
                L"\uc5ed\ubcd1 \ub3cc\uace0 \uae38\uc774 \ub9c9\ud600\uc11c \ubb3c\uac74 \uad6c\ud558\ub294 \uac83\ub3c4 \uc77c\uc774\uc57c. \uac12\uc774 \uc62c\ub790\ub2e4\uace0 \ub098\ub9cc \ud0d3\ud558\uc9c0\ub294 \ub9c8. \ub098\ub3c4 \uc624\ub298 \ud314\uc544\uc57c \ub0b4\uc77c \uc7a5\uc744 \uc5f4\uc9c0.";
            answer =
                L"\uc7a5\ud130\uc5d0\uc11c T\ub85c \uac70\ub798\ud560 \uc218 \uc788\uc5b4. \uac00\uc8fd\uc740 6\ub0e5\uc5d0 \uc0ac\uace0, \ud68c\ubcf5\uc57d\uc740 8\ub0e5\uc774\uc57c. \uadf8\ub0e5 \uc774\uc57c\uae30\ub9cc \ud588\ub2e4\uace0 \ubb3c\uac74\uc744 \uac00\uc838\uac00\uc9c4 \uc54a\uc73c\ub2c8 \ud544\uc694\ud55c \uac78 \uc0dd\uac01\ud574 \ubd10.";
        }
        else if (npc && npc->job == Job::Raider)
        {
            context =
                L"\ub9c8\uc744 \uc0ac\ub78c\ub4e4\uc740 \uc6b0\ub9ac\uac00 \uc232\uc5d0\uc11c \ubb58 \uba39\uace0 \uc0ac\ub294\uc9c0 \uad00\uc2ec\ub3c4 \uc5c6\uc9c0. \uadf8\ub807\ub2e4\uace0 \uc6b0\ub9ac\uac00 \ud558\ub294 \uc77c\uc774 \ub5b3\ub5b3\ud558\ub2e4\ub294 \uac74 \uc544\ub2c8\uc57c.";
            answer =
                L"\uc774 \uadfc\ucc98\uc5d0\uc120 \uc8fc\uba38\ub2c8\ub97c \uc798 \uac04\uc218\ud574. \uad81\ud574\uc11c \ube7c\uc557\ub294 \uc0ac\ub78c\ub3c4, \uadf8\ub0e5 \uc26c\uc6cc\uc11c \ube7c\uc557\ub294 \uc0ac\ub78c\ub3c4 \uc788\uc73c\ub2c8\uae4c.";
        }
        else if (npc && npc->homeVillage != 0)
        {
            context =
                (npc->knowledge & Knowledge::Published)
                    ? L"\ubb34\uc5f0\ud56d\uc5d0\uc11c \uc61b \uc7a5\ubd80\ub97c \uacf5\uac1c\ud588\ub2e4\uba70? \uc5ec\uae30\uae4c\uc9c0 \uc628 \uc0ac\ub78c\uc774 \uc804\ud574 \uc92c\uc5b4. \ub204\uad6c\ub294 \uc9c4\uc2e4\uc744 \ubc18\uae30\uace0 \ub204\uad6c\ub294 \uc774\ub984\uc774 \ub4dc\ub7ec\ub0a0\uae4c \uac81\ub0b8\ub2e4\ub354\ub77c."
                    : L"\ubb34\uc5f0\ud56d\uc758 \uc790\uc138\ud55c \uc0ac\uc815\uc740 \uc544\uc9c1 \ubabb \ub4e4\uc5c8\uc5b4. \uae38\uc744 \uac74\ub108\uc628 \uc0ac\ub78c\uc774 \uc788\uc5b4\uc57c \uc18c\uc2dd\ub3c4 \ub2ff\uc9c0. \uc5ec\uae30\uc120 \uc7a5\uc0ac\ud558\uace0 \uc544\ud508 \uc2dd\uad6c \ucc59\uae30\uae30\ub3c4 \ubc14\ube60.";
            answer =
                npc->courier
                    ? L"\uc774\uc6c3 \uc7a5\ud130\uae4c\uc9c0 \uc9c1\uc811 \uac78\uc5b4\uac00. \ub4e4\uc740 \uc774\uc57c\uae30\ub3c4 \uac70\uae30\uc11c \ub9cc\ub09c \uc0ac\ub78c\ud55c\ud14c\ub9cc \uc804\ud558\uc9c0. \ub0b4\uac00 \ub3cc\uc544\uc624\uae30 \uc804\uc5d0 \uc77c\uc774 \ub2ec\ub77c\uc84c\uc744 \uc218\ub3c4 \uc788\uc5b4."
                    : L"\ud070\uae38\uc5d0 \uc9d0\uc2b9\uc774 \ub9ce\uc544\uc84c\uc5b4. \uc7a5\uc0ac\uafbc\ub4e4\uc774 \uc624\uac00\uc57c \uba39\uc744\uac70\ub9ac\ub3c4 \uc18c\uc2dd\ub3c4 \ub4e4\uc5b4\uc624\ub294\ub370, \ud63c\uc790 \ub5a0\ub098\uae30\ub294 \uac81\ub098\ub354\ub77c.";
        }
        else if (npc && npc->job == Job::Scholar)
        {
            context =
                L"\uc5ed\ubcd1\ubcf4\ub2e4 \uba3c\uc800 \ud37c\uc9c0\ub294 \uac8c \uc18c\ubb38\uc774\uc9c0. \ub204\uac00 \uc4f4 \uae30\ub85d\uc778\uc9c0, \ub204\uac00 \uc9c0\uc6e0\ub294\uc9c0\ub3c4 \ubaa8\ub974\uba74\uc11c \uc885\uc774\uc5d0 \uc801\ud614\ub2e4\ub294 \uc774\uc720\ub85c \ubbff\uac8c \ub418\ub2c8\uae4c.";
            answer =
                L"\uc7a5\ubd80\uc758 \uc774\ub984\uc774 \uc9c4\uc9dc\ub77c\uace0 \ud574\ub3c4 \uadf8 \uc774\ub984\uc744 \uc54c\ub9b4 \uad8c\ub9ac\uac00 \ub124\uac8c \uc788\ub294\uc9c0\ub294 \ub2e4\ub978 \ubb38\uc81c\uc57c. \uadf8\ub798\ub3c4 \uac10\ucd94\ub294 \uac8c \ub298 \uc870\uc2ec\uc2a4\ub7ec\uc6b4 \uc120\ud0dd\uc778 \uac74 \uc544\ub2c8\uaca0\uc9c0.";
        }
        else
        {
            context =
                L"\ube44\uac00 \uadf8\ucce4\ub2e4\uace0 \uc0b4\ub9bc\uc774 \uc800\uc808\ub85c \ub9c8\ub974\ub294 \uac74 \uc544\ub2c8\ub354\ub77c. \uc816\uc740 \uc774\ubd88 \ub110\uace0, \uc300 \ucc59\uae30\uace0\u2026 \uc544\ud508 \uc2dd\uad6c\uac00 \uc788\uc73c\uba74 \ubb3c \ud55c \ubc14\uac00\uc9c0\ub3c4 \uc544\uc26c\uc6cc.";
            answer =
                gateOpen
                    ? L"\uc0c8\ub85c \uc5f4\ub9b0 \uace8\ubaa9\uc73c\ub85c \ub3cc\uc544\uac00\ub2c8\uae4c \ud6e8\uc52c \ube68\ub77c. \ub124\uac00 \ubb58 \uacb0\uc815\ud588\ub294\uc9c0\ub294 \ub2e4 \ubab0\ub77c\ub3c4, \uc624\ub298 \uae38\uc744 \uc4f8 \uc218 \uc788\uac8c \ub41c \uac74 \uc54c\uc544."
                    : L"\ubbf8\ub77c\uac00 \uc218\ub85c\ub97c \ubd99\uc7a1\uace0 \uc788\uc5b4. \ubb3c\uae38\uc774 \uc5f4\ub9ac\uba74 \uc9d1\uc73c\ub85c \ub3cc\uc544\uac08 \uc218 \uc788\uc744 \ud150\ub370. \ub098\ub294 \uc7a5\ubd80\ubcf4\ub2e4 \uc624\ub298 \uc800\ub141\uc774 \uba3c\uc800 \uac71\uc815\uc774\uc57c.";
        }
        add(name, context);
        option(1, L"\uc870\uae08 \ub354 \ub4e4\ub824\uc918.", 2, DialogueEffect::None);
        const bool canShare =
            npc && (Holder(ledger) == level.Player().Id() || copied || Holder(key) == level.Player().Id());
        if (canShare)
        {
            option(
                1,
                L"\ub0b4\uac00 \ucc3e\uc740 \ubb3c\uac74 \uc774\uc57c\uae30\ub97c \ud574\uc904\uac8c. (\uc815\ubcf4 \uc804\ub2ec)",
                3,
                DialogueEffect::ShareKnowledge);
        }
        option(1, L"\uc774\uc57c\uae30 \uace0\ub9c8\uc6cc. \uac00 \ubcfc\uac8c.", -1, DialogueEffect::None);
        add(name, answer);
        option(2,
               L"\uc54c\uaca0\uc5b4. \uc5b4\ub5bb\uac8c \ud560\uc9c0\ub294 \ub0b4\uac00 \uc815\ud560\uac8c.",
               -1,
               DialogueEffect::None);
        add(name,
            L"\ub124\uac00 \uc9c1\uc811 \ubd24\ub2e4\ub294 \uac70\uc9c0? \uadf8\uac74 \uae30\uc5b5\ud574 \ub458\uac8c. \ub2e4\ub978 \uc0ac\ub78c\uc5d0\uac8c \uc774\uc57c\uae30\ud558\uae30 \uc804\uc5d4 \ud55c \ubc88 \ub354 \uc0dd\uac01\ud574.");
        option(3, L"\uc5ec\uae30\uae4c\uc9c0 \uc774\uc57c\uae30\ud560\uac8c.", -1, DialogueEffect::None);
    }

    void World::Talk()
    {
        if (dialogue.Active())
        {
            return;
        }
        UpdateQuest();
        Npc* npc = Nearest();
        // Talking at the marked quest giver must not select a slightly closer bystander.
        if (questStep == 0 || questStep == 5)
        {
            Npc* questGiver = Person(0);
            if (questGiver && questGiver->health > 0 &&
                Distance(level.Player().WorldPosition(), questGiver->WorldPosition()) < 1.5f &&
                level.ClearLine(level.Player().WorldPosition(), questGiver->WorldPosition()))
            {
                npc = questGiver;
            }
        }
        if (!npc)
        {
            level.message =
                L"\ub9d0\uc744 \uac78\ub824\uba74 \uc870\uae08 \ub354 \uac00\uae4c\uc774 \uac00\uc790.";
        }
        else if (npc->health <= 0)
        {
            if (npc->coins == 0 && npc->hides == 0)
            {
                level.message =
                    L"\ucc59\uae38 \ub9cc\ud55c \uac83\uc740 \ub0a8\uc544 \uc788\uc9c0 \uc54a\ub2e4.";
                level.messageTime = 5;
                return;
            }
            level.stats.coins += npc->coins;
            level.stats.hides += npc->hides;
            npc->coins = npc->hides = 0;
            Witness(level.Player().Id(), npc->WorldPosition(), -4, -1);
            level.message =
                L"\ub0a8\uaca8\uc9c4 \uc8fc\uba38\ub2c8\ub97c \ucc59\uacbc\ub2e4. \uc774 \uad11\uacbd\uc744 \ubcf8 \uc0ac\ub78c\uc740 \uae30\uc5b5\ud560 \uac83\uc774\ub2e4.";
        }
        else
        {
            BeginDialogue(npc);
            return;
        }
        level.messageTime = 8;
    }

    void World::Trade()
    {
        Perf::Scope scope(Perf::Cpu::NpcTrade);
        Npc* npc = Nearest();
        if (!npc || npc->health <= 0 || npc->affinity < -20 || npc->threatTime > 0 ||
            Distance(npc->WorldPosition(), Site(2)) > WorldLayout::MarketRadius)
        {
            Event(
                L"\uac70\ub798\ud558\ub824\uba74 \uc7a5\ud130 \uadfc\ucc98\uc758 \uc0b4\uc544 \uc788\ub294 \uc8fc\ubbfc\uc5d0\uac8c \ub2e4\uac00\uac00\uc790. \uc801\ub300\uc801\uc778 \uc0c1\ub300\ub294 \uac70\ub798\ud558\uc9c0 \uc54a\ub294\ub2e4.");
            return;
        }
        if (Holder(ledger) == npc->Id() && IsMerchant(*npc) && level.stats.coins >= 50)
        {
            level.stats.coins -= 50;
            npc->coins += 50;
            Transfer(ledger, level.Player().Id());
            Event(
                L"\uc270 \ub0e5\uc744 \ub0b4\uace0 \uc7a5\ubd80\ub97c \ub418\uc0c0\ub2e4. \uac12\uc744 \uce58\ub800\ub2e4\uace0 \ucc45\uc784\uae4c\uc9c0 \uc0ac\ub77c\uc9c0\ub294 \uac74 \uc544\ub2c8\ub2e4.");
        }
        else if (Holder(key) == level.Player().Id() &&
                 (npc->job == Job::Engineer || npc->job == Job::Apprentice))
        {
            npc->knowledge |= Knowledge::Key | Knowledge::Plans;
            Transfer(key, npc->Id());
            npc->thinkTime = 0;
            Event(
                npc->name +
                L"\uc5d0\uac8c \uc5f4\uc1e0\ub97c \ub9e1\uacbc\ub2e4. \uc218\ubb38\uc744 \uc5f4\uc9c0 \uadf8 \uc0ac\ub78c\ub3c4 \ud310\ub2e8\ud560 \uac83\uc774\ub2e4.");
        }
        else if (Holder(ledger) == level.Player().Id() && (npc->variant == 2 || npc->variant == 3) &&
                 recordChoice == RecordChoice::Undecided)
        {
            if (!copied)
            {
                Event(
                    L"\uba3c\uc800 \uc7a5\ud130 \uac8c\uc2dc\ud310\uc5d0\uc11c F\ub85c \uc0ac\ubcf8\uc744 \ub0a8\uae30\uc790. \uc6d0\ubcf8\uc744 \ub118\uae30\uba74 \uae30\ub85d\uc744 \ub418\ucc3e\uae30 \uc5b4\ub835\ub2e4.");
                return;
            }
            int price = npc->job == Job::Broker ? 40 : 30;
            if (npc->coins < price)
            {
                Event(
                    L"\uc0c1\ub300\uc5d0\uac8c \uc7a5\ubd80 \uac12\uc744 \uce58\ub97c \ub3c8\uc774 \uc5c6\ub2e4.");
                return;
            }
            npc->coins -= price;
            level.stats.coins += price;
            npc->knowledge |= Knowledge::Ledger;
            npc->knownLedgerHolder = npc->Id();
            npc->thinkTime = 0;
            Transfer(ledger, npc->Id());
            ChooseRecord(npc->job == Job::Guard ? RecordChoice::Authority : RecordChoice::Sold);
            UpdateQuest();
        }
        else if (IsMerchant(*npc) && level.stats.hides > 0 && npc->coins >= 6)
        {
            int amount = (std::min)(level.stats.hides, npc->coins / 6);
            level.stats.hides -= amount;
            npc->hides += amount;
            level.stats.coins += amount * 6;
            npc->coins -= amount * 6;
            Event(
                L"\uac00\uc8fd\uc744 \ud314\uc558\ub2e4. \ud55c \uc7a5\uc5d0 \uc5ec\uc12f \ub0e5, \uc11c\ub85c \ub3c8\uacfc \ubb3c\uac74\uc744 \ubc14\uafe8\ub2e4.");
        }
        else if (IsMerchant(*npc) && npc->food > 0 && level.stats.coins >= 8)
        {
            level.stats.coins -= 8;
            npc->coins += 8;
            --npc->food;
            ++level.stats.potions;
            Event(
                L"\uc5ec\ub35f \ub0e5\uc744 \ub0b4\uace0 \ud68c\ubcf5\uc57d \ud55c \ubcd1\uc744 \uc0c0\ub2e4.");
        }
        else if (npc->job == Job::Resident && npc->food > 0 && level.stats.coins >= 4 &&
                 level.stats.health < level.stats.MaxHealth())
        {
            level.stats.coins -= 4;
            npc->coins += 4;
            --npc->food;
            level.stats.health = (std::min)(level.stats.MaxHealth(), level.stats.health + 35);
            Event(
                L"\ub530\ub73b\ud55c \uad6d\uc744 \uba39\uc5c8\ub2e4. \uc228\uc774 \uc870\uae08 \ud3b8\ud574\uc84c\ub2e4.");
        }
        else
        {
            Event(
                L"\uc9c0\uae08 \ubc14\uafc0 \ubb3c\uac74\uc774\ub098 \ub3c8\uc774 \ubd80\uc871\ud558\ub2e4. \uc0c1\uc778\uc740 \uac00\uc8fd\uc744 \uc0ac\uace0, \uc57d\uc744 \ud310\ub2e4.");
            return;
        }
        Perf::Count(Perf::Counter::NpcTrades);
    }

    void World::InteractSite()
    {
        if (dialogue.Active())
        {
            return;
        }
        UpdateQuest();
        Position hero = level.Player().WorldPosition();
        if (Distance(hero, Site(2)) < 1.8f && (!briefed || questStep == 5))
        {
            BeginDialogue(nullptr);
            return;
        }
        for (ActorId artifact : {ledger, key})
        {
            Actor& item = level.Scene().Get(artifact);
            if (!Holder(artifact) && Distance(hero, item.WorldPosition()) < 1.5f &&
                level.ClearLine(hero, item.WorldPosition()))
            {
                Transfer(artifact, level.Player().Id());
                briefed = true;
                if (artifact == ledger)
                {
                    recoveredLedger = true;
                    ReactToPlayer(
                        L"\uc7a5\ubd80 \ud655\ubcf4 \u2192 NPC\ub4e4\uc740 \uc6d0\ubcf8\uc744 \uac00\uc838\uac08 \uc218 \uc5c6\ub2e4. \uc5f4\uc1e0\ub3c4 \ucc59\uae34 \ub4a4 \uc7a5\ud130\uc5d0\uc11c \uc0ac\ubcf8\uc744 \ub0a8\uae30\uc790.");
                }
                else
                {
                    recoveredKey = true;
                    ReactToPlayer(
                        L"\uc5f4\uc1e0 \ud655\ubcf4 \u2192 \uc218\ubb38 \uac1c\ubc29\uc744 \uc9c1\uc811 \uacb0\uc815\ud558\uac70\ub098 \uc218\ub9ac\uacf5\uc5d0\uac8c \ub9e1\uae38 \uc218 \uc788\ub2e4.");
                }
                UpdateQuest();
                return;
            }
        }
        if (Distance(hero, Site(1)) < 1.8f)
        {
            if (gateOpen)
            {
                Event(
                    L"\uc218\ubb38\uc740 \uc774\ubbf8 \uc5f4\ub824 \uc788\ub2e4. \ubb3c\uc740 \ube60\uc84c\uc9c0\ub9cc \ubd09\uc778\ub41c \uad6c\uc5ed\uc744 \ub450\uace0 \ub2e4\ud23c\uc774 \uc2dc\uc791\ub410\ub2e4.");
            }
            else if (Holder(key) == level.Player().Id())
            {
                OpenGate(nullptr);
            }
            else
            {
                Event(
                    L"\uc218\ubb38\uc774 \uc7a0\uaca8 \uc788\ub2e4. \ubc30\uc218\ub85c\uc5d0\uc11c \ub098\uc628 \uc5f4\uc1e0\uac00 \ud544\uc694\ud558\ub2e4.");
            }
            return;
        }
        if (Distance(hero, Site(0)) < 1.8f)
        {
            Event(
                controlled
                    ? L"\ud56d\ub9cc\uccad\uc774 \uc785\uad6c\ub97c \uc9c0\ud0a8\ub2e4. \uc606\uae38\uc740 \uc5f4\ub824 \uc788\uc9c0\ub9cc \ub208\uc5d0 \ub744\uba74 \uc758\uc2ec\uc744 \uc0b4 \uac83\uc774\ub2e4."
                    : L"\ubb3c \uc544\ub798 \uc788\ub358 \uc61b \ud654\ubb3c\uc774 \ub4dc\ub7ec\ub0ac\ub2e4. \ub204\uad70\uac00 \uba3c\uc800 \ucc59\uae30\uba74 \uc8fc\uc778\uc740 \ub2ec\ub77c\uc9c8 \uac83\uc774\ub2e4.");
            return;
        }
        if (Distance(hero, Site(2)) < 1.8f)
        {
            if (Holder(ledger) == level.Player().Id())
            {
                copied = true;
                ReactToPlayer(
                    L"\uc7a5\ubd80 \uc0ac\ubcf8 \uc791\uc131 \u2192 \uc6d0\ubcf8\uc744 \ub118\uaca8\ub3c4 \uc99d\uac70\uac00 \ub0a8\ub294\ub2e4. \uacf5\uac1c\u00b7\uad00\uccad\u00b7\ud310\ub9e4 \uc911 \uacb0\uc815\ud558\uc790.");
                UpdateQuest();
                Event(
                    L"\uc7a5\ubd80\ub97c \ubca0\uaef4 \ub450\uc5c8\ub2e4. \uc6d0\ubcf8\uc744 \ub118\uaca8\ub3c4 R\ub85c \uc0ac\ubcf8\uc744 \uacf5\uac1c\ud560 \uc218 \uc788\ub2e4.");
            }
            else
            {
                Event(
                    L"\uc870\ud569 \uc0ac\ubb34\uc18c\uc640 \uc7a5\ud130. T\ub85c \uac00\uc8fd\uc744 \ud314\uac70\ub098 \uc57d\uc744 \uc0ac\uace0, \uc7a5\ubd80\ub97c \uac00\uc9c4 \ucc44 F\ub85c \uc0ac\ubcf8\uc744 \ub0a8\uae38 \uc218 \uc788\ub2e4.");
            }
            return;
        }
        for (int index = 3; index < int(sites.size()); ++index)
        {
            if (Distance(hero, Site(index)) < 1.8f)
            {
                if (index == 4 && returned && !explored)
                {
                    explored = true;
                    level.stats.GainExperience(40);
                    ReactToPlayer(
                        L"\uc194\ubc14\ub78c \uac70\uc810 \ubc1c\uacac \u2192 \uacbd\ud5d8\uce58 40. \uc774\uc81c \uc0ac\ub0e5\u00b7\uac70\ub798\ub85c \uc131\uc7a5\ud558\uba70 \ub354 \uba3c \uc0b0\uae38\uc744 \ud0d0\ud5d8\ud558\uc790.");
                    UpdateQuest();
                    return;
                }
                const auto& landmark = static_cast<const Landmark&>(level.Scene().Get(sites[index]));
                Event(
                    landmark.label +
                    (index == 7
                         ? L"\u00b7 \ubcf5\uba74\uc744 \uc4f4 \uc57d\ud0c8\uc790\ub4e4\uc758 \uac70\uc810\uc774\ub2e4. \ub9c8\uc744\uacfc\ub294 \uba40\ub9ac \ub5a8\uc5b4\uc838 \uc788\ub2e4."
                         : L"\u00b7 \uc774 \uae38\uc744 \ub530\ub77c \ub2e4\uc74c \uac70\uc810\uc73c\ub85c \uac08 \uc218 \uc788\ub2e4."));
                return;
            }
        }
        Event(
            L"F\ub294 \uac8c\uc2dc\ud310\u00b7\ubc30\uc218\ub85c\u00b7\uc218\ubb38\u00b7\uc678\uacfd \uac70\uc810 \uac00\uae4c\uc774\uc5d0\uc11c \uc0ac\uc6a9\ud55c\ub2e4.");
    }

    void World::Reveal()
    {
        if (recordChoice != RecordChoice::Undecided)
        {
            Event(
                L"\uc7a5\ubd80\uc758 \ucc98\ubd84\uc740 \uc774\ubbf8 \uc815\ud588\ub2e4. J\ub97c \uc5f4\uba74 \uadf8 \uacb0\uc815\uacfc \uc9c0\uae08 \ud574\uc57c \ud560 \uc77c\uc744 \ubcfc \uc218 \uc788\ub2e4.");
        }
        else if (Distance(level.Player().WorldPosition(), Site(2)) > WorldLayout::MarketRadius)
        {
            Event(
                L"\uc7a5\ubd80\ub97c \uacf5\uac1c\ud558\ub824\uba74 \uc0ac\ub78c\ub4e4\uc774 \ubaa8\uc778 \uc7a5\ud130\ub85c \ub3cc\uc544\uac00\uc790.");
        }
        else if (!copied)
        {
            Event(
                L"\uc7a5\ud130 \uac8c\uc2dc\ud310\uc5d0\uc11c F\ub85c \uc0ac\ubcf8\uc744 \uba3c\uc800 \ub0a8\uae30\uc790. \uacf5\uac1c\ub420 \uc774\ub984\uacfc \ucc45\uc784\uc744 \ub2e4\uc2dc \ud655\uc778\ud560 \uc2dc\uac04\uc774\ub2e4.");
        }
        else
        {
            ChooseRecord(RecordChoice::Public);
            UpdateQuest();
        }
    }

    std::wstring World::AreaName() const
    {
        Position hero = level.Player().WorldPosition();
        int village = WorldLayout::VillageAt(hero);
        if (village >= 0)
        {
            return std::wstring(WorldLayout::VillageName(village)) +
                   L" \u00b7 \ub9c8\uc744 \ubcf4\ud638 \uad6c\uc5ed";
        }
        if (Distance(hero, WorldLayout::RaiderCamp()) < WorldLayout::RaiderTerritoryRadius + 8)
        {
            return L"\ubd89\uc740\uace8 \u00b7 \ubcf5\uba74 \uc57d\ud0c8\uc790\uc758 \uc601\uc5ed, \uc8fc\uba38\ub2c8\uc640 \uccb4\ub825\uc744 \uc0b4\ud53c\uc790";
        }
        if (Distance(hero, WorldLayout::HunterCamp()) < 16)
        {
            return L"\uc194\ubc14\ub78c \uc232 \u00b7 \uc0ac\ub0e5\uafbc\ub4e4\uc758 \uac70\uc810";
        }
        if (Distance(hero, WorldLayout::Waystation()) < 12)
        {
            return L"\uae34\uae38 \uc8fc\ub9c9 \u00b7 \ubb34\uc5f0\ud56d\uacfc \uba3c \uc0b0\uae38\uc744 \uc787\ub294 \uc27c\ud130";
        }
        if (Distance(hero, WorldLayout::OldShrine()) < 12)
        {
            return L"\uc61b \uc0b0\uc0ac \u00b7 \ubd89\uc740\uace8\ub85c \uc774\uc5b4\uc9c0\ub294 \uc624\ub798\ub41c \uc0b0\uae38";
        }
        return L"\ud574\ub3d9\uad6d \uc678\uacfd \u00b7 \ud070\uae38\uc744 \ub530\ub77c \uc232\uacfc \ud638\uc218, \uba3c \uac70\uc810\uc744 \ucc3e\uc544\ubcf4\uc790";
    }

    std::wstring World::Clock() const
    {
        int time = int(minutes);
        int hour = time % 1440 / 60, minute = time % 60;
        return std::to_wstring(time / 1440 + 1) + L"\uc77c " + (hour < 10 ? L"0" : L"") +
               std::to_wstring(hour) + L":" + (minute < 10 ? L"0" : L"") + std::to_wstring(minute);
    }

    std::wstring World::Status() const
    {
        auto owner = [&](ActorId artifact)
        {
            ActorId holder = Holder(artifact);
            if (!holder)
            {
                return std::wstring(L"\ud604\uc7a5");
            }
            if (holder == level.Player().Id())
            {
                return std::wstring(L"\ub098");
            }
            const auto* npc = dynamic_cast<const Npc*>(level.Scene().Find(holder));
            return npc ? npc->name : std::wstring(L"\uc54c \uc218 \uc5c6\uc74c");
        };
        return Clock() + L"  \uc7a5\ubd80: " + owner(ledger) + L"  \uc5f4\uc1e0: " + owner(key) +
               L"  \uce68\uc218 " + std::to_wstring(flood) + L"%";
    }

    std::vector<std::wstring> World::Journal() const
    {
        std::vector<std::wstring> lines;
        lines.push_back(L"\uc758\ub8b0: \ubb3c\uae38 \uc544\ub798\uc758 \uc774\ub984\ub4e4");
        lines.push_back(Objective());
        lines.push_back(ObjectiveHint());
        lines.push_back(L"\ub0b4 \uc120\ud0dd\uc758 \uacb0\uacfc: " + Consequence());
        lines.push_back(Status());
        lines.push_back(
            L"\uacf5\uac1c: \uac00\uc871\ub4e4\uc774 \uc7a5\ud130\uc5d0 \ubaa8\uc778\ub2e4 / \uad00\uccad: \ud604\uc7a5 \ud1b5\uc81c / \ud310\ub9e4: \uc774\uc548\uc774 \ubd80\ub450\ub85c \uac04\ub2e4");
        lines.push_back(
            L"\uc218\ubb38\uc744 \uc5f4\uba74 \uc7a0\uae34 \uace8\ubaa9\uc774 \uae38\uc774 \ub418\uace0 \uc8fc\ubbfc\ub4e4\uc774 \uadf8\uacf3\uc73c\ub85c \uc774\ub3d9\ud55c\ub2e4");
        lines.push_back(
            L"\uccab \uc758\ub8b0 \ubcf4\uc0c1: \uacbd\ud5d8\uce58 120 + 20\ub0e5 / \uc194\ubc14\ub78c \uac70\uc810 \ubc1c\uacac: \uacbd\ud5d8\uce58 40");
        int shown = 0;
        for (const auto& event : history)
        {
            if (shown++ == 3)
            {
                break;
            }
            lines.push_back(event);
        }
        return lines;
    }

    void World::Log()
    {
        if (Perf::Profiler::Get().Enabled())
        {
            Perf::Profiler::Get().WorldSnapshot(SnapshotJson());
        }
    }

    std::string World::SnapshotJson() const
    {
        Perf::Scope scope(Perf::Cpu::WorldSnapshot);
        const auto& scene = static_cast<const Level::FirstLevel&>(level).Scene();
        std::vector<const Npc*> npcs;
        for (ActorId id : scene.FindByKind(ActorKind::Npc, false))
        {
            if (const auto* npc = dynamic_cast<const Npc*>(scene.Find(id)))
            {
                npcs.push_back(npc);
            }
        }
        Perf::Count(Perf::Counter::WorldSnapshotNpcs, npcs.size());
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << std::fixed << std::setprecision(2) << "{\"seed\":" << level.seed
            << ",\"world_minute\":" << minutes << ",\"map_size_tiles\":" << Level::MapSize
            << ",\"player_actor_id\":" << level.Player().Id() << ",\"quest_step\":" << questStep
            << ",\"record_choice\":" << int(recordChoice)
            << ",\"quest_target_actor_id\":" << ObjectiveTarget()
            << ",\"quest_briefed\":" << (briefed ? "true" : "false")
            << ",\"quest_ledger_recovered\":" << (recoveredLedger ? "true" : "false")
            << ",\"quest_key_recovered\":" << (recoveredKey ? "true" : "false")
            << ",\"quest_returned\":" << (returned ? "true" : "false")
            << ",\"hunter_camp_discovered\":" << (explored ? "true" : "false")
            << ",\"player_coins\":" << level.stats.coins << ",\"player_health\":" << level.stats.health
            << ",\"player_hides\":" << level.stats.hides << ",\"player_potions\":" << level.stats.potions
            << ",\"ledger_holder_actor_id\":" << Holder(ledger) << ",\"key_holder_actor_id\":" << Holder(key)
            << ",\"gate_open\":" << (gateOpen ? "true" : "false")
            << ",\"ledger_published\":" << (published ? "true" : "false")
            << ",\"player_has_copy\":" << (copied ? "true" : "false")
            << ",\"ledger_water_damaged\":" << (ledgerWet ? "true" : "false")
            << ",\"drain_controlled\":" << (controlled ? "true" : "false")
            << ",\"drain_repaired\":" << (repaired ? "true" : "false") << ",\"flood_percent\":" << flood
            << ",\"faction_trust\":[";
        for (size_t i = 0; i < trust.size(); ++i)
        {
            out << (i ? "," : "") << trust[i];
        }
        out << "],\"npcs\":[";
        bool comma = false;
        for (const Npc* npc : npcs)
        {
            Position p = npc->WorldPosition();
            out << (comma ? "," : "") << "{\"actor_id\":" << npc->Id()
                << ",\"definition_id\":" << npc->variant << ",\"name\":" << JsonWide(npc->name)
                << ",\"dialogue\":" << JsonWide(npc->line) << ",\"faction\":" << npc->faction
                << ",\"active\":" << (npc->IsActive() ? "true" : "false")
                << ",\"alive\":" << (npc->health > 0 ? "true" : "false") << ",\"home_settlement_id\":\""
                << SettlementId(npc->homeVillage) << "\",\"current_settlement_id\":\""
                << SettlementId(SettlementAt(p)) << "\",\"job\":\"" << JobKey(npc->job) << "\",\"action\":\""
                << ActionKey(npc->action) << "\",\"health\":" << npc->health
                << ",\"max_health\":" << npc->maxHealth << ",\"coins\":" << npc->coins
                << ",\"hides\":" << npc->hides << ",\"food\":" << npc->food
                << ",\"enlightenment\":" << npc->enlightenment << ",\"experience\":" << npc->experience
                << ",\"affinity\":" << npc->affinity << ",\"witnessed_karma\":" << npc->karma
                << ",\"knowledge_mask\":" << npc->knowledge << ",\"appearance_id\":" << npc->appearance
                << ",\"is_courier\":" << (npc->courier ? "true" : "false")
                << ",\"travel_destination_settlement_id\":\"" << SettlementId(npc->travelVillage) << "\""
                << ",\"last_rumor_sender_actor_id\":" << npc->lastRumorSender
                << ",\"last_rumor_world_minute\":" << npc->lastRumorMinute
                << ",\"rumor_messages_received\":" << npc->rumorsReceived
                << ",\"raid_weight\":" << npc->raidWeight
                << ",\"raid_cooldown_seconds\":" << npc->raidCooldown << ",\"home_x\":" << npc->home.x
                << ",\"home_y\":" << npc->home.y
                << ",\"known_ledger_holder_actor_id\":" << npc->knownLedgerHolder
                << ",\"has_ledger_copy\":" << (npc->ledgerCopy ? "true" : "false")
                << ",\"health_weight\":" << npc->healthWeight << ",\"wealth_weight\":" << npc->wealthWeight
                << ",\"enlightenment_weight\":" << npc->enlightenmentWeight
                << ",\"target_actor_id\":" << npc->target << ",\"world_x\":" << p.x << ",\"world_y\":" << p.y
                << ",\"destination_x\":" << npc->destination.x << ",\"destination_y\":" << npc->destination.y
                << ",\"path_remaining_waypoints\":"
                << (npc->waypoint < npc->path.size() ? npc->path.size() - npc->waypoint : 0) << "}";
            comma = true;
        }
        int aliveNpcs = 0;
        for (const Npc* npc : npcs)
        {
            aliveNpcs += npc->health > 0 ? 1 : 0;
        }
        int monstersTotal = 0, monstersAlive = 0;
        std::array<int, 3> monstersByKind{};
        for (ActorId id : scene.FindByKind(ActorKind::Enemy, false))
        {
            if (const auto* enemy = dynamic_cast<const Level::Enemy*>(scene.Find(id)))
            {
                ++monstersTotal;
                if (enemy->health > 0 && enemy->IsActive())
                {
                    ++monstersAlive;
                    if (enemy->kind >= 0 && enemy->kind < int(monstersByKind.size()))
                    {
                        ++monstersByKind[size_t(enemy->kind)];
                    }
                }
            }
        }
        out << "],\"counts\":{\"villages_total\":" << WorldLayout::VillageCount
            << ",\"npcs_per_village_target\":" << WorldLayout::NpcsPerVillage
            << ",\"monsters_target\":" << WorldLayout::MonsterCount << ",\"npcs_total\":" << npcs.size()
            << ",\"npcs_alive\":" << aliveNpcs << ",\"npcs_dead\":" << npcs.size() - size_t(aliveNpcs)
            << ",\"monsters_total\":" << monstersTotal << ",\"monsters_alive\":" << monstersAlive
            << ",\"monsters_dead_or_inactive\":" << monstersTotal - monstersAlive
            << ",\"monsters_alive_by_kind\":[" << monstersByKind[0] << ',' << monstersByKind[1] << ','
            << monstersByKind[2] << "]},\"settlements\":[";
        const auto& places = Settlements();
        for (size_t i = 0; i < places.size(); ++i)
        {
            const auto& place = places[i];
            int residents = 0, deadResidents = 0, present = 0, visitors = 0;
            int wounded = 0, conflicts = 0, trading = 0, wealth = 0, health = 0, maxHealth = 0;
            int ledgerKnown = 0, plansKnown = 0, publishedKnown = 0, localNews = 0;
            std::array<int, 4> factions{};
            std::array<double, 4> weights{}; // health, wealth, enlightenment, raid
            for (const Npc* npc : npcs)
            {
                bool home = npc->homeVillage == int(i);
                bool here = SettlementAt(npc->WorldPosition()) == int(i);
                if (home && npc->health <= 0)
                {
                    ++deadResidents;
                }
                if (npc->health <= 0)
                {
                    continue;
                }
                if (here)
                {
                    ++present;
                    visitors += home ? 0 : 1;
                    wounded += npc->health < npc->maxHealth / 2 ? 1 : 0;
                    conflicts += npc->action == Action::Raid || npc->action == Action::Defend ? 1 : 0;
                    trading += npc->action == Action::Trade ? 1 : 0;
                    localNews += (npc->knowledge & Knowledge::Published) ? 1 : 0;
                }
                if (!home)
                {
                    continue;
                }
                ++residents;
                wealth += npc->coins;
                health += npc->health;
                maxHealth += npc->maxHealth;
                ledgerKnown += (npc->knowledge & Knowledge::Ledger) ? 1 : 0;
                plansKnown += (npc->knowledge & Knowledge::Plans) ? 1 : 0;
                publishedKnown += (npc->knowledge & Knowledge::Published) ? 1 : 0;
                if (npc->faction >= 0 && npc->faction < int(factions.size()))
                {
                    ++factions[size_t(npc->faction)];
                }
                weights[0] += npc->healthWeight;
                weights[1] += npc->wealthWeight;
                weights[2] += npc->enlightenmentWeight;
                weights[3] += npc->raidWeight;
            }
            const char* mood = MoodKeys[VillageMood(int(i), npcs)];
            size_t dominant = 0;
            for (size_t w = 1; w < weights.size(); ++w)
            {
                if (weights[w] > weights[dominant])
                {
                    dominant = w;
                }
            }
            const char* tendencies[] = {"survival", "wealth", "enlightenment", "raiding"};
            out << (i ? "," : "") << "{\"id\":\"" << place.id << "\",\"name\":" << JsonWide(place.name)
                << ",\"kind\":\"" << place.kind << "\",\"world_x\":" << place.center.x
                << ",\"world_y\":" << place.center.y << ",\"radius_tiles\":" << place.radius
                << ",\"population\":" << residents << ",\"dead_residents\":" << deadResidents
                << ",\"npcs_present\":" << present << ",\"visitors\":" << visitors
                << ",\"wealth_coins\":" << wealth << ",\"health_total\":" << health
                << ",\"max_health_total\":" << maxHealth << ",\"wounded_present\":" << wounded
                << ",\"conflicts_present\":" << conflicts << ",\"trading_present\":" << trading
                << ",\"mood\":\"" << mood << "\",\"mood_source\":\"derived_from_live_state\""
                << ",\"flood_percent\":" << (i == 0 ? flood : 0) << ",\"tendency\":\""
                << (residents ? tendencies[dominant] : "none")
                << "\",\"goal_weight_totals\":{\"health\":" << weights[0] << ",\"wealth\":" << weights[1]
                << ",\"enlightenment\":" << weights[2] << ",\"raid\":" << weights[3]
                << "},\"rumors\":{\"ledger_known\":" << ledgerKnown << ",\"plans_known\":" << plansKnown
                << ",\"published_known\":" << publishedKnown << ",\"published_present\":" << localNews
                << "},\"rumor_deliveries\":" << villageStatistics[i].rumorDeliveries
                << ",\"faction_population\":[" << factions[0] << ',' << factions[1] << ',' << factions[2]
                << ',' << factions[3] << "]"
                << ",\"mood_statistics\":{\"sample_interval_seconds\":1,\"observed_seconds\":"
                << villageStatistics[i].observedSeconds << ",\"seconds\":{";
            for (size_t m = 0; m < villageStatistics[i].moodSeconds.size(); ++m)
            {
                out << (m ? "," : "") << '"' << MoodKeys[m] << "\":" << villageStatistics[i].moodSeconds[m];
            }
            out << "},\"percent\":{";
            for (size_t m = 0; m < villageStatistics[i].moodSeconds.size(); ++m)
            {
                double observed = villageStatistics[i].observedSeconds;
                out << (m ? "," : "") << '"' << MoodKeys[m]
                    << "\":" << (observed > 0 ? villageStatistics[i].moodSeconds[m] * 100 / observed : 0);
            }
            out << "}}}";
        }
        out << "],\"objective\":" << JsonWide(Objective()) << ",\"consequence\":" << JsonWide(Consequence())
            << ",\"clock\":" << JsonWide(Clock()) << ",\"recent_events\":[";
        bool eventComma = false;
        for (const auto& event : history)
        {
            out << (eventComma ? "," : "") << JsonWide(event);
            eventComma = true;
        }
        out << "]}";
        return out.str();
    }
} // namespace Harbor
