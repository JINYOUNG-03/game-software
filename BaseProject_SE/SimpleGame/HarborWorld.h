#pragma once

#include "LevelOne.h"
#include <array>
#include <deque>

namespace Harbor
{
    enum class Job
    {
        Engineer,
        Clerk,
        Guard,
        Broker,
        Apprentice,
        Hunter,
        Raider,
        Merchant,
        Scholar,
        Resident
    };
    enum class Action
    {
        Rest,
        Hunt,
        Raid,
        Trade,
        Study,
        Investigate,
        Return,
        Patrol,
        Dead,
        Defend
    };

    enum Knowledge
    {
        Ledger = 1,
        Key = 2,
        Plans = 4,
        Published = 8
    };

    enum class RecordChoice
    {
        Undecided,
        Public,
        Authority,
        Sold
    };

    struct Npc : public Actor
    {
        std::wstring name;
        std::wstring line;
        Job job = Job::Resident;
        Action action = Action::Rest;
        int faction = 2;
        int affinity = 0;
        int karma = 0;
        int knowledge = 0;
        int health = 100;
        int maxHealth = 100;
        int coins = 25;
        int hides = 0;
        int food = 3;
        int enlightenment = 0;
        int experience = 0;
        float healthWeight = 1;
        float wealthWeight = 1;
        float enlightenmentWeight = .2f;
        float raidWeight = 0;
        float thinkTime = 0;
        float cooldown = 0;
        float actionTime = 0;
        float threatTime = 0;
        float phase = 0;
        float raidCooldown = 0;
        float raidWindow = 0;
        int raidHits = 0;
        std::uint64_t pathRevision = 0;
        Level::Position pathDestination;
        bool moving = false;
        bool scheduled = false;
        ActorId target = InvalidActorId;
        ActorId threat = InvalidActorId;
        ActorId knownLedgerHolder = InvalidActorId;
        bool searchedLedger = false;
        bool ledgerCopy = false;
        Level::Position home;
        Level::Position destination;
        Level::Position lastLedger;
        std::vector<Level::Position> path;
        size_t waypoint = 0;
    };

    struct Landmark : public Actor
    {
        std::wstring label;
        int site = 0;
    };

    struct ObjectiveGuide
    {
        int step = 0;
        ActorId target = InvalidActorId;
        std::wstring destination;
        std::wstring reason;
        std::wstring action;
        char interactionKey = 0;
        bool ready = false;
    };

    class World
    {
      public:
        explicit World(Level::FirstLevel& level) : level(level)
        {
        }

        void Initialize();
        void Update(float dt);
        void PlayerAttack();
        void Talk();
        void Trade();
        void InteractSite();
        void Reveal();
        Npc* Nearest(float radius = 1.5f);
        Actor* NearbySite(float radius = 1.8f);
        std::wstring Clock() const;
        std::wstring Status() const;
        std::wstring AreaName() const;
        std::vector<std::wstring> Journal() const;
        std::wstring Objective() const;
        std::wstring ObjectiveHint() const;
        std::wstring Consequence() const;
        ActorId ObjectiveTarget() const;
        ObjectiveGuide Guide() const;
        std::string SnapshotJson() const;
        static const wchar_t* JobLabel(Job job);
        static const wchar_t* ActionLabel(Action action);

        const std::array<ActorId, 8>& LandmarkIds() const
        {
            return sites;
        }

      private:
        using Position = Level::Position;
        Level::FirstLevel& level;
        ActorId worldRoot = InvalidActorId;
        ActorId ledger = InvalidActorId;
        ActorId key = InvalidActorId;
        ActorId barrier = InvalidActorId;
        std::array<ActorId, 8> sites{};
        std::array<int, 4> trust{};
        std::deque<std::wstring> history;
        std::mt19937 random;
        double minutes = 360;
        float logTime = 0;
        float gossipTime = 0;
        bool gateOpen = false;
        bool published = false;
        bool controlled = false;
        bool repaired = false;
        int flood = 0;
        bool copied = false;
        bool ledgerWet = false;
        bool briefed = false;
        bool recoveredLedger = false;
        bool recoveredKey = false;
        bool returned = false;
        bool explored = false;
        bool dealSettled = false;
        RecordChoice recordChoice = RecordChoice::Undecided;
        int questStep = 0;
        std::wstring consequence;

        Level::Position Site(int index) const;
        ActorId Holder(ActorId artifact) const;
        void Transfer(ActorId artifact, ActorId holder);
        void Observe(Npc& npc);
        void Think(Npc& npc, const std::vector<Npc*>& npcs, const std::vector<Level::Enemy*>& beasts);
        void Step(Npc& npc, float dt);
        void Hurt(Npc& victim, int damage, ActorId attacker);
        void Witness(ActorId attacker, const Position& where, int affinity, int karma);
        void Exchange(Npc& seller, Npc& buyer);
        void Event(const std::wstring& text, bool notifyPlayer = true);
        void Log();
        void Disclose(Npc* speaker);
        void OpenGate(Npc* actor);
        Level::Position Reachable(const Position& requested) const;
        bool RaidAllowed(const Npc& attacker, const Actor& target) const;
        Npc* Person(int definitionId) const;
        void ReactToPlayer(const std::wstring& result);
        void UpdateQuest();
        void ChooseRecord(RecordChoice choice);
    };
} // namespace Harbor
